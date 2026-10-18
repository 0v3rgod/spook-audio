/*
 * SPOOK AUDIO — mixer do hardware (Scarlett) via ALSA snd_ctl
 *
 * Fala com a interface de CONTROLE do cartao (`hw:N` -> snd_ctl_*). Isso e
 * metadado do mixer (nomes/valores): NAO abre PCM, NAO toca som. Só a escrita
 * (set_*) muda ganho de verdade.
 *
 * Refs da RE: ../re/reports/01-scarlett-protocolo.md, ../re/kernel/mixer_scarlett2.c
 * Nomes tipicos (driver scarlett2): "Mix A Input 01 Playback Volume",
 * "Line 01 Playback Volume", "Headphone Playback Volume", "Master HW Playback Volume".
 */
#pragma once

#include <string>
#include <vector>

namespace spook {

struct CtlElem {
	std::string  name;
	std::string  kind;    // "INTEGER", "ENUM", "BOOL", "BYTES", ...
	std::string  unit;    // "dB" se o TLV indicar
	bool         has_range;
	long         min, max, step;
	long         value;         // valor atual quando numerico
	std::string  value_text;    // para ENUM: o rotulo atual
	std::vector<std::string> items; // para ENUM
	unsigned int count;
	bool         writable;
};

class AlsaMixer {
  public:
	AlsaMixer () : _ctl (0), _card (-1) {}
	~AlsaMixer () { close (); }

	AlsaMixer (AlsaMixer const&) = delete;
	AlsaMixer& operator= (AlsaMixer const&) = delete;

	/* Cartoes presentes no sistema. */
	struct Card {
		int         index;
		std::string id;          // "NVidia", "sofhdadsp"… (snd_ctl_card_info.id)
		std::string name;        // "HDA NVidia" (snd_ctl_card_info.name)
		std::string longname;    // descritivo (snd_ctl_card_info.longname)
		std::string mixer_name;  // "Realtek ALC…" / "" 
		std::string components;  // "USB1235…" — o VID (Focusrite = 1235) aparece aqui
		bool        scarlett;
	};
	static std::vector<Card> cards ();

	bool open (int card, std::string& err);
	void close ();
	bool is_open () const { return _ctl != 0; }
	int  card_index () const { return _card; }

	/* Enumera TODOS os controles de mixer do cartao. */
	bool read_all (std::vector<CtlElem>& out, std::string& err);

	/* Le um controle pelo nome exato. */
	bool read_one (std::string const& name, CtlElem& out, std::string& err);

	/* NIVEIS (VU): le o controle "Level Meter" (iface=PCM) — no Solo 4th
	 * Gen sao 12 valores ao vivo 0..4095 (indice = destino do roteamento;
	 * [0]=Saida Analogica 1 e [1]=Saida 2 sao os unicos confirmados). */
	bool read_meters (std::vector<long>& out, std::string& err);

	bool set_integer (std::string const& name, long value, std::string& err);
	bool set_enum (std::string const& name, std::string const& label, std::string& err);

  private:
	void* _ctl; // snd_ctl_t*
	int   _card;
};

} // namespace spook
