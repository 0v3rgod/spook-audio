/*
 * SPOOK AUDIO — camada DAW (o que dá "cara de programa de gravação")
 *
 * Aqui vive o que uma DAW de verdade faz e o painel da Fase 3 só fazia por OSC:
 *   · TRANSPORTE   play/stop/loop/rec/marcadores/undo/redo/save
 *   · TEMPO        BPM e fórmula de compasso + posição em barras|batidas|ticks
 *   · MIXER        uma "tira de canal" por rota: ganho, pan, M/S/arm, medidor
 *   · SESSÃO       criar/apagar trilhas (áudio e MIDI)
 *
 * COMO: sem reimplementar nada. O próprio Ardour separa a lógica de controle da
 * interface gráfica: a classe `BasicUI` (libardourcp) faz transporte, marcador e
 * undo chamando o Session — o OSC usa exatamente ela. Então instanciamos
 * `BasicUI(session)` e falamos com o Session direto para o resto. Nada de toolkit
 * aqui (regra do core/): quem desenha é a ui/.
 *
 * Regra de ABI: o build TEM de usar -DWAF_BUILD (ver ../PoC-ENGINE.md).
 */
#pragma once

#include <memory>
#include <string>
#include <vector>

namespace ARDOUR {
class Session;
class Route;
class Playlist;
class Region;
}
class BasicUI; // libardourcp (control protocol), global scope

namespace spook {

/* Uma tira de canal do mixer: tudo que a faixa precisa mostrar/comandar. */
struct RouteStrip {
	std::string  name;
	bool         is_track  = false;
	bool         is_master = false;
	bool         is_midi   = false;
	bool         armed     = false;
	bool         muted     = false;
	bool         soloed    = false;
	/* RGBA da PresentationInfo do Ardour (0xRRGGBBAA). A cor é metadado
	 * persistente da trilha, como na Track List do Studio One/Logic. */
	unsigned int color     = 0;
	double       gain_db   = 0.0;
	double       pan       = 0.0;
	double       pan_min   = -1.0;   /* alcance real do controle de pan da rota */
	double       pan_max   =  1.0;
	float        peak[2]   = { -1000.0f, -1000.0f }; // dB
	unsigned int nchan     = 2;
};

/* Um local da sessão: marcador (IsMark) ou região (IsRangeMarker). */
struct MarkerInfo {
	std::string name;
	std::string kind;   // MARCA | REGIÃO | LOOP | PUNCH | SESSÃO
	long long   start = 0;
	long long   end   = 0;
};

/* Evento material numa Playlist do Ardour. A Arrange View desenha somente
 * estes objetos reais — nunca blocos decorativos. */
struct ClipInfo {
	int         route_idx = -1;
	std::string name;
	bool        is_midi = false;
	long long   start = 0;
	long long   end = 0;
};

/* Uma nota do modelo MIDI real do Ardour. Tempo e duração são beats na fonte
 * MIDI do clip, que é exatamente o domínio usado pelo piano-roll. */
struct MidiNoteInfo {
	int                clip_idx = -1;
	unsigned long long id = 0;
	int                pitch = 60;
	int                velocity = 100;
	double             start_beats = 0.0;
	double             length_beats = 1.0;
};

class Daw {
  public:
	Daw ();
	~Daw ();

	/* o Session tem dono (core/engine.cc); aqui só apontamos. null = desliga. */
	void attach (ARDOUR::Session* s);
	bool attached () const { return _session != 0; }

	/* ---------------------------------------------------------- TRANSPORTE */
	bool      rolling () const;
	void      toggle_roll ();   /* o "espaço" do Ardour: play/stop inteligente */
	void      stop ();
	void      goto_start ();
	void      goto_end ();
	void      rewind ();
	void      ffwd ();
	void      locate (long long sample);
	void      jump_bars (int bars);
	long long position () const;
	double    speed () const;

	void toggle_loop ();
	bool loop_active () const;
	void set_loop (bool on);

	bool click_on () const;
	void toggle_click ();

	bool record_enabled () const;
	void set_record_enabled (bool);
	void arm_all (bool);

	/* --------------------------------------------------------------- TEMPO */
	double bpm () const;
	bool   set_bpm (double bpm, std::string& err);
	int    meter_beats () const;
	int    meter_div () const;

