/*
 * SPOOK AUDIO — binario unico (Fase 4)
 *
 * Um UNICO processo que fala direto com o core do Ardour (libardour), mais o
 * mixer de hardware via ALSA snd_ctl. NAO executa `ardour9` nem `carla`.
 *
 * Modos:
 *   spook-audio                     abre a janela (padrao)
 *   spook-audio --backends          lista os backends do Ardour e sai
 *   spook-audio --self-test         teste de fumaca SEM GUI (Dummy, sem som)
 *   spook-audio --version           versao e revisao do core
 *   spook-audio --help              isto
 *
 * Autonomia: o runtime do Ardour vive EMBUTIDO em vendor/ardour (ao lado do
 * binario). O app define sozinho ARDOUR_DLL_PATH / ARDOUR_DATA_PATH /
 * ARDOUR_CONFIG_PATH / VAMP_PATH (se nao vierem do ambiente) e NAO depende
 * de /usr/lib/ardour9 nem do pacote `ardour` instalado. O script ./app.sh
 * exporta os mesmos caminhos explicitamente.
 *
 * UI: gtkmm-3.0 — NAO gtkmm-4.0. O libardour e glibmm-2.4/sigc++-2.0 e as duas
 * familias de ABI nao convivem no mesmo processo (ver ui/main_window.h).
 *
 * Para teste sem a sessao grafica do usuario: SPOOK_NON_UNIQUE=1 + Broadway/Xvfb.
 */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include <unistd.h>

#include <glibmm.h>
#include <gtkmm.h>
#include <gdk/gdk.h>

#include "core/daw.h"
#include "core/engine.h"
#include "core/log.h"
#include "ui/main_window.h"

#include "ardour/revision.h"

