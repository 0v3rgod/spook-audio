/*
 * SPOOK AUDIO — janela principal (gtkmm-3.0)
 *
 * Visual: DAW de estúdio. O que dá a "cara de programa de gravação":
 *   · barra de transporte com botões redondos + LCD (barras|batidas, tempo, BPM)
 *   · TOOLBAR de ações no estilo Reaper (trilha, marcador, loop, click, undo…)
 *   · MIXER com tiras de canal (fader, pan, M/S/arm, medidor) + master
 *   · LISTA DE AÇÕES pesquisável (a "action list" do Reaper)
 *   · abas estilo console, marcadores/regiões, medidor de CPU, barra de status
 *
 * Abas: MIXER · SESSÃO · AÇÕES · ENGINE · SCARLETT · PLUGINS · LOG
 *
 * POR QUE GTK3 E NAO GTK4 (medido, nao chute)
 *   O libardour instalado foi construido contra glibmm-2.4 + sigc++-2.0.
 *   O gtkmm-4.0 usa glibmm-2.68 + sigc++-3.0. Misturar os dois no mesmo
 *   processo nao funciona:
 *     - os headers do Ardour nem COMPILAM com sigc++3 (pbd/crossthread.h:97 usa
 *       `sigc::slot<bool,Glib::IOCondition>`, sintaxe removida no sigc++3);
 *     - mesmo se compilasse, libsigc-2.0 e libsigc-3.0 exportam 79 nomes
 *       mangled iguais, e libglibmm-2.4/libglibmm-2.68 mais 1483 — o linker
 *       dinamico interpoe um sobre o outro (layouts diferentes) e quebra.
 *   gtkmm-3.0 usa glibmm-2.4/sigc++-2.0, a MESMA familia do libardour: um unico
 *   libsigc/libglibmm carregado, zero interposicao. Alem disso o proprio GUI do
 *   Ardour 9 e GTK3 (o pacote traz um GTK3 forkado como libytk/libydk, que so
 *   carrega quando se usa libgtkmm2ext/libcanvas — nos nao carregamos).
 *   Prova: /tmp/opencode/mixtest/mix-GTK3 (init -> Dummy -> sessao -> trilha ->
 *   get_routes -> teardown, RC=0) ao lado do mix-GTK4 que nem compila.
 *
 * Regra de convivência (AGENTS.md): NUNCA abrir janela de teste na sessão Wayland
 * do Taylor. Os testes headless forçam GDK_BACKEND=x11 + Xvfb (o GDK prefere
 * Wayland se WAYLAND_DISPLAY estiver setada).
 */
#pragma once

#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <glibmm/dispatcher.h>
#include <gtkmm.h>

#include "core/daw.h"
#include "core/engine.h"
#include "core/scarlett.h"

namespace spook {

/* Medidor VU desenhado com cairo (verde/amarelo/vermelho + pico). Definido no
 * fim de ui/main_window.cc — fica fora do header para não arrastar cairo para
 * quem só quer a janela. */
class VuMeter;

class MainWindow : public Gtk::Window {
  public:
	MainWindow ();
	~MainWindow () override;

	/* Diagnóstico headless (só usado por SPOOK_TAB / SPOOK_AUTO_SESSION).
	 * Nunca chamado no uso normal: nenhuma janela de teste na sessão do usuário. */
	void show_tab (int n);
	void debug_bootstrap_session ();
	void debug_key_test ();

  private:
	/* --- montagem --- */
	Gtk::Widget* build_header ();
	Gtk::Widget* build_toolbar ();
	Gtk::Widget* build_transport ();
	Gtk::Widget* build_mixer_tab ();
	Gtk::Widget* build_arrange_tab ();
	Gtk::Widget* build_session_tab ();
	Gtk::Widget* build_actions_tab ();
	Gtk::Widget* build_engine_tab ();
	Gtk::Widget* build_scarlett_tab ();
	Gtk::Widget* build_plugins_tab ();
	Gtk::Widget* build_log_tab ();
	Gtk::Widget* build_statusbar ();