	/* posição formatada (readouts da barra de transporte) */
	std::string pos_bars () const;     /* 001|01|000 */
	std::string pos_time () const;     /* 00:00:00.000 */
	std::string pos_samples () const;

	/* -------------------------------------------------- MARCADORES/REGIÕES */
	std::vector<MarkerInfo> markers () const;
	void add_marker (std::string const& name);
	/* Seção global do Arranger (Studio One): uma Location::IsRangeMarker
	 * persistente, começando no cursor e com duração em compassos. */
	bool add_arranger_section (std::string const& name, int bars, std::string& err);
	/* Cria uma região MIDI vazia numa trilha MIDI real. É a base para os
	 * clips clicáveis/arrastáveis; áudio importado vem em etapa posterior. */
	bool add_midi_clip (int route_idx, std::string const& name, int bars, std::string& err);
	bool move_clip (int clip_idx, long long start, std::string& err);
	bool resize_clip (int clip_idx, long long end, std::string& err);
	bool duplicate_clip (int clip_idx, std::string& err);
	bool remove_clip (int clip_idx, std::string& err);
	std::vector<ClipInfo> clips () const;
	/* Edição de notas passa pelo MidiModel/NoteDiffCommand do Ardour, entrando
	 * no histórico da sessão (Ctrl+Z), em vez de escrever bytes MIDI na mão. */
	std::vector<MidiNoteInfo> midi_notes (int clip_idx, std::string& err) const;
	bool add_midi_note (int clip_idx, int pitch, double start_beats,
	                    double length_beats, int velocity, std::string& err);
	bool move_midi_note (int clip_idx, unsigned long long id, double start_beats, std::string& err);
	bool resize_midi_note (int clip_idx, unsigned long long id, double length_beats, std::string& err);
	bool remove_midi_note (int clip_idx, unsigned long long id, std::string& err);
	/* Importação segura: abre o arquivo como fonte externa e cria uma Region
	 * real na Playlist. Não arma entrada nem inicia o motor de áudio. */
	bool import_audio_clip (int route_idx, std::string const& path, std::string& err);
	/* Picos para a mini-waveform do canvas; apenas leitura da AudioSource. */
	std::vector<float> audio_waveform (int clip_idx, int columns, std::string& err) const;
	void remove_marker_at_playhead ();
	void next_marker ();
	void prev_marker ();

	/* --------------------------------------------------------------- EDIÇÃO */
	void undo ();
	void redo ();
	bool save (std::string& err);
	void midi_panic ();

	/* ---------------------------------------------------------------- MIXER */
	std::vector<RouteStrip> strips () const;
	std::string master_name () const;
	/* cria o bus master se ainda não existir (o Ardour não o cria sozinho) */
	bool ensure_master_bus ();
	bool set_gain  (int idx, double db, std::string& err);
	bool set_pan   (int idx, double pan, std::string& err);
	bool set_mute  (int idx, bool on, std::string& err);
	bool set_solo  (int idx, bool on, std::string& err);
	bool set_arm   (int idx, bool on, std::string& err);
	/* Renomeia a Route real: o core também renomeia as portas I/O e valida
	 * colisões. Não é um apelido só da interface. */
	bool rename_route (int idx, std::string const& name, std::string& err);
	bool set_route_color (int idx, unsigned int rgba, std::string& err);
	void clear_solo ();

	/* -------------------------------------------------------------- SESSÃO */
	std::vector<std::string> new_audio_track (std::string const& name, unsigned int chans);
	std::vector<std::string> new_midi_track (std::string const& name);
	/* Duplica as propriedades de mix da trilha selecionada (nome, tipo,
	 * canais, ganho/pan/M/S e cor); conteúdo/áudio não é clonado ainda. */
	bool duplicate_route (int idx, std::string& err);
	bool remove_route (int idx, std::string& err);
	/* nomes das rotas, na ordem do mixer (mesma ordem de strips()) */
	std::vector<std::string> route_names () const;

  private:
	std::vector<ARDOUR::Route*> ordered_routes () const;
	ARDOUR::Route* route_at (int idx) const;
	bool find_clip (int idx, std::shared_ptr<ARDOUR::Playlist>& playlist,
	                std::shared_ptr<ARDOUR::Region>& region) const;

	ARDOUR::Session*         _session;
	std::unique_ptr<BasicUI> _ui;
};

} // namespace spook
