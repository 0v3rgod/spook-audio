/*
 * SPOOK AUDIO — engine (Fase 4, prova de conceito do binario unico)
 *
 * Prova que o NOSSO processo fala com o core do Ardour (ARDOUR::AudioEngine /
 * ARDOUR::Session) SEM abrir a GUI do Ardour e SEM executar `ardour9`/`carla`.
 * Este arquivo nao inclui NENHUM header de toolkit grafico.
 *
 * Base: bootstrap oficial `headless/load_session.cc` (Ardour, GPL-2.0-or-later).
 *
 * Modos:
 *   --probe                        sonda cada .so de backend (por que entrou/saiu)
 *   --backends                     lista os backends disponiveis
 *   --backend NAME                 escolhe o motor   (default: "None (Dummy)")
 *   --session DIR NOME             abre (ou cria) uma sessao
 *   --track NOME                   cria uma trilha de audio na sessao
 *   --seconds N                    mantem o motor rodando N segundos
 */
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include <glibmm.h>
#include <glibmm/module.h>

#include "pbd/error.h"
#include "pbd/pbd.h"
#include "pbd/receiver.h"
#include "pbd/transmitter.h"

#include "ardour/ardour.h"
#include "ardour/audioengine.h"
#include "ardour/audio_backend.h"
#include "ardour/revision.h"
#include "ardour/route.h"
#include "ardour/session.h"
#include "ardour/session_event.h"
#include "ardour/types.h"

using namespace std;
using namespace ARDOUR;

static const char* localedir = LOCALEDIR;

/* O libardour deixa vstfx_* em aberto de proposito ("code design confusion",
 * Feb 2014 — ver headless/misc.cc). Quem linka tem que fornecer. */
#include "ardour/vst_types.h"
int  vstfx_init (void*) { return 0; }
void vstfx_exit () {}
void vstfx_destroy_editor (VSTState*) {}

/* Recebe os avisos/erros do PBD e mostra no stderr. */
class Recv : public Receiver
{
  protected:
	void receive (Transmitter::Channel chn, const char* str)
	{
		const char* p = "";
		switch (chn) {
			case Transmitter::Debug:   return;
			case Transmitter::Info:    return;
			case Transmitter::Warning: p = "[AVISO] "; break;
			case Transmitter::Error:   p = "[ERRO] ";  break;
			case Transmitter::Fatal:   p = "[FATAL] "; break;
			case Transmitter::Throw:   ::abort ();
		}
		cerr << p << str << endl;
	}
};

static Recv recv_;

static string
opt (int argc, char** argv, const string& key, const string& def = "")
{
	for (int i = 1; i < argc - 1; ++i) {
		if (key == argv[i]) { return argv[i + 1]; }
	}
	return def;
}

static bool
has (int argc, char** argv, const string& key)
{
	for (int i = 1; i < argc; ++i) {
		if (key == argv[i]) { return true; }
	}
	return false;
}

static int
cmd_backends ()
{
	AudioEngine* engine = AudioEngine::create ();
	vector<const AudioBackendInfo*> bs = engine->available_backends ();
	cout << "backends listados: " << bs.size () << endl;
	for (size_t i = 0; i < bs.size (); ++i) {
		cout << "  - " << bs[i]->name << endl;
	}
	return 0;
}

/* Mesma logica de AudioEngine::backend_discover(), mas contando o motivo. */
static int
cmd_probe ()
{
	string dir = Glib::getenv ("ARDOUR_DLL_PATH");
	if (dir.empty ()) { dir = "/usr/lib/ardour9"; }
	dir += "/backends";

	AudioEngine* engine = AudioEngine::create ();
	map<string, const AudioBackendInfo*> listed;
	vector<const AudioBackendInfo*> bs = engine->available_backends ();
	for (size_t i = 0; i < bs.size (); ++i) { listed[bs[i]->name] = bs[i]; }

	cout << "sondando " << dir << endl;

	GDir* gd = g_dir_open (dir.c_str (), 0, 0);
	if (!gd) { cerr << "nao abriu " << dir << endl; return 1; }

	const gchar* entry;
	while ((entry = g_dir_read_name (gd))) {
		string name (entry);
		if (name.find ("backend.so") == string::npos) { continue; }
		string path = dir + "/" + name;

		Glib::Module module (path);
		if (!module) {
			cout << "  [nao carrega] " << name << " : " << Glib::Module::get_last_error () << endl;
			continue;
		}
		void* func = 0;
		if (!module.get_symbol ("descriptor", func)) {
			cout << "  [sem descriptor()] " << name << endl;
			continue;
		}
		AudioBackendInfo* (*dfunc) (void) = (AudioBackendInfo* (*) (void)) func;
		AudioBackendInfo* info = dfunc ();
		cout << "  [ok] " << name << " -> \"" << info->name << "\""
		     << " available=" << (info->available () ? "sim" : "NAO")
		     << " listado=" << (listed.find (info->name) != listed.end () ? "sim" : "NAO")
		     << (string (info->name) == "None (Dummy)" && listed.find (info->name) == listed.end ()
		         ? "  (escondido por Config->hide_dummy_backend)" : "")
		     << endl;
	}
	g_dir_close (gd);
	return 0;
}