	/* --- ações --- */
	void on_backend_changed ();
	void on_start ();
	void on_stop ();
	void on_session_open ();
	void on_session_close ();
	void on_track_new ();
	void on_track_new_midi ();
	void on_track_remove ();
	void on_arranger_add ();
	void on_arranger_midi_clip ();
	void on_arranger_clip_duplicate ();
	void on_arranger_clip_delete ();
	void on_arranger_import_audio ();
	void on_piano_roll_open ();
	void on_route_duplicate (int idx);
	void on_route_rename (int idx, Gtk::Entry* input);
	void on_route_color (int idx, Gdk::RGBA const& color);
	void on_scarlett_scan ();
	void on_scarlett_card_changed ();
	void on_plugins_cache ();
	void on_plugins_full ();
	void on_plugins_scanned ();

	/* --- transporte / DAW --- */
	void on_transport_play ();
	void on_transport_stop ();
	void on_transport_rec ();
	void on_transport_start ();
	void on_transport_end ();
	void on_rewind ();
	void on_ffwd ();
	void on_loop ();
	void on_click ();
	void on_marker_add ();
	void on_marker_remove ();
	void on_undo ();
	void on_redo ();
	void on_save ();
	void on_panic ();
	void on_bpm_apply ();
	bool on_key_press (GdkEventKey* event);

	/* --- cartões --- */
	void refresh_mixer ();
	void refresh_arrange ();
	void refresh_markers ();
	void refresh_transport ();
	void sync_daw ();            /* religa o Daw à sessão atual */
	void do_action (std::string const& id);

	/* --- loop --- */
	bool on_tick ();

	void on_switch_page (Gtk::Widget* page, guint page_num);
	void on_device_card_pick ();
	void refresh_device_cards ();
	void refresh_routes ();
	void refresh_ctl_list ();
	void drain_log ();
	void set_status (std::string const& s, bool error = false);
	std::string default_session_dir () const;
	void update_engine_visuals ();

	std::unique_ptr<Engine> _engine;
	std::unique_ptr<Daw>    _daw;

	/* --- LINHA DE TOPO (header + transporte) --- */
	Gtk::Label* _led_engine  = nullptr;   /* motor no ar            */
	Gtk::Label* _led_session = nullptr;   /* sessao aberta          */
	Gtk::Label* _led_mixer   = nullptr;   /* Scarlett/cartao aberto */
	Gtk::Label* _head_sr     = nullptr;   /* sample rate do header  */

	/* transporte (botões redondos) */
	Gtk::Button* _tn_motor = nullptr;     /* sobe/derruba o motor   */
	Gtk::Button* _tn_start = nullptr;     /* |◀  volta ao início    */
	Gtk::Button* _tn_rew   = nullptr;     /* ◀◀  rebobinar          */
	Gtk::Button* _tn_play  = nullptr;     /* ▶   play/stop          */
	Gtk::Button* _tn_stop  = nullptr;     /* ■   parar              */
	Gtk::Button* _tn_rec   = nullptr;     /* ●   gravar (arm)       */
	Gtk::Button* _tn_end   = nullptr;     /* ▶|  fim               */
	Gtk::Button* _tn_panic = nullptr;     /* pânico MIDI            */

	/* toolbar */
	Gtk::ToggleButton* _tb_loop  = nullptr;
	Gtk::ToggleButton* _tb_click = nullptr;

	/* LCD do transporte */
	Gtk::Label*  _lcd_bars    = nullptr;  /* 001|01|000            */
	Gtk::Label*  _lcd_time    = nullptr;  /* 00:00:00.000          */
	Gtk::Label*  _lcd_samples = nullptr;  /* amostras              */
	Gtk::SpinButton* _bpm_spin = nullptr;
	Gtk::Label*  _sig_label   = nullptr;  /* 4/4                   */
	Gtk::Label*  _tr_backend  = nullptr;
	Gtk::Label*  _tr_sr       = nullptr;
	Gtk::Label*  _tr_session  = nullptr;
	Gtk::Label*  _tr_state    = nullptr;  /* STOPPED/PLAYING/…     */
	Gtk::LevelBar* _cpu_bar   = nullptr;  /* medidor de CPU (real) */
	Gtk::Label*  _cpu_label   = nullptr;
	double _cpu_prev_total = 0.0;
	double _cpu_prev_idle  = 0.0;