namespace {

void
usage (const char* argv0)
{
	std::cout <<
	  "SPECTER — motor do Ardour + mixer do hardware num so binario\n"
	  "\n"
	  "uso: " << argv0 << " [opcoes]\n"
	  "\n"
	  "  (sem opcoes)      abre a janela\n"
	  "  --backends        lista os backends disponiveis e sai\n"
	  "  --self-test       teste de fumaca sem GUI (backend Dummy: nao toca som)\n"
	  "  --docs            imprime os caminhos de dados/config e sai\n"
	  "  --version         versao + revisao do core do Ardour\n"
	  "  --help            esta ajuda\n"
	  "\n"
	  "O backend \"None (Dummy)\" e o modo seguro: sobe o motor e processa\n"
	  "silencio, sem abrir nenhum dispositivo de som.\n";
}

/* Sobe o core e lista o que ele oferece. */
int
cmd_backends ()
{
	spook::Engine engine;
	std::string err;
	if (!engine.init (spook::locale_dir (), err)) {
		std::cerr << "[ERRO] " << err << "\n";
		return 1;
	}

	std::vector<spook::BackendEntry> bs = engine.backend_details ();
	std::cout << "backends do Ardour (" << bs.size () << "):\n";
	for (size_t i = 0; i < bs.size (); ++i) {
		std::cout << "  - " << bs[i].name;
		if (!bs[i].description.empty ()) { std::cout << "  — " << bs[i].description; }
		std::cout << "\n";
	}
	std::cout << "  - None (Dummy)  — (escondido por hide_dummy_backend; modo seguro, "
	             "processa silencio e nao abre som)\n";
	engine.shutdown ();
	return 0;
}

int
cmd_docs ()
{
	const char* keys[] = { "ARDOUR_DATA_PATH", "ARDOUR_CONFIG_PATH",
	                       "ARDOUR_DLL_PATH", "VAMP_PATH", "LD_LIBRARY_PATH" };
	for (size_t i = 0; i < sizeof (keys) / sizeof (keys[0]); ++i) {
		const char* v = std::getenv (keys[i]);
		std::cout << keys[i] << " = " << (v ? v : "(nao definido)") << "\n";
	}
	return 0;
}

/* Teste de fumaca: init -> backend Dummy -> sessao nova -> trilha -> rotas.
 * Nao abre nenhum dispositivo de som e nao depende de GUI. */
int
cmd_self_test ()
{
	spook::Engine engine;
	std::string err;

	std::cout.setf (std::ios::unitbuf); /* nao perder a saida se algo estourar */
	std::cout << "SPECTER — self-test (backend Dummy, sem som)\n";

	if (!engine.init (spook::locale_dir (), err)) {
		std::cerr << "  [FALHA] init: " << err << "\n";
		return 1;
	}
	std::cout << "  [ok] ARDOUR::init()\n";

	std::vector<std::string> bs = engine.available_backends ();
	std::cout << "  [ok] backends disponiveis: " << bs.size () << "\n";
	for (size_t i = 0; i < bs.size (); ++i) { std::cout << "        - " << bs[i] << "\n"; }

	if (!engine.select_backend ("None (Dummy)", "", err)) {
		std::cerr << "  [FALHA] select_backend: " << err << "\n";
		return 2;
	}
	std::cout << "  [ok] set_backend(None (Dummy))\n";

	if (!engine.start_engine (err)) {
		std::cerr << "  [FALHA] start: " << err << "\n";
		return 3;
	}
	std::cout << "  [ok] motor no ar: " << engine.backend_name ()
	          << " @ " << (long) engine.sample_rate () << " Hz\n";

	/* Diretorio tem de NAO existir para o Ardour criar a sessao. */
	char tmpl[] = "/tmp/opencode/spook-app-selftest-XXXXXX";
	char* dir = ::mkdtemp (tmpl);
	if (!dir) { std::cerr << "  [FALHA] mkdtemp\n"; return 4; }
	::rmdir (dir); /* o Ardour exige que o diretorio da sessao ainda nao exista */
	std::string sdir = dir;

	if (!engine.open_session (sdir, "selftest", err)) {
		std::cerr << "  [FALHA] open_session: " << err << "\n";
		return 5;
	}
	std::cout << "  [ok] sessao \"" << engine.session_name () << "\" @ "
	          << (long) engine.session_sample_rate () << " Hz\n";

	std::vector<std::string> tr = engine.add_audio_track ("Trilha de Teste");
	std::cout << "  [ok] trilhas criadas: " << tr.size ();
	for (size_t i = 0; i < tr.size (); ++i) { std::cout << " (" << tr[i] << ")"; }
	std::cout << "\n";

	/* Valida a mesma API usada pelo Track Header do app: o nome é da Route
	 * real do Ardour, não um apelido visual. */
	spook::Daw daw;
	daw.attach (engine.session ());
	if (!daw.rename_route (0, "Trilha Renomeada", err)) {
		std::cerr << "  [FALHA] renomear trilha: " << err << "\n";
		return 6;
	}
	std::cout << "  [ok] trilha renomeada: " << daw.route_names ()[0] << "\n";
	if (!daw.duplicate_route (0, err)) {
		std::cerr << "  [FALHA] duplicar trilha: " << err << "\n";
		return 6;
	}
	std::cout << "  [ok] trilha duplicada: " << daw.route_names ().size () << " rotas\n";
	if (!daw.add_arranger_section ("Intro", 8, err)) {
		std::cerr << "  [FALHA] criar seção do arranger: " << err << "\n";
		return 6;
	}
	std::cout << "  [ok] seção Arranger persistente: " << daw.markers ().size () << " local(is)\n";
	std::vector<std::string> midis = daw.new_midi_track ("MIDI Teste");
	if (midis.empty () || !daw.add_midi_clip (2, "Clip Teste", 4, err)) {
		std::cerr << "  [FALHA] criar clip MIDI: " << err << "\n";
		return 6;
	}
	if (!daw.duplicate_clip (0, err) || !daw.move_clip (0, 96000, err) || !daw.resize_clip (0, 192000, err)) {
		std::cerr << "  [FALHA] editar clip MIDI: " << err << "\n";
		return 6;
	}
	if (!daw.remove_clip (1, err)) {
		std::cerr << "  [FALHA] apagar clip MIDI: " << err << "\n";
		return 6;
	}
	if (!daw.add_midi_note (0, 60, 0.0, 1.0, 100, err) ||
	    !daw.add_midi_note (0, 64, 1.0, 0.5, 96, err)) {
		std::cerr << "  [FALHA] criar notas MIDI: " << err << "\n";
		return 6;
	}
	std::vector<spook::MidiNoteInfo> notas = daw.midi_notes (0, err);
	if (!err.empty () || notas.size () != 2 ||
	    !daw.move_midi_note (0, notas[0].id, 0.5, err) ||
	    !daw.resize_midi_note (0, notas[0].id, 1.5, err) ||
	    !daw.remove_midi_note (0, notas[0].id, err) || daw.midi_notes (0, err).size () != 1) {
		std::cerr << "  [FALHA] editar notas MIDI: " << err << "\n";
		return 6;
	}
	std::cout << "  [ok] clip MIDI material/editável: " << daw.clips ().size () << " clip(s), " << daw.midi_notes (0, err).size () << " nota(s)\n";

	/* Fixture deliberadamente gerada no /tmp: confirma a rota de importação
	 * sem capturar/reproduzir nenhum som real. */
	const std::string wav = sdir + "/silencio-fixture.wav";
	const std::string py = "import wave; w=wave.open('" + wav + "','wb'); w.setnchannels(1); w.setsampwidth(2); w.setframerate(48000); w.writeframes(b'\\0\\0'*4800); w.close()";
	const int mk_wav = std::system (("python3 -c \"" + py + "\"").c_str ());
	if (mk_wav != 0 || !daw.import_audio_clip (0, wav, err)) {
		std::cerr << "  [FALHA] importar clip de áudio: " << err << "\n";
		return 6;
	}
	if (daw.clips ().size () != 2) {
		std::cerr << "  [FALHA] clip de áudio não entrou na Playlist\n";
		return 6;
	}
	int audio_clip = -1;
	const std::vector<spook::ClipInfo> all_clips = daw.clips ();
	for (size_t i = 0; i < all_clips.size (); ++i) { if (!all_clips[i].is_midi) { audio_clip = (int) i; break; } }
	std::vector<float> wave = daw.audio_waveform (audio_clip, 24, err);
	if (audio_clip < 0 || !err.empty () || wave.size () != 24) {
		std::cerr << "  [FALHA] ler waveform da fonte de áudio: " << err << "\n";
		return 6;
	}
	std::cout << "  [ok] clip de áudio importado como Region + waveform: " << daw.clips ().size () << " clip(s)\n";

	std::vector<std::string> routes = engine.route_names ();
	std::cout << "  [ok] rotas na sessao: " << routes.size () << "\n";
	for (size_t i = 0; i < routes.size (); ++i) { std::cout << "        - " << routes[i] << "\n"; }
	if (routes.empty ()) { std::cerr << "  [FALHA] nenhuma rota (o motor nao criou nada)\n"; return 6; }

	engine.close_session ();
	std::cout << "  [ok] sessao fechada\n";
	engine.stop_engine ();
	std::cout << "  [ok] motor parado\n";
	engine.shutdown ();
	std::cout << "self-test: TUDO OK\n";
	return 0;
}

/* ----------------------------------------------------- diagnostico da arvore */

/* Imprime a hierarquia de widgets (para conferir a UI sem abrir janela na
 * sessao do usuario). Ligado por SPOOK_DUMP_TREE=1. */
void
dump_tree (Gtk::Widget& w, int depth)
{
	std::string pad ((size_t) (depth * 2), ' ');
	std::string extra;

	if (Gtk::Label* l = dynamic_cast<Gtk::Label*> (&w)) {
		extra = " \"" + l->get_text () + "\"";
	} else if (Gtk::Entry* e = dynamic_cast<Gtk::Entry*> (&w)) {
		extra = " [" + e->get_text () + "]";
	} else if (Gtk::Button* b = dynamic_cast<Gtk::Button*> (&w)) {
		extra = " <" + b->get_label () + ">";
	} else if (Gtk::Notebook* n = dynamic_cast<Gtk::Notebook*> (&w)) {
		extra = " pages=" + std::to_string (n->get_n_pages ())
		      + " ativa=" + std::to_string (n->get_current_page ());
	} else if (Gtk::ComboBoxText* c = dynamic_cast<Gtk::ComboBoxText*> (&w)) {
		extra = " ativo=\"" + c->get_active_text () + "\"";
		if (Glib::RefPtr<Gtk::TreeModel> m = c->get_model ()) {
			extra += " itens=" + std::to_string (m->children ().size ());
		}
	} else if (Gtk::ListBox* lb = dynamic_cast<Gtk::ListBox*> (&w)) {
		extra = " filhos=" + std::to_string (lb->get_children ().size ());
	} else if (Gtk::TextView* tv = dynamic_cast<Gtk::TextView*> (&w)) {
		extra = tv->get_monospace () ? " (monospace)" : "";
	}

	/* ATENCAO: `w` e um wrapper C++ do gtkmm; G_OBJECT_TYPE_NAME(&w) daria um
	 * ponteiro invalido (nao e um GObject*) e g_type_name() devolveria NULL.
	 * Um `<< (const char*)NULL` marca badbit no std::cout e CALA toda a saida
	 * seguinte. O caminho certo e w.gobj() (o GObject embrulhado). */
	const char* tn = G_OBJECT_TYPE_NAME (w.gobj ());
	const Gtk::Allocation al = w.get_allocation ();
	std::cout << pad << (tn ? tn : "?") << extra
	          << "  [" << al.get_width () << "x" << al.get_height ()
	          << " @" << al.get_x () << "," << al.get_y () << "]\n";
	if (!std::cout) { std::cout.clear (); }

	if (Gtk::Container* c = dynamic_cast<Gtk::Container*> (&w)) {
		std::vector<Gtk::Widget*> kids = c->get_children ();
		for (size_t i = 0; i < kids.size (); ++i) { dump_tree (*kids[i], depth + 1); }
	}
}

int
run_gui (int argc, char** argv)
{
	Glib::set_application_name ("SPECTER");
	/* Identidade da JANELA para o compositor (Hyprland/quickshell casam isto
	 * com o .desktop de mesmo id para achar o ícone e o app certo).
	 * Wayland (gdkwindow-wayland.c): app_id = g_get_prgname(), e SÓ usa
	 * gdk_get_program_class() como fallback quando prgname é NULL. Por isso
	 * os DOIS são definidos — antes o app_id saía "spook-audio" e o shell
	 * associava a janela ao ícone antigo do SPOOK. X11: program_class. */
	g_set_prgname ("com.taylor.specter");
	gdk_set_program_class ("com.taylor.specter");

	Gio::ApplicationFlags flags = Gio::APPLICATION_FLAGS_NONE;
	if (std::getenv ("SPOOK_NON_UNIQUE")) {
		flags = Gio::APPLICATION_NON_UNIQUE;
	}

	Glib::RefPtr<Gtk::Application> app =
	  Gtk::Application::create (argc, argv, "com.taylor.specter", flags);

	spook::MainWindow window;

	/* `app->run(window)` mostra SO o toplevel (gtk_widget_show nao cascateia
	 * nos filhos). Sem isto a janela aparece VAZIA e o notebook fica com
	 * current_page = -1. O jeito classico do gtkmm e show_all() na janela. */
	window.show_all ();

	/* X11: o _NET_WM_ICON só é aplicado quando a janela já está realizada;
	 * reaplica no map para o ícone valer também em X11/XWayland. */
	window.signal_map ().connect ([&window] () {
		try { window.set_icon_from_file (spook::exe_dir () + "/assets/specter-icon.png"); }
		catch (const Glib::Error&) { }
	});

	/* Diagnóstico headless: sobe motor + abre sessão + cria trilhas, para o
	 * dump da árvore mostrar o mixer/lista de ações com conteúdo de verdade.
	 * Só roda com SPOOK_AUTO_SESSION=1 (nunca no uso normal). */
	if (std::getenv ("SPOOK_AUTO_SESSION")) {
		window.debug_bootstrap_session ();
	}
	if (const char* tab = std::getenv ("SPOOK_TAB")) {
		window.show_tab (std::atoi (tab));
	}
	if (std::getenv ("SPOOK_KEYTEST")) {
		window.debug_key_test ();
	}

	if (std::getenv ("SPOOK_DUMP_TREE")) {
		std::cout.setf (std::ios::unitbuf);
		Glib::signal_timeout ().connect ([&window, app] () {
			std::fprintf (stderr, "[DUMP] entrada\n");
			std::cout << "--- arvore de widgets ---\n";
			std::fprintf (stderr, "[DUMP] antes do dump_tree\n");
			try {
				dump_tree (window, 0);
			} catch (const std::exception& e) {
				std::fprintf (stderr, "[DUMP] EXCECAO: %s\n", e.what ());
			} catch (...) {
				std::fprintf (stderr, "[DUMP] EXCECAO desconhecida\n");
			}
			std::fprintf (stderr, "[DUMP] depois do dump_tree\n");
			std::cout << "--- fim ---\n";
			app->quit ();
			return false;
		}, 1500);
	}

	return app->run (window);
}

} // namespace

