#include "core/scarlett.h"
#include "core/log.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

#include <alsa/asoundlib.h>

namespace spook {

namespace {

std::string
lower (std::string s)
{
	std::transform (s.begin (), s.end (), s.begin (),
	                [] (unsigned char c) { return (char) std::tolower (c); });
	return s;
}

bool
looks_scarlett (std::string const& s)
{
	std::string l = lower (s);
	return l.find ("scarlett") != std::string::npos
	    || l.find ("focusrite") != std::string::npos;
}

std::string
kind_name (snd_ctl_elem_type_t t)
{
	switch (t) {
		case SND_CTL_ELEM_TYPE_BOOLEAN:    return "BOOL";
		case SND_CTL_ELEM_TYPE_INTEGER:    return "INTEGER";
		case SND_CTL_ELEM_TYPE_ENUMERATED: return "ENUM";
		case SND_CTL_ELEM_TYPE_BYTES:      return "BYTES";
		case SND_CTL_ELEM_TYPE_IEC958:     return "IEC958";
		case SND_CTL_ELEM_TYPE_INTEGER64:  return "INTEGER64";
		default: return "?";
	}
}

} // namespace

std::vector<AlsaMixer::Card>
AlsaMixer::cards ()
{
	std::vector<Card> out;
	int card = -1;

	while (snd_card_next (&card) == 0 && card >= 0) {
		char dev[32];
		std::snprintf (dev, sizeof (dev), "hw:%d", card);

		Card c;
		c.index    = card;
		c.scarlett = false;

		snd_ctl_t* ctl = 0;
		if (snd_ctl_open (&ctl, dev, 0) == 0) {
			/* O caminho bom: a info do cartao diz id/nome/driver/mixer/componentes. */
			snd_ctl_card_info_t* info = 0;
			snd_ctl_card_info_alloca (&info);
			if (snd_ctl_card_info (ctl, info) == 0) {
				const char* id = snd_ctl_card_info_get_id (info);
				const char* nm = snd_ctl_card_info_get_name (info);
				const char* ln = snd_ctl_card_info_get_longname (info);
				const char* mx = snd_ctl_card_info_get_mixername (info);
				const char* cp = snd_ctl_card_info_get_components (info);
				c.id         = id ? id : "";
				c.name       = nm ? nm : c.id;
				c.longname   = ln ? ln : "";
				c.mixer_name = mx ? mx : "";
				c.components = cp ? cp : "";
			}
			snd_ctl_close (ctl);
		}

		if (c.id.empty ()) {
			/* Fallback (permissao no /dev/snd/controlC*): nomes do /proc. */
			char* a = 0;
			char* b = 0;
			snd_card_get_name (card, &a);
			snd_card_get_longname (card, &b);
			c.id   = a ? a : "";
			c.name = b ? b : c.id;
			if (a) { free (a); }
			if (b) { free (b); }
		}

		/* Focusrite = VID 1235; o driver scarlett2 cria o mixer com nome
		 * "Scarlett2" e usa "USB1235" nos componentes. */
		c.scarlett = looks_scarlett (c.id)
		          || looks_scarlett (c.name)
		          || looks_scarlett (c.longname)
		          || looks_scarlett (c.mixer_name)
		          || c.components.find ("1235") != std::string::npos;
		out.push_back (c);
	}
	return out;
}

bool
AlsaMixer::open (int card, std::string& err)
{
	close ();
	char dev[32];
	std::snprintf (dev, sizeof (dev), "hw:%d", card);

	snd_ctl_t* ctl = 0;
	int r = snd_ctl_open (&ctl, dev, 0);
	if (r < 0) {
		err = std::string ("snd_ctl_open(") + dev + ") falhou: " + snd_strerror (r);
		return false;
	}
	_ctl  = ctl;
	_card = card;
	return true;
}

void
AlsaMixer::close ()
{
	if (_ctl) {
		snd_ctl_close ((snd_ctl_t*) _ctl);
		_ctl = 0;
	}
	_card = -1;
}

bool
AlsaMixer::read_all (std::vector<CtlElem>& out, std::string& err)
{
	out.clear ();
	if (!_ctl) { err = "mixer fechado"; return false; }
	snd_ctl_t* ctl = (snd_ctl_t*) _ctl;

	snd_ctl_elem_list_t* list = 0;
	snd_ctl_elem_list_alloca (&list);
	if (snd_ctl_elem_list (ctl, list) < 0) { err = "snd_ctl_elem_list falhou"; return false; }

	unsigned int n = snd_ctl_elem_list_get_count (list);
	snd_ctl_elem_list_alloc_space (list, n);
	if (snd_ctl_elem_list (ctl, list) < 0) { err = "snd_ctl_elem_list(2) falhou"; return false; }

	for (unsigned int i = 0; i < snd_ctl_elem_list_get_used (list); ++i) {
		snd_ctl_elem_id_t* id = 0;
		snd_ctl_elem_id_alloca (&id);
		snd_ctl_elem_list_get_id (list, i, id);

		/* só o mixer do cartao nos interessa */
		unsigned int iface = snd_ctl_elem_id_get_interface (id);
		if (iface != SND_CTL_ELEM_IFACE_MIXER) { continue; }

		CtlElem e;
		e.name  = snd_ctl_elem_id_get_name (id);
		e.count = 0;
		e.has_range = false;
		e.min = e.max = e.step = e.value = 0;
		e.writable = false;

		snd_ctl_elem_info_t* info = 0;
		snd_ctl_elem_info_alloca (&info);
		snd_ctl_elem_info_set_id (info, id);
		if (snd_ctl_elem_info (ctl, info) < 0) { continue; }

		e.count    = snd_ctl_elem_info_get_count (info);
		snd_ctl_elem_type_t t = snd_ctl_elem_info_get_type (info);
		e.kind     = kind_name (t);
		e.writable = snd_ctl_elem_info_is_writable (info) != 0;

		if (t == SND_CTL_ELEM_TYPE_INTEGER) {
			e.has_range = true;
			e.min  = snd_ctl_elem_info_get_min (info);
			e.max  = snd_ctl_elem_info_get_max (info);
			e.step = snd_ctl_elem_info_get_step (info);
			if (snd_ctl_elem_info_is_tlv_readable (info)) {
				unsigned int tlv[64];
				if (snd_ctl_elem_tlv_read (ctl, id, tlv, sizeof (tlv)) >= 0) {
					e.unit = "dB"; /* presenca de TLV == escala em dB */
				}
			}
		} else if (t == SND_CTL_ELEM_TYPE_ENUMERATED) {
			unsigned int items = snd_ctl_elem_info_get_items (info);
			for (unsigned int k = 0; k < items; ++k) {
				snd_ctl_elem_info_set_item (info, k);
				if (snd_ctl_elem_info (ctl, info) < 0) { break; }
				e.items.push_back (snd_ctl_elem_info_get_item_name (info));
			}
		}

		snd_ctl_elem_value_t* val = 0;
		snd_ctl_elem_value_alloca (&val);
		snd_ctl_elem_value_set_id (val, id);
		if (snd_ctl_elem_read (ctl, val) >= 0) {
			if (t == SND_CTL_ELEM_TYPE_INTEGER) {
				e.value = snd_ctl_elem_value_get_integer (val, 0);
			} else if (t == SND_CTL_ELEM_TYPE_BOOLEAN) {
				e.value = snd_ctl_elem_value_get_boolean (val, 0);
			} else if (t == SND_CTL_ELEM_TYPE_ENUMERATED) {
				unsigned int idx = snd_ctl_elem_value_get_enumerated (val, 0);
				e.value = (long) idx;
				if (idx < e.items.size ()) { e.value_text = e.items[idx]; }
			}
		}

		out.push_back (e);
	}

	std::sort (out.begin (), out.end (), [] (CtlElem const& a, CtlElem const& b) {
		return a.name < b.name;
	});
	return true;
}

bool
AlsaMixer::read_one (std::string const& name, CtlElem& out, std::string& err)
{
	std::vector<CtlElem> all;
	if (!read_all (all, err)) { return false; }
	for (size_t i = 0; i < all.size (); ++i) {
		if (all[i].name == name) { out = all[i]; return true; }
	}
	err = "controle nao encontrado: " + name;
	return false;
}

bool
AlsaMixer::set_integer (std::string const& name, long value, std::string& err)
{
	if (!_ctl) { err = "mixer fechado"; return false; }
	snd_ctl_t* ctl = (snd_ctl_t*) _ctl;

	snd_ctl_elem_id_t* id = 0;
	snd_ctl_elem_id_alloca (&id);
	snd_ctl_elem_id_set_interface (id, SND_CTL_ELEM_IFACE_MIXER);
	snd_ctl_elem_id_set_name (id, name.c_str ());

	snd_ctl_elem_value_t* val = 0;
	snd_ctl_elem_value_alloca (&val);
	snd_ctl_elem_value_set_id (val, id);
	snd_ctl_elem_value_set_integer (val, 0, value);

	int r = snd_ctl_elem_write (ctl, val);
	if (r < 0) { err = "escrita em " + name + " falhou: " + snd_strerror (r); return false; }
	return true;
}

bool
AlsaMixer::set_enum (std::string const& name, std::string const& label, std::string& err)
{
	if (!_ctl) { err = "mixer fechado"; return false; }
	CtlElem e;
	if (!read_one (name, e, err)) { return false; }

	unsigned int idx = 0;
	for (size_t i = 0; i < e.items.size (); ++i) {
		if (e.items[i] == label) { idx = (unsigned int) i; break; }
	}
	if (idx >= e.items.size ()) { err = "rotulo invalido para " + name + ": " + label; return false; }

	snd_ctl_t* ctl = (snd_ctl_t*) _ctl;
	snd_ctl_elem_id_t* id = 0;
	snd_ctl_elem_id_alloca (&id);
	snd_ctl_elem_id_set_interface (id, SND_CTL_ELEM_IFACE_MIXER);
	snd_ctl_elem_id_set_name (id, name.c_str ());

	snd_ctl_elem_value_t* val = 0;
	snd_ctl_elem_value_alloca (&val);
	snd_ctl_elem_value_set_id (val, id);
	snd_ctl_elem_value_set_enumerated (val, 0, idx);

	int r = snd_ctl_elem_write (ctl, val);
	if (r < 0) { err = "escrita em " + name + " falhou: " + snd_strerror (r); return false; }
	return true;
}

} // namespace spook