	Gtk::Notebook* _nb = nullptr;

	/* ARRANGE: visão de música baseada em seções Location::IsRangeMarker.
	 * O canvas é de navegação/arranjo global; eventos de áudio/MIDI entram numa
	 * próxima etapa, quando import/gravação de regiões estiverem implementados. */
	Gtk::DrawingArea* _arrange_canvas = nullptr;
	Gtk::Label* _arrange_state = nullptr;
	Gtk::Entry* _arrange_name = nullptr;
	Gtk::SpinButton* _arrange_bars = nullptr;
	Gtk::ComboBoxText* _arrange_midi_track = nullptr;
	Gtk::ComboBoxText* _arrange_audio_track = nullptr;
	Gtk::Scale* _arrange_zoom = nullptr;
	int _arrange_selected_clip = -1; /* índice transitório em Daw::clips() */
	int _arrange_drag_clip = -1;
	bool _arrange_drag_resize = false;
	long long _arrange_drag_offset = 0;
	long long _arrange_playhead = -1;
	std::string _arrange_sig;

	/* Piano-roll: uma janela secundária só é criada por ação explícita. Ela usa
	 * o MidiModel da região selecionada, jamais uma cópia desenhada em memória. */
	Gtk::Window* _piano_window = nullptr;
	Gtk::DrawingArea* _piano_canvas = nullptr;
	Gtk::Label* _piano_state = nullptr;
	int _piano_clip = -1;
	int _piano_selected_note = -1;
	bool _piano_dragging = false;
	bool _piano_drag_resize = false;
	double _piano_drag_delta = 0.0;
	int _piano_velocity = 100;

	/* MIXER: uma tira por rota, atualizada a cada tick */
	struct StripUI {
		int   idx = -1;
		std::string name;
		Gtk::Box*   strip = nullptr;
		Gtk::Scale* fader = nullptr;
		Gtk::Scale* pan   = nullptr;
		Gtk::Label* db    = nullptr;
		Gtk::Label* panv  = nullptr;
		Gtk::Entry* name_entry = nullptr; /* Track Header: Enter renomeia */
		double pan_lo = -1.0;      /* alcance real do controle de pan desta rota */
		double pan_hi =  1.0;
		VuMeter*    meter[2] = { nullptr, nullptr };
		Gtk::ToggleButton* mute = nullptr;
		Gtk::ToggleButton* solo = nullptr;
		Gtk::ToggleButton* arm  = nullptr;
		double touched = 0.0;      /* g_get_monotonic_time() do último toque */
	};
	Gtk::Box* _mixer_box = nullptr;
	std::vector<StripUI> _strip_ui;
	std::string _mixer_sig;        /* assinatura das rotas (reconstrói quando muda) */
	Gtk::Label* _mixer_info = nullptr;
	Gtk::Label* _mixer_state = nullptr;
	bool _mixer_updating = false;  /* true enquanto NÓS escrevemos nos widgets */
	bool _bpm_guard = false;       /* true enquanto NÓS pomos o BPM no spin   */
	/* true enquanto NÓS pomos `active` num toggle (LOOP/CLICK). Sem isto,
	 * set_active() emite "toggled" -> on_loop() -> alterna -> set_active()…
	 * num laço infinito que estoura o pool do PBD (POOL OUT OF MEMORY). */
	bool _toggle_guard = false;

	/* SESSÃO */
	Gtk::Entry*    _sess_dir  = nullptr;
	Gtk::Entry*    _sess_name = nullptr;
	Gtk::Button*   _btn_sess_open  = nullptr;
	Gtk::Button*   _btn_sess_close = nullptr;
	Gtk::Entry*    _track_name = nullptr;
	Gtk::Entry*    _marker_name = nullptr;
	Gtk::Button*   _btn_track  = nullptr;
	Gtk::Button*   _btn_track_midi = nullptr;
	Gtk::Button*   _btn_track_rm   = nullptr;
	Gtk::Box*      _routes     = nullptr;
	int            _selected_route = -1; /* Track List / Inspector, não master */
	Gtk::Label*    _routes_count = nullptr;
	Gtk::Label*    _sess_state = nullptr;
	Gtk::Box*      _marker_box = nullptr;   /* lista de marcadores/regiões */
	Gtk::Label*    _marker_count = nullptr;