int
main (int argc, char* argv[])
{
	/* Autonomia: resolve o runtime embutido (vendor/ardour) ANTES de tudo,
	 * para --docs/--version/--self-test mostrarem os caminhos reais. */
	spook::setup_env_defaults ();

	/* argumentos que repassamos ao GTK (a janela aceita os dela: --display,
	 * --gdk-backend, --gtk-module…). Os nossos modos sao tratados aqui. */
	std::vector<std::string> gui_args;
	gui_args.push_back (argc > 0 ? argv[0] : "spook-audio");

	for (int i = 1; i < argc; ++i) {
		std::string a = argv[i];

		if (a == "--help" || a == "-h") { usage (argv[0]); return 0; }

		if (a == "--version") {
			const char* dll = std::getenv ("ARDOUR_DLL_PATH");
			std::cout << "SPECTER — Fase 4 (binario unico: libardour + ALSA)\n"
			          << "core do Ardour: " << ARDOUR::revision << "\n"
			          << "libardour: " << (dll ? dll : "(nao definido)") << "  ·  sessao GUI: gtkmm-3.0\n";
			return 0;
		}

		/* Modos de linha de comando (sem GUI). */
		if (a == "--backends")  { spook::log_install (); return cmd_backends (); }
		if (a == "--docs")      { return cmd_docs (); }
		if (a == "--self-test") { spook::log_install (); return cmd_self_test (); }

		if (a == "--ui") { continue; } /* nosso: "abre a janela" (padrao) */

		gui_args.push_back (a);
	}

	std::vector<char*> gargv;
	for (size_t i = 0; i < gui_args.size (); ++i) { gargv.push_back (const_cast<char*> (gui_args[i].c_str ())); }
	return run_gui ((int) gargv.size (), gargv.data ());
}
