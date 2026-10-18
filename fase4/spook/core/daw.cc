#include "core/daw.h"
#include "core/log.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>

#include "ardour/ardour.h"
#include "ardour/audio_track.h"
#include "ardour/audiofilesource.h"
#include "ardour/audioregion.h"
#include "ardour/automation_control.h"
#include "ardour/dB.h"
#include "ardour/gain_control.h"
#include "ardour/location.h"
#include "ardour/meter.h"
#include "ardour/midi_track.h"
#include "ardour/midi_source.h"
#include "ardour/midi_region.h"
#include "ardour/midi_model.h"
#include "ardour/playlist.h"
#include "ardour/region.h"
#include "ardour/region_factory.h"
#include "ardour/mute_control.h"
#include "ardour/pannable.h"
#include "ardour/plugin_manager.h"
#include "ardour/presentation_info.h"
#include "ardour/route.h"
#include "ardour/session.h"
#include "ardour/solo_control.h"
#include "ardour/source_factory.h"
#include "ardour/track.h"
#include "ardour/types.h"

#include "control_protocol/basic_ui.h"

#include "temporal/bbt_argument.h"
#include "temporal/beats.h"
#include "temporal/tempo.h"

#include "evoral/Note.h"
#include "temporal/timeline.h"

using namespace ARDOUR;
using namespace Temporal;