	/* AÇÕES (action list do Reaper) */
	Gtk::TreeView*  _actions_view = nullptr;
	Gtk::Entry*     _actions_filter = nullptr;
	Gtk::Label*     _actions_state = nullptr;
	Glib::RefPtr<Gtk::ListStore>  _actions_store;
	Glib::RefPtr<Gtk::TreeModelFilter> _actions_filter_model;
	std::vector<std::pair<std::string, std::string> > _actions; /* id, descrição */

	/* SCARLETT */
	Gtk::ComboBoxText*      _card_combo = nullptr;
	Gtk::Button*            _btn_scan   = nullptr;
	Gtk::Entry*             _ctl_filter = nullptr;
	Gtk::ToggleButton*      _write_sw   = nullptr;
	Gtk::Label*             _write_badge = nullptr;
	Gtk::Box*               _strip_box  = nullptr;   /* faders verticais */
	Gtk::Box*               _switch_box = nullptr;   /* enums/bools      */
	Gtk::Label*             _scarlett_state = nullptr;
	std::unique_ptr<AlsaMixer> _mixer;
	std::vector<CtlElem>    _ctls;
	bool                    _writing = false;         /* guarda contra escrever
	                                                     enquanto preenchemos   */

	/* ENGINE */
	Gtk::ComboBoxText* _backend_combo = nullptr;
	Gtk::Entry*        _device_entry  = nullptr;
	Gtk::ComboBoxText* _device_combo  = nullptr; /* cartões ALSA -> copia o
	                                                nome exato para o entry
	                                                (o backend ALSA casa o
	                                                nome bonito do cartão)   */
	Gtk::Button*       _btn_start     = nullptr;
	Gtk::Button*       _btn_stop2     = nullptr;
	Gtk::Label*        _engine_state  = nullptr;
	Gtk::Label*        _eng_led_init  = nullptr;
	Gtk::Label*        _eng_led_back  = nullptr;
	Gtk::Label*        _eng_led_run   = nullptr;
	Gtk::LevelBar*     _eng_cpu_bar   = nullptr;
	Gtk::Label*        _eng_cpu_val   = nullptr;
	Gtk::LevelBar*     _eng_mem_bar   = nullptr;
	Gtk::Label*        _eng_mem_val   = nullptr;
	Gtk::Label*        _eng_cards     = nullptr;

	/* PLUGINS */
	Gtk::Box*     _plugins    = nullptr;
	Gtk::Button*  _btn_pcache = nullptr;
	Gtk::Button*  _btn_pfull  = nullptr;
	Gtk::Label*   _plug_state = nullptr;
	/* a varredura completa demora dezenas de segundos: roda fora da thread da UI
	 * e o fim e sinalizado pelo Dispatcher (seguro entre threads). */
	Glib::Dispatcher _plugins_done;
	bool             _plugins_busy = false;
	std::thread      _scan_thread;

	/* LOG */
	Gtk::TextView* _log_view = nullptr;
	Glib::RefPtr<Gtk::TextBuffer> _log_buf;
	Gtk::ScrolledWindow* _log_scroll = nullptr;

	/* rodapé */
	Gtk::Label* _status  = nullptr;
	Gtk::Label* _status_version = nullptr;

	bool _tick_busy = false;
	long long _lcd_pos = -2;       /* última posição mostrada no LCD */
	sigc::connection _tick;
	/* O GTK emite "switch-page" ao DISPOR o notebook (teardown). Como os
	 * membros _engine/_daw morrem antes do ~Gtk::Widget, deixar esse sinal
	 * ligado faz o handler tocar num Engine já destruído (SEGV). O
	 * destrutor desconecta. */
	sigc::connection _sig_page;
};

} // namespace spook
