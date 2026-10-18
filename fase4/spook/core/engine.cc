#include "core/engine.h"
#include "core/log.h"

#include <cstdlib>
#include <exception>

#include <sys/stat.h>
#include <unistd.h>

#include <glibmm.h>

#include "pbd/pbd.h"

#include "ardour/ardour.h"
#include "ardour/audio_backend.h"
#include "ardour/audio_track.h"
#include "ardour/audioengine.h"
#include "ardour/route.h"
#include "ardour/session.h"
#include "ardour/session_event.h"
#include "ardour/types.h"

/* O libardour deixa vstfx_* indefinidos de proposito (headless/misc.cc); quem
 * linka tem de fornecer. Vale para qualquer binario nosso. */
#include "ardour/vst_types.h"
int  vstfx_init (void*) { return 0; }
void vstfx_exit () {}
void vstfx_destroy_editor (VSTState*) {}

using namespace ARDOUR;

namespace spook {

/* -------------------- caminhos do runtime embutido -------------------- */

std::string
exe_dir ()
{
	char buf[4096];
	const ssize_t n = ::readlink ("/proc/self/exe", buf, sizeof (buf) - 1);
	if (n <= 0) { return std::string (); }
	buf[n] = '\0';
	const std::string p (buf);
	const std::string::size_type slash = p.rfind ('/');
	return slash == std::string::npos ? std::string () : p.substr (0, slash);
}

std::string
vendor_dir ()
{
	return exe_dir () + "/vendor/ardour";
}

namespace {

bool
_dir_ok (const std::string& p)
{
	struct stat st;
	return ::stat (p.c_str (), &st) == 0 && S_ISDIR (st.st_mode);
}

void
_setenv_unless_set (const char* k, const std::string& v)
{
	if (!std::getenv (k)) { ::setenv (k, v.c_str (), 1); }
}

} /* namespace */

void
setup_env_defaults ()
{
	const std::string v = vendor_dir ();
	const bool embutido = _dir_ok (v + "/lib");

	const std::string libroot  = embutido ? v + "/lib" : "/usr/lib/ardour9";
	const std::string dataroot = (embutido && _dir_ok (v + "/share")) ? v + "/share" : "/usr/share/ardour9";
	const std::string confroot = (embutido && _dir_ok (v + "/etc")) ? v + "/etc" : "/etc/ardour9";

	_setenv_unless_set ("ARDOUR_DLL_PATH", libroot);
	_setenv_unless_set ("ARDOUR_DATA_PATH", dataroot);
	_setenv_unless_set ("ARDOUR_CONFIG_PATH", confroot);
	_setenv_unless_set ("VAMP_PATH", libroot + "/vamp");
}

std::string
locale_dir ()
{
	if (const char* d = std::getenv ("ARDOUR_DATA_PATH")) {
		const std::string cand = std::string (d) + "/locale";
		if (_dir_ok (cand)) { return cand; }
	}
	const std::string vend = vendor_dir () + "/share/locale";
	if (_dir_ok (vend)) { return vend; }
	return "/usr/share/ardour9/locale";
}

Engine::Engine ()
	: _engine (0)
	, _session (0)
	, _initialised (false)
{
}

Engine::~Engine ()
{
	shutdown ();
}

bool
Engine::init (std::string const& localedir, std::string& err)
{
	if (_initialised) { return true; }

	/* Garante ARDOUR_DLL_PATH/DATA_PATH/CONFIG_PATH/VAMP_PATH apontando para o
	 * vendor embutido (ou sistema) mesmo se o chamador tiver esquecido. */
	setup_env_defaults ();

	std::string ldir = localedir;
	if (!_dir_ok (ldir)) {
		const std::string alt = locale_dir ();
		if (_dir_ok (alt)) { ldir = alt; }
	}

	log_install ();

	if (!ARDOUR::init (false, ldir.c_str ())) {
		err = "ARDOUR::init() falhou";
		return false;
	}
	SessionEvent::create_per_thread_pool ("spook", 512);
	_initialised = true;
	return true;
}

void
Engine::shutdown ()
{
	if (!_initialised) {
		/* Nada inicializado: nao ha singleton para soltar. */
		_engine = 0;
		_session = 0;
		return;
	}

	/* ORDEM IMPORTA: a sessao e o motor tem de morrer ANTES do
	 * ARDOUR::cleanup(), que por sua vez apaga o singleton AudioEngine
	 * (audioengine.cc:859 AudioEngine::destroy()). Sem isso o destrutor
	 * tocaria em ponteiro morto (SIGSEGV em pc=0). */
	close_session ();
	stop_engine ();

	ARDOUR::cleanup ();

	_engine = 0;
	_session = 0;
	_initialised = false;
}

std::vector<std::string>
Engine::available_backends ()
{
	std::vector<std::string> out;
	if (!_engine) { _engine = AudioEngine::create (); }
	std::vector<const AudioBackendInfo*> bs = _engine->available_backends ();
	for (size_t i = 0; i < bs.size (); ++i) { out.push_back (bs[i]->name); }

	/* O Dummy e filtrado por Config->hide_dummy_backend() (audioengine.cc:961). Ele
	 * NAO aparece na lista mas funciona via set_backend() — e o nosso modo seguro de
	 * testar sem tocar em som. */
	bool has_dummy = false;
	for (size_t i = 0; i < out.size (); ++i) {
		if (out[i] == "None (Dummy)") { has_dummy = true; }
	}
	if (!has_dummy) { out.push_back ("None (Dummy)"); }
	return out;
}

std::vector<BackendEntry>
Engine::backend_details ()
{
	std::vector<BackendEntry> out;
	if (!_engine) { _engine = AudioEngine::create (); }
	std::vector<const AudioBackendInfo*> bs = _engine->available_backends ();
	for (size_t i = 0; i < bs.size (); ++i) {
		BackendEntry e;
		e.name = bs[i]->name;
		/* AudioBackendInfo nao tem descricao textual; o que ele diz de si
		 * mesmo e se esta disponivel e se ja esta configurado. */
		bool av = bs[i]->available      ? bs[i]->available ()             : true;
		bool cf = bs[i]->already_configured ? bs[i]->already_configured () : false;
		e.description = std::string (av ? "disponivel" : "indisponivel")
		              + (cf ? " · ja configurado" : "");
		out.push_back (e);
	}
	return out;
}

bool
Engine::select_backend (std::string const& name, std::string const& device, std::string& err)
{
	if (!_initialised) { err = "ARDOUR::init() nao foi chamado"; return false; }
	if (!_engine) { _engine = AudioEngine::create (); }
	if (engine_running ()) { stop_engine (); }

	if (!_engine->set_backend (name, "spook-audio", device)) {
		err = "nao consegui set_backend(\"" + name + "\")";
		return false;
	}
	log_line ("[ENGINE] backend = " + name + (device.empty () ? "" : (" device=" + device)));
	return true;
}

bool
Engine::start_engine (std::string& err)
{
	if (!_engine) { err = "sem AudioEngine"; return false; }
	if (_engine->start () != 0) {
		err = "o motor nao subiu";
		return false;
	}
	log_line ("[ENGINE] motor no ar: " + _engine->current_backend_name ());
	return true;
}

void
Engine::stop_engine ()
{
	if (!_initialised || !_engine) { return; }
	if (_engine->session ()) { _engine->remove_session (); }
	if (_engine->running ()) { _engine->stop (); }
	log_line ("[ENGINE] motor parado");
}

bool
Engine::engine_running () const
{
	return _engine && _engine->running ();
}

std::string
Engine::backend_name () const
{
	if (!_engine) { return ""; }
	return _engine->current_backend_name ();
}

double
Engine::sample_rate () const
{
	if (!_engine) { return 0; }
	return _engine->sample_rate ();
}

bool
Engine::open_session (std::string const& dir, std::string const& snapshot, std::string& err)
{
	if (!_engine) { err = "sem AudioEngine"; return false; }
	if (!engine_running ()) { err = "o motor precisa estar no ar"; return false; }

	close_session ();

	try {
		/* Diretorio inexistente => o Ardour CRIA a sessao
		 * (_is_new = !Glib::file_test(_path, IS_DIR), session_state.cc:178). */
		_session = new Session (*_engine, dir, snapshot);
	} catch (std::exception& e) {
		_session = 0;
		err = std::string ("erro ao abrir a sessao: ") + e.what ();
		return false;
	}

	_engine->set_session (_session);
	Glib::usleep (300000);

	log_line ("[SESSAO] \"" + _session->name () + "\" @ "
	          + std::to_string ((long) _session->sample_rate ()) + " Hz");
	return true;
}

void
Engine::close_session ()
{
	if (!_session) { return; }
	if (_engine) { _engine->remove_session (); }
	delete _session;
	_session = 0;
}

std::string
Engine::session_name () const
{
	return _session ? _session->name () : "";
}

double
Engine::session_sample_rate () const
{
	return _session ? (double) _session->sample_rate () : 0.0;
}

std::vector<std::string>
Engine::route_names () const
{
	std::vector<std::string> out;
	if (!_session) { return out; }
	std::shared_ptr<RouteList const> routes = _session->get_routes ();
	for (RouteList::const_iterator i = routes->begin (); i != routes->end (); ++i) {
		out.push_back ((*i)->name ());
	}
	return out;
}

std::vector<std::string>
Engine::add_audio_track (std::string const& name, unsigned int nchan)
{
	std::vector<std::string> out;
	if (!_session) { return out; }
	AudioTrackList tl = _session->new_audio_track (nchan, nchan, std::shared_ptr<RouteGroup> (),
	                                               1, name, PresentationInfo::max_order);
	for (AudioTrackList::iterator i = tl.begin (); i != tl.end (); ++i) {
		out.push_back ((*i)->name ());
	}
	return out;
}

} // namespace spook