int
main (int argc, char* argv[])
{
	cout << "SPOOK AUDIO engine — Ardour " << ARDOUR::revision
	     << " (libardour linkada, process='" << PROGRAM_NAME << "', sem GUI)" << endl;

	recv_.listen_to (PBD::warning);
	recv_.listen_to (PBD::error);
	recv_.listen_to (PBD::fatal);

	if (!ARDOUR::init (false, localedir)) {
		cerr << "falhou ARDOUR::init()" << endl;
		return 1;
	}
	cout << "ARDOUR::init(): ok" << endl;

	int rc = 0;

	if (has (argc, argv, "--probe")) {
		rc = cmd_probe ();
		ARDOUR::cleanup ();
		return rc;
	}
	if (has (argc, argv, "--backends")) {
		rc = cmd_backends ();
		ARDOUR::cleanup ();
		return rc;
	}

	string backend = opt (argc, argv, "--backend", "None (Dummy)");
	string sdir    = opt (argc, argv, "--session");
	string sname   = opt (argc, argv, "--session");
	/* --session tem 2 argumentos: corrige pegando o proximo do proximo */
	for (int i = 1; i < argc - 2; ++i) {
		if (string (argv[i]) == "--session") { sdir = argv[i + 1]; sname = argv[i + 2]; }
	}
	int seconds = atoi (opt (argc, argv, "--seconds", "0").c_str ());
	string trackname = opt (argc, argv, "--track");

	(void) sname;

	SessionEvent::create_per_thread_pool ("spook", 512);

	cout << "escolhendo motor: " << backend << endl;
	AudioEngine* engine = AudioEngine::create ();
	if (!engine->set_backend (backend, "spook-audio", "")) {
		cerr << "nao consegui set_backend(\"" << backend << "\")" << endl;
		ARDOUR::cleanup ();
		return 2;
	}
	if (engine->start () != 0) {
		cerr << "o motor nao subiu" << endl;
		ARDOUR::cleanup ();
		return 3;
	}
	cout << "motor no ar: " << engine->current_backend_name ()
	     << " | sample_rate=" << engine->sample_rate () << " Hz" << endl;

	if (!sdir.empty ()) {
		string snapshot = (argc > 0) ? "" : "";
		for (int i = 1; i < argc - 2; ++i) {
			if (string (argv[i]) == "--session") { snapshot = argv[i + 2]; }
		}
		cout << "abrindo/criando sessao em " << sdir << " (snapshot \"" << snapshot << "\")" << endl;

		Session* session = 0;
		try {
			session = new Session (*engine, sdir, snapshot);
		} catch (exception& e) {
			cerr << "erro ao abrir a sessao: " << e.what () << endl;
			engine->stop ();
			ARDOUR::cleanup ();
			return 4;
		}
		engine->set_session (session);
		Glib::usleep (300000);

		cout << "sessao: \"" << session->name () << "\""
		     << " sample_rate=" << session->sample_rate () << endl;

		if (!trackname.empty ()) {
			cout << "criando trilha \"" << trackname << "\"..." << endl;
			AudioTrackList tl = session->new_audio_track (2, 2, std::shared_ptr<RouteGroup> (), 1, trackname, PresentationInfo::max_order);
			cout << "  criadas: " << tl.size () << endl;
		}

		std::shared_ptr<RouteList const> routes = session->get_routes ();
		cout << "rotas na sessao: " << routes->size () << endl;
		for (RouteList::const_iterator i = routes->begin (); i != routes->end (); ++i) {
			cout << "  - " << (*i)->name () << endl;
		}

		if (seconds > 0) {
			cout << "rodando o motor por " << seconds << "s..." << endl;
			for (int i = 0; i < seconds * 10; ++i) { Glib::usleep (100000); }
		}

		engine->remove_session ();
		delete session;
	}

	engine->stop ();
	ARDOUR::cleanup ();
	cout << "encerrado limpo." << endl;
	return rc;
}