namespace spook {

namespace {

/* BBT_Argument -> "001|01|000" (o readout de barras do Ardour) */
std::string
bbt_text (BBT_Argument const& bbt)
{
	char buf[32];
	std::snprintf (buf, sizeof (buf), "%03d|%02d|%03d", bbt.bars, bbt.beats, bbt.ticks);
	return buf;
}

std::string
marker_kind (Location const& l)
{
	if (l.is_auto_loop ())    { return "LOOP"; }
	if (l.is_auto_punch ())   { return "PUNCH"; }
	if (l.is_session_range ()) { return "SESSÃO"; }
	if (l.is_range_marker ()) { return "REGIÃO"; }
	if (l.is_mark ())         { return "MARCA"; }
	return "—";
}

} // namespace

Daw::Daw ()
	: _session (0)
{
}

Daw::~Daw () = default;

void
Daw::attach (Session* s)
{
	_session = s;
	_ui.reset (s ? new BasicUI (*s) : 0);
}

/* ------------------------------------------------------------- TRANSPORTE */

bool
Daw::rolling () const
{
	return _ui ? (_ui->transport_rolling () != 0.0) : false;
}

void
Daw::toggle_roll ()
{
	if (_ui) { _ui->toggle_roll (false, false); }
}

void
Daw::stop ()
{
	if (_ui) { _ui->transport_stop (); }
	else if (_session) { _session->request_transport_speed (0.0); }
}

void
Daw::goto_start ()
{
	if (_ui) { _ui->goto_start (false); }
}

void
Daw::goto_end ()
{
	if (_ui) { _ui->goto_end (); }
}

void
Daw::rewind ()
{
	if (_ui) { _ui->rewind (); }
}

void
Daw::ffwd ()
{
	if (_ui) { _ui->ffwd (); }
}

void
Daw::locate (long long sample)
{
	if (!_session) { return; }
	if (sample < 0) { sample = 0; }
	_session->request_locate ((samplepos_t) sample, false, RollIfAppropriate);
}

void
Daw::jump_bars (int bars)
{
	if (_ui) { _ui->jump_by_bars (bars, RollIfAppropriate); }
}

long long
Daw::position () const
{
	return _session ? (long long) _session->transport_sample () : 0;
}

double
Daw::speed () const
{
	return _ui ? _ui->get_transport_speed () : 0.0;
}

/* O Ardour já sabe qual é a região de loop da sessão (Location IsAutoLoop); o
 * botão LOOP do painel liga/desliga essa região. Sem nenhuma definida, criamos
 * uma de 4 compassos a partir do cursor — é o que uma DAW faz quando você aperta
 * loop sem seleção. */
void
Daw::toggle_loop ()
{
	set_loop (!loop_active ());
}

void
Daw::set_loop (bool on)
{
	if (!_session) { return; }
	Location* loop = _session->locations ()->auto_loop_location ();
	if (!on) {
		if (loop) {
			_session->set_auto_loop_location (0);
			_session->locations ()->remove (loop);
			log_line ("[LOOP] desligado");
		}
		return;
	}
	if (loop) { return; }

	/* região de loop = 4 compassos a partir do cursor */
	const samplepos_t sr = _session->sample_rate ();
	const samplepos_t start = _session->transport_sample ();
	double bpm = this->bpm ();
	if (bpm < 1.0) { bpm = 120.0; }
	const double secs = (60.0 / bpm) * (double) meter_beats () * 4.0; /* 4 compassos */
	const samplepos_t end = start + (samplepos_t) (secs * sr);
	Location* nova = new Location (*_session, timepos_t (start), timepos_t (end),
	                               "Loop", Location::IsAutoLoop);
	_session->locations ()->add (nova, true);
	_session->set_auto_loop_location (nova);
	log_line ("[LOOP] ligado (4 compassos a partir do cursor)");
}

bool
Daw::loop_active () const
{
	if (!_session) { return false; }
	return _session->locations ()->auto_loop_location () != 0;
}

bool
Daw::click_on () const
{
	return Config ? Config->get_clicking () : false;
}

void
Daw::toggle_click ()
{
	if (_ui) { _ui->toggle_click (); }
}

bool
Daw::record_enabled () const
{
	return _session ? _session->get_record_enabled () : false;
}

void
Daw::set_record_enabled (bool on)
{
	if (_ui) { _ui->set_record_enable (on); }
}

void
Daw::arm_all (bool on)
{
	if (_session) { _session->set_all_tracks_record_enabled (on); }
}

/* ------------------------------------------------------------------ TEMPO */

double
Daw::bpm () const
{
	if (!_session) { return 0.0; }
	try {
		TempoMap::SharedPtr map (TempoMap::use ());
		return map->tempo_at (timepos_t ((samplepos_t) 0)).quarter_notes_per_minute ();
	} catch (...) {
		return 0.0;
	}
}

bool
Daw::set_bpm (double bpm, std::string& err)
{
	if (!_session) { err = "nenhuma sessão aberta"; return false; }
	if (bpm < 1.0 || bpm > 999.0) { err = "BPM fora de 1..999"; return false; }
	try {
		TempoMap::WritableSharedPtr map = TempoMap::write_copy ();
		map->set_tempo (Tempo (bpm, 4), timepos_t ((samplepos_t) 0));
		TempoMap::update (map);
	} catch (std::exception& e) {
		TempoMap::abort_update ();
		err = std::string ("falha ao gravar o tempo: ") + e.what ();
		return false;
	}
	log_line ("[TEMPO] BPM -> " + std::to_string (bpm));
	return true;
}

int
Daw::meter_beats () const
{
	if (!_session) { return 4; }
	try {
		TempoMap::SharedPtr map (TempoMap::use ());
		return map->meter_at (timepos_t ((samplepos_t) 0)).divisions_per_bar ();
	} catch (...) {
		return 4;
	}
}

int
Daw::meter_div () const
{
	if (!_session) { return 4; }
	try {
		TempoMap::SharedPtr map (TempoMap::use ());
		return map->meter_at (timepos_t ((samplepos_t) 0)).note_value ();
	} catch (...) {
		return 4;
	}
}

std::string
Daw::pos_bars () const
{
	if (!_session) { return "001|01|000"; }
	try {
		TempoMap::SharedPtr map (TempoMap::use ());
		return bbt_text (map->bbt_at (timepos_t ((samplepos_t) position ())));
	} catch (...) {
		return "—";
	}
}

std::string
Daw::pos_time () const
{
	const double sr = _session ? _session->sample_rate () : 48000.0;
	long long s = position ();
	const long long total_ms = (long long) std::llround (1000.0 * (double) s / sr);
	const long long ms = total_ms % 1000;
	const long long total_s = total_ms / 1000;
	char buf[32];
	std::snprintf (buf, sizeof (buf), "%02lld:%02lld:%02lld.%03lld",
	               total_s / 3600, (total_s / 60) % 60, total_s % 60, ms);
	return buf;
}

std::string
Daw::pos_samples () const
{
	return std::to_string (position ());
}

/* --------------------------------------------------- MARCADORES / REGIÕES */

std::vector<MarkerInfo>
Daw::markers () const
{
	std::vector<MarkerInfo> out;
	if (!_session) { return out; }
	Locations::LocationList const& ls = _session->locations ()->list ();
	for (Locations::LocationList::const_iterator i = ls.begin (); i != ls.end (); ++i) {
		Location* l = *i;
		if (l->is_hidden ()) { continue; }
		MarkerInfo m;
		m.name  = l->name ();
		m.kind  = marker_kind (*l);
		m.start = (long long) l->start_sample ();
		m.end   = (long long) l->end_sample ();
		out.push_back (m);
	}
	std::sort (out.begin (), out.end (),
	           [] (MarkerInfo const& a, MarkerInfo const& b) { return a.start < b.start; });
	return out;
}

/* O BasicUI::add_marker já cria a Location + comando reversível (Ctrl+Z pega). */
void
Daw::add_marker (std::string const& name)
{
	if (_ui) {
		_ui->add_marker (name, Location::IsMark);
		log_line ("[MARCADOR] + " + (name.empty () ? std::string ("(sem nome)") : name));
	}
}

bool
Daw::add_arranger_section (std::string const& name, int bars, std::string& err)
{
	if (!_session) { err = "nenhuma sessão aberta"; return false; }
	if (bars < 1 || bars > 256) { err = "duração deve estar entre 1 e 256 compassos"; return false; }
	const samplepos_t sr = _session->sample_rate ();
	double b = bpm ();
	if (b < 1.0) { b = 120.0; }
	const double seconds = (60.0 / b) * (double) meter_beats () * (double) bars;
	const samplepos_t start = _session->transport_sample ();
	const samplepos_t end = start + (samplepos_t) std::llround (seconds * sr);
	const std::string titulo = name.empty () ? "Seção" : name;
	Location* sec = new Location (*_session, timepos_t (start), timepos_t (end), titulo,
	                             Location::Flags (Location::IsRangeMarker));
	_session->locations ()->add (sec, true);
	log_line ("[ARRANGER] + " + titulo + " (" + std::to_string (bars) + " compassos)");
	return true;
}

bool
Daw::add_midi_clip (int route_idx, std::string const& name, int bars, std::string& err)
{
	if (!_session) { err = "nenhuma sessão aberta"; return false; }
	if (bars < 1 || bars > 256) { err = "duração deve estar entre 1 e 256 compassos"; return false; }
	Route* route = route_at (route_idx);
	MidiTrack* track = dynamic_cast<MidiTrack*> (route);
	if (!track) { err = "selecione uma trilha MIDI para criar um clip"; return false; }

	const samplepos_t sr = _session->sample_rate ();
	double b = bpm (); if (b < 1.0) { b = 120.0; }
	const samplecnt_t len = (samplecnt_t) std::llround ((60.0 / b) * meter_beats () * bars * sr);
	const samplepos_t pos = _session->transport_sample ();
	const std::string titulo = name.empty () ? "MIDI clip" : name;
	try {
		std::shared_ptr<MidiSource> source = _session->create_midi_source_for_session (titulo);
		PBD::PropertyList props;
		props.add (ARDOUR::Properties::start, timepos_t ((samplepos_t) 0));
		props.add (ARDOUR::Properties::length, timecnt_t (len));
		props.add (ARDOUR::Properties::name, titulo);
		std::shared_ptr<Region> clip = RegionFactory::create (std::static_pointer_cast<Source> (source), props, true);
		if (!clip) { err = "falha ao construir a região MIDI"; return false; }
		track->playlist ()->add_region (clip, timepos_t (pos), 1.0, false);
		log_line ("[CLIP] + MIDI " + titulo + " (" + std::to_string (bars) + " compassos)");
		return true;
	} catch (std::exception const& e) {
		err = std::string ("falha criando fonte MIDI: ") + e.what ();
		return false;
	}
}

bool
Daw::find_clip (int wanted, std::shared_ptr<Playlist>& playlist, std::shared_ptr<Region>& region) const
{
	if (wanted < 0) { return false; }
	int n = 0;
	const std::vector<Route*> rs = ordered_routes ();
	for (size_t i = 0; i < rs.size (); ++i) {
		Track* track = dynamic_cast<Track*> (rs[i]);
		if (!track) { continue; }
		std::shared_ptr<Playlist> pl = track->playlist ();
		if (!pl) { continue; }
		bool found = false;
		pl->foreach_region ([&] (std::shared_ptr<Region> r) {
			if (found || !r) { return; }
			if (n++ == wanted) { playlist = pl; region = r; found = true; }
		});
		if (found) { return true; }
	}
	return false;
}

bool
Daw::import_audio_clip (int route_idx, std::string const& path, std::string& err)
{
	if (!_session) { err = "nenhuma sessão aberta"; return false; }
	if (path.empty ()) { err = "escolha um arquivo de áudio"; return false; }
	Route* route = route_at (route_idx);
	AudioTrack* track = dynamic_cast<AudioTrack*> (route);
	if (!track) { err = "selecione uma trilha de áudio para importar"; return false; }
	SoundFileInfo info; std::string source_err;
	if (!AudioFileSource::get_soundfile_info (path, info, source_err) || !info.seekable || info.channels < 1 || info.length < 1) {
		err = "não foi possível ler o áudio: " + source_err; return false;
	}
	try {
		std::shared_ptr<Source> source = SourceFactory::createExternal (DataType::AUDIO, *_session, path, 0, Source::Flag (0), true, false);
		if (!source) { err = "o Ardour não abriu a fonte de áudio"; return false; }
		PBD::PropertyList props;
		props.add (ARDOUR::Properties::start, timepos_t ((samplepos_t) 0));
		props.add (ARDOUR::Properties::length, source->length ());
		const std::string::size_type slash = path.find_last_of ("/");
		std::string clip_name = slash == std::string::npos ? path : path.substr (slash + 1);
		const std::string::size_type dot = clip_name.find_last_of ('.');
		if (dot != std::string::npos) { clip_name.erase (dot); }
		props.add (ARDOUR::Properties::name, clip_name);
		props.add (ARDOUR::Properties::layer, 0);
		props.add (ARDOUR::Properties::whole_file, true);
		props.add (ARDOUR::Properties::external, true);
		props.add (ARDOUR::Properties::opaque, true);
		std::shared_ptr<Region> clip = RegionFactory::create (source, props, true);
		if (!clip) { err = "falha ao construir região de áudio"; return false; }
		track->playlist ()->add_region (clip, timepos_t (_session->transport_sample ()), 1.0, false);
		log_line ("[CLIP] + ÁUDIO " + path);
		return true;
	} catch (std::exception const& e) {
		err = std::string ("falha importando áudio: ") + e.what (); return false;
	}
}

std::vector<float>
Daw::audio_waveform (int clip_idx, int columns, std::string& err) const
{
	std::vector<float> peaks;
	if (columns < 1) { return peaks; }
	std::shared_ptr<Playlist> pl; std::shared_ptr<Region> region;
	if (!find_clip (clip_idx, pl, region)) { err = "clip inexistente"; return peaks; }
	std::shared_ptr<AudioRegion> audio = std::dynamic_pointer_cast<AudioRegion> (region);
	if (!audio || !audio->audio_source ()) { err = "o clip selecionado não é áudio"; return peaks; }
	const samplecnt_t total = audio->length_samples ();
	if (total < 1) { return peaks; }
	const samplecnt_t step = std::max<samplecnt_t> (1, total / columns);
	const samplecnt_t block = std::min<samplecnt_t> (128, step);
	std::vector<Sample> buf ((size_t) block);
	peaks.resize ((size_t) columns, 0.0f);
	for (int col = 0; col < columns; ++col) {
		const samplepos_t offset = std::min<samplepos_t> ((samplepos_t) col * step, total - 1);
		const samplecnt_t count = std::min<samplecnt_t> (block, total - offset);
		if (audio->audio_source ()->read (&buf[0], offset, count) <= 0) { continue; }
		for (samplecnt_t i = 0; i < count; ++i) { peaks[(size_t) col] = std::max (peaks[(size_t) col], (float) std::fabs (buf[(size_t) i])); }
	}
	return peaks;
}

bool
Daw::move_clip (int idx, long long start, std::string& err)
{
	std::shared_ptr<Playlist> pl; std::shared_ptr<Region> r;
	if (!find_clip (idx, pl, r)) { err = "clip inexistente"; return false; }
	if (start < 0) { start = 0; }
	r->set_position (timepos_t ((samplepos_t) start));
	return true;
}

bool
Daw::resize_clip (int idx, long long end, std::string& err)
{
	std::shared_ptr<Playlist> pl; std::shared_ptr<Region> r;
	if (!find_clip (idx, pl, r)) { err = "clip inexistente"; return false; }
	const long long begin = (long long) r->position_sample ();
	if (end <= begin) { err = "o clip precisa ter duração positiva"; return false; }
	r->set_length (timecnt_t ((samplecnt_t) (end - begin)));
	return true;
}

bool
Daw::duplicate_clip (int idx, std::string& err)
{
	std::shared_ptr<Playlist> pl; std::shared_ptr<Region> r;
	if (!find_clip (idx, pl, r)) { err = "clip inexistente"; return false; }
	std::shared_ptr<Region> copy = RegionFactory::create (r, true);
	if (!copy) { err = "não foi possível duplicar o clip"; return false; }
	const samplepos_t to = r->position_sample () + r->length_samples ();
	pl->add_region (copy, timepos_t (to), 1.0, false);
	return true;
}

bool
Daw::remove_clip (int idx, std::string& err)
{
	std::shared_ptr<Playlist> pl; std::shared_ptr<Region> r;
	if (!find_clip (idx, pl, r)) { err = "clip inexistente"; return false; }
	pl->remove_region (r);
	return true;
}

std::vector<ClipInfo>
Daw::clips () const
{
	std::vector<ClipInfo> out;
	const std::vector<Route*> rs = ordered_routes ();
	for (size_t i = 0; i < rs.size (); ++i) {
		Track* track = dynamic_cast<Track*> (rs[i]);
		if (!track) { continue; }
		std::shared_ptr<Playlist> pl = track->playlist ();
		if (!pl) { continue; }
		pl->foreach_region ([&out, i] (std::shared_ptr<Region> region) {
			if (!region) { return; }
			ClipInfo c;
			c.route_idx = (int) i;
			c.name = region->name ();
			c.is_midi = region->data_type () == DataType::MIDI;
			c.start = (long long) region->position_sample ();
			c.end = c.start + (long long) region->length_samples ();
			out.push_back (c);
		});
	}
	return out;
}

std::vector<MidiNoteInfo>
Daw::midi_notes (int clip_idx, std::string& err) const
{
	std::vector<MidiNoteInfo> out;
	std::shared_ptr<Playlist> pl; std::shared_ptr<Region> region;
	if (!find_clip (clip_idx, pl, region)) { err = "clip inexistente"; return out; }
	std::shared_ptr<MidiRegion> midi = std::dynamic_pointer_cast<MidiRegion> (region);
	if (!midi) { err = "o clip selecionado não é MIDI"; return out; }
	std::shared_ptr<const MidiModel> model = midi->model ();
	if (!model) { err = "modelo MIDI indisponível"; return out; }
	MidiModel::ReadLock lock = model->read_lock ();
	for (MidiModel::Notes::const_iterator it = model->notes ().begin (); it != model->notes ().end (); ++it) {
		if (!*it) { continue; }
		MidiNoteInfo n;
		n.clip_idx = clip_idx;
		n.id = (unsigned long long) (*it)->id ();
		n.pitch = (*it)->note ();
		n.velocity = (*it)->velocity ();
		n.start_beats = (double) (*it)->time ().to_ticks () / Temporal::Beats::PPQN;
		n.length_beats = (double) (*it)->length ().to_ticks () / Temporal::Beats::PPQN;
		out.push_back (n);
	}
	return out;
}

bool
Daw::add_midi_note (int clip_idx, int pitch, double start_beats, double length_beats, int velocity, std::string& err)
{
	std::shared_ptr<Playlist> pl; std::shared_ptr<Region> region;
	if (!find_clip (clip_idx, pl, region)) { err = "clip inexistente"; return false; }
	std::shared_ptr<MidiRegion> midi = std::dynamic_pointer_cast<MidiRegion> (region);
	if (!midi) { err = "o clip selecionado não é MIDI"; return false; }
	if (pitch < 0 || pitch > 127) { err = "nota MIDI deve estar entre 0 e 127"; return false; }
	if (start_beats < 0.0) { err = "posição da nota inválida"; return false; }
	if (length_beats <= 0.0) { err = "a nota precisa ter duração positiva"; return false; }
	velocity = std::max (1, std::min (127, velocity));
	std::shared_ptr<MidiModel> model = midi->model ();
	if (!model) { err = "modelo MIDI indisponível"; return false; }
	std::shared_ptr<Evoral::Note<Temporal::Beats> > note (new Evoral::Note<Temporal::Beats> (0,
		Temporal::Beats::from_double (start_beats), Temporal::Beats::from_double (length_beats),
		(uint8_t) pitch, (uint8_t) velocity));
	MidiModel::NoteDiffCommand* cmd = model->new_note_diff_command ("SPOOK piano-roll: adicionar nota");
	cmd->add (note);
	model->apply_diff_command_as_commit (*_session, cmd);
	if (!model->find_note (note->id ())) { err = "nota recusada pela política de sobreposição"; return false; }
	return true;
}

bool
Daw::move_midi_note (int clip_idx, unsigned long long id, double start_beats, std::string& err)
{
	std::shared_ptr<Playlist> pl; std::shared_ptr<Region> region;
	if (!find_clip (clip_idx, pl, region)) { err = "clip inexistente"; return false; }
	std::shared_ptr<MidiRegion> midi = std::dynamic_pointer_cast<MidiRegion> (region);
	if (!midi) { err = "o clip selecionado não é MIDI"; return false; }
	if (start_beats < 0.0) { err = "posição da nota inválida"; return false; }
	std::shared_ptr<MidiModel> model = midi->model ();
	if (!model) { err = "modelo MIDI indisponível"; return false; }
	std::shared_ptr<Evoral::Note<Temporal::Beats> > note = model->find_note ((Evoral::event_id_t) id);
	if (!note) { err = "nota inexistente"; return false; }
	MidiModel::NoteDiffCommand* cmd = model->new_note_diff_command ("SPOOK piano-roll: mover nota");
	cmd->change (note, MidiModel::NoteDiffCommand::StartTime, Temporal::Beats::from_double (start_beats));
	model->apply_diff_command_as_commit (*_session, cmd);
	return true;
}

bool
Daw::resize_midi_note (int clip_idx, unsigned long long id, double length_beats, std::string& err)
{
	std::shared_ptr<Playlist> pl; std::shared_ptr<Region> region;
	if (!find_clip (clip_idx, pl, region)) { err = "clip inexistente"; return false; }
	std::shared_ptr<MidiRegion> midi = std::dynamic_pointer_cast<MidiRegion> (region);
	if (!midi) { err = "o clip selecionado não é MIDI"; return false; }
	if (length_beats <= 0.0) { err = "a nota precisa ter duração positiva"; return false; }
	std::shared_ptr<MidiModel> model = midi->model ();
	if (!model) { err = "modelo MIDI indisponível"; return false; }
	std::shared_ptr<Evoral::Note<Temporal::Beats> > note = model->find_note ((Evoral::event_id_t) id);
	if (!note) { err = "nota inexistente"; return false; }
	MidiModel::NoteDiffCommand* cmd = model->new_note_diff_command ("SPOOK piano-roll: redimensionar nota");
	cmd->change (note, MidiModel::NoteDiffCommand::Length, Temporal::Beats::from_double (length_beats));
	model->apply_diff_command_as_commit (*_session, cmd);
	return true;
}

bool
Daw::remove_midi_note (int clip_idx, unsigned long long id, std::string& err)
{
	std::shared_ptr<Playlist> pl; std::shared_ptr<Region> region;
	if (!find_clip (clip_idx, pl, region)) { err = "clip inexistente"; return false; }
	std::shared_ptr<MidiRegion> midi = std::dynamic_pointer_cast<MidiRegion> (region);
	if (!midi) { err = "o clip selecionado não é MIDI"; return false; }
	std::shared_ptr<MidiModel> model = midi->model ();
	if (!model) { err = "modelo MIDI indisponível"; return false; }
	std::shared_ptr<Evoral::Note<Temporal::Beats> > note = model->find_note ((Evoral::event_id_t) id);
	if (!note) { err = "nota inexistente"; return false; }
	MidiModel::NoteDiffCommand* cmd = model->new_note_diff_command ("SPOOK piano-roll: apagar nota");
	cmd->remove (note);
	model->apply_diff_command_as_commit (*_session, cmd);
	return true;
}

void
Daw::remove_marker_at_playhead ()
{
	if (!_session) { return; }
	const samplepos_t pos = _session->transport_sample ();
	const double janela = (double) _session->sample_rate () * 0.1; /* 100 ms */
	Location* alvo = 0;
	Locations::LocationList const& ls = _session->locations ()->list ();
	for (Locations::LocationList::const_iterator i = ls.begin (); i != ls.end (); ++i) {
		Location* l = *i;
		if (!l->is_mark () || l->is_hidden ()) { continue; }
		if (std::llabs ((long long) l->start_sample () - (long long) pos) <= (long long) janela) {
			alvo = l;
			break;
		}
	}
	if (!alvo) { log_line ("[MARCADOR] nenhum marcador no cursor"); return; }
	log_line ("[MARCADOR] - " + alvo->name ());
	_session->locations ()->remove (alvo);
}

void
Daw::next_marker ()
{
	if (_ui) { _ui->next_marker (); }
}

void
Daw::prev_marker ()
{
	if (_ui) { _ui->prev_marker (); }
}

/* ------------------------------------------------------------------ EDIÇÃO */

void
Daw::undo ()
{
	if (_ui) { _ui->undo (); }
}

void
Daw::redo ()
{
	if (_ui) { _ui->redo (); }
}

bool
Daw::save (std::string& err)
{
	if (!_session) { err = "nenhuma sessão aberta"; return false; }
	int r = _session->save_state ("");
	if (r != 0) { err = "save_state devolveu erro"; return false; }
	log_line ("[SESSÃO] estado salvo");
	return true;
}

void
Daw::midi_panic ()
{
	if (_ui) {
		_ui->midi_panic ();
		log_line ("[PÂNICO] todas as notas MIDI cortadas");
	}
}

/* ------------------------------------------------------------------- MIXER */

Route*
Daw::route_at (int idx) const
{
	const std::vector<Route*> rs = ordered_routes ();
	if (idx < 0 || (size_t) idx >= rs.size ()) { return 0; }
	return rs[(size_t) idx];
}

/* Ordem ÚNICA das rotas: as de get_routes() e, se o Ardour não incluir o
 * master lá, ele entra no FIM. route_at()/strips()/route_names() usam essa
 * mesma ordem — senão o índice do fader apontaria para outra rota. */
std::vector<Route*>
Daw::ordered_routes () const
{
	std::vector<Route*> out;
	if (!_session) { return out; }

	std::shared_ptr<RouteList const> routes = _session->get_routes ();
	bool tem_master = false;
	for (RouteList::const_iterator i = routes->begin (); i != routes->end (); ++i) {
		out.push_back (i->get ());
		if ((*i)->is_master ()) { tem_master = true; }
	}

	if (!tem_master) {
		std::shared_ptr<Route> m = _session->master_out ();
		if (m) { out.push_back (m.get ()); }
	}
	return out;
}

std::vector<std::string>
Daw::route_names () const
{
	std::vector<std::string> out;
	const std::vector<Route*> rs = ordered_routes ();
	for (size_t i = 0; i < rs.size (); ++i) { out.push_back (rs[i]->name ()); }
	return out;
}

std::vector<RouteStrip>
Daw::strips () const
{
	std::vector<RouteStrip> out;
	if (!_session) { return out; }

	const std::vector<Route*> rs = ordered_routes ();
	for (size_t k = 0; k < rs.size (); ++k) {
		Route* r = rs[k];
		RouteStrip s;
		s.name      = r->name ();
		s.is_master = r->is_master ();
		s.is_track  = r->is_track ();
		s.is_midi   = (r->presentation_info ().flags () & PresentationInfo::MidiTrack) != 0;
		s.color     = r->presentation_info ().color ();
		s.muted     = r->mute_control () ? (r->mute_control ()->get_value () > 0.5) : false;
		if (r->solo_control ()) { s.soloed = r->solo_control ()->get_value () > 0.5; }
		/* gain_control()->get_value() é COEFICIENTE (1.0 = unidade), não dB:
		 * converter, senão o fader de 0 dB aparece como +1 dB. */
		if (r->gain_control ()) { s.gain_db = (double) accurate_coefficient_to_dB ((float) r->gain_control ()->get_value ()); }

		if (r->pannable () && r->pannable ()->pan_azimuth_control) {
			std::shared_ptr<AutomationControl> pa = r->pannable ()->pan_azimuth_control;
			s.pan     = pa->get_value ();
			s.pan_min = pa->lower ();
			s.pan_max = pa->upper ();
			if (s.pan_max <= s.pan_min) { s.pan_min = -1.0; s.pan_max = 1.0; }
		}
		if (Track* t = dynamic_cast<Track*> (r)) {
			if (t->rec_enable_control ()) { s.armed = t->rec_enable_control ()->get_value () > 0.5; }
		}
		if (r->peak_meter ()) {
			s.nchan = std::max (1u, r->peak_meter ()->input_streams ().n_total ());
			s.peak[0] = r->peak_meter ()->meter_level (0, MeterPeak);
			s.peak[1] = (s.nchan > 1) ? r->peak_meter ()->meter_level (1, MeterPeak) : s.peak[0];
		}
		out.push_back (s);
	}
	return out;
}

std::string
Daw::master_name () const
{
	if (!_session) { return ""; }
	std::shared_ptr<Route> m = _session->master_out ();
	return m ? m->name () : std::string ();
}

/* O Ardour 9 NÃO cria o bus master ao criar a sessão: quem faz isso é o GUI
 * (Session::add_master_bus). Sem ele não há soma, não há canal master no mixer
 * e `master_out()` devolve nulo. Idempotente: se já existe, não faz nada. */
bool
Daw::ensure_master_bus ()
{
	if (!_session) { return false; }
	if (_session->master_out ()) { return true; }

	const int r = _session->add_master_bus (ARDOUR::ChanCount (ARDOUR::DataType::AUDIO, 2));
	if (r != 0) {
		log_line ("[MIXER] falhou criar o bus master (rc=" + std::to_string (r) + ")");
		return false;
	}
	log_line ("[MIXER] bus master criado (2 canais)");
	return true;
}

bool
Daw::set_gain (int idx, double db, std::string& err)
{
	Route* r = route_at (idx);
	if (!r) { err = "rota inexistente"; return false; }
	if (!r->gain_control ()) { err = "rota sem controle de ganho"; return false; }
	/* `db` chega em dB; o controle trabalha em COEFICIENTE. */
	r->gain_control ()->set_value (dB_to_coefficient ((float) db), PBD::Controllable::NoGroup);
	return true;
}

bool
Daw::set_pan (int idx, double pan, std::string& err)
{
	Route* r = route_at (idx);
	if (!r) { err = "rota inexistente"; return false; }
	if (!r->pannable () || !r->pannable ()->pan_azimuth_control) {
		err = "rota sem controle de pan (mono?)";
		return false;
	}
	r->pannable ()->pan_azimuth_control->set_value (pan, PBD::Controllable::NoGroup);
	return true;
}

bool
Daw::set_mute (int idx, bool on, std::string& err)
{
	Route* r = route_at (idx);
	if (!r) { err = "rota inexistente"; return false; }
	if (!r->mute_control ()) { err = "rota sem mute"; return false; }
	r->mute_control ()->set_value (on ? 1.0 : 0.0, PBD::Controllable::NoGroup);
	return true;
}

bool
Daw::set_solo (int idx, bool on, std::string& err)
{
	Route* r = route_at (idx);
	if (!r) { err = "rota inexistente"; return false; }
	if (!r->solo_control ()) { err = "rota sem solo"; return false; }
	r->solo_control ()->set_value (on ? 1.0 : 0.0, PBD::Controllable::NoGroup);
	return true;
}

bool
Daw::set_arm (int idx, bool on, std::string& err)
{
	Route* r = route_at (idx);
	if (!r) { err = "rota inexistente"; return false; }
	Track* t = dynamic_cast<Track*> (r);
	if (!t || !t->rec_enable_control ()) { err = "rota não é trilha (sem arm)"; return false; }
	t->rec_enable_control ()->set_value (on ? 1.0 : 0.0, PBD::Controllable::NoGroup);
	return true;
}

bool
Daw::rename_route (int idx, std::string const& name, std::string& err)
{
	Route* r = route_at (idx);
	if (!r) { err = "rota inexistente"; return false; }
	if (name.empty ()) { err = "o nome da trilha não pode ficar vazio"; return false; }
	const std::string antes = r->name ();
	/* Track::set_name também mantém coerente o nome de take/diskstream e
	 * recusa renomear uma faixa armada — regras importantes do Ardour. */
	Track* t = dynamic_cast<Track*> (r);
	const bool ok = t ? t->set_name (name) : r->set_name (name);
	if (!ok) {
		err = "o Ardour recusou o nome (trilha armada, nome inválido ou porta em uso)";
		return false;
	}
	log_line ("[SESSÃO] rota renomeada: " + antes + " -> " + r->name ());
	return true;
}

bool
Daw::set_route_color (int idx, unsigned int rgba, std::string& err)
{
	Route* r = route_at (idx);
	if (!r) { err = "rota inexistente"; return false; }
	r->presentation_info ().set_color ((PresentationInfo::color_t) rgba);
	return true;
}

void
Daw::clear_solo ()
{
	if (_ui) { _ui->cancel_all_solo (); }
}

/* ------------------------------------------------------------------ SESSÃO */

std::vector<std::string>
Daw::new_audio_track (std::string const& name, unsigned int chans)
{
	std::vector<std::string> out;
	if (!_session) { return out; }
	AudioTrackList tl = _session->new_audio_track ((int) chans, (int) chans,
	                                               std::shared_ptr<RouteGroup> (), 1, name,
	                                               PresentationInfo::max_order);
	for (AudioTrackList::iterator i = tl.begin (); i != tl.end (); ++i) {
		out.push_back ((*i)->name ());
	}
	return out;
}

std::vector<std::string>
Daw::new_midi_track (std::string const& name)
{
	std::vector<std::string> out;
	if (!_session) { return out; }
	ChanCount in (DataType::MIDI, 1);
	ChanCount out_ch (DataType::AUDIO, 2);
	std::list<std::shared_ptr<MidiTrack> > tl = _session->new_midi_track (
	    in, out_ch, false, std::shared_ptr<PluginInfo> (), 0,
	    std::shared_ptr<RouteGroup> (), 1, name, PresentationInfo::max_order,
	    TrackMode::Normal, true, false);
	for (std::list<std::shared_ptr<MidiTrack> >::iterator i = tl.begin (); i != tl.end (); ++i) {
		out.push_back ((*i)->name ());
	}
	return out;
}

bool
Daw::duplicate_route (int idx, std::string& err)
{
	Route* src = route_at (idx);
	if (!src || src->is_master () || src->is_monitor ()) {
		err = "selecione uma trilha ou bus para duplicar";
		return false;
	}

	const std::vector<RouteStrip> antes = strips ();
	if ((size_t) idx >= antes.size ()) { err = "rota inexistente"; return false; }
	const RouteStrip modelo = antes[(size_t) idx];
	const std::string novo_nome = src->name () + " copia";
	std::vector<std::string> criadas;
	if (modelo.is_midi) { criadas = new_midi_track (novo_nome); }
	else                { criadas = new_audio_track (novo_nome, 2); }
	if (criadas.empty ()) { err = "o Ardour não conseguiu criar a cópia"; return false; }

	/* A sessão pode ajustar o nome para evitar colisão. Localizamos a rota pelo
	 * nome que ela de fato devolveu e aplicamos o estado de mix do original. */
	const std::vector<Route*> rs = ordered_routes ();
	int novo_idx = -1;
	for (size_t i = 0; i < rs.size (); ++i) {
		if (rs[i]->name () == criadas[0]) { novo_idx = (int) i; break; }
	}
	if (novo_idx < 0) { err = "cópia criada mas não foi localizada"; return false; }
	std::string ign;
	set_gain (novo_idx, modelo.gain_db, ign);
	set_pan  (novo_idx, modelo.pan,     ign);
	set_mute (novo_idx, modelo.muted,   ign);
	set_solo (novo_idx, modelo.soloed,  ign);
	set_route_color (novo_idx, modelo.color, ign);
	log_line ("[SESSÃO] trilha duplicada: " + src->name () + " -> " + criadas[0]);
	return true;
}

bool
Daw::remove_route (int idx, std::string& err)
{
	Route* r = route_at (idx);
	if (!r) { err = "rota inexistente"; return false; }
	if (r->is_master () || r->is_monitor ()) { err = "não dá para apagar o master/monitor"; return false; }
	/* a lista guarda shared_ptr: pega o dono antes de mandar remover */
	std::shared_ptr<Route> alvo;
	std::shared_ptr<RouteList const> routes = _session->get_routes ();
	int n = 0;
	for (RouteList::const_iterator i = routes->begin (); i != routes->end (); ++i, ++n) {
		if (n == idx) { alvo = *i; break; }
	}
	if (!alvo) { err = "rota inexistente"; return false; }
	_session->remove_route (alvo);
	log_line ("[SESSÃO] rota removida: " + r->name ());
	return true;
}

} // namespace spook
