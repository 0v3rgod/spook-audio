/*
 * SPOOK AUDIO — nucleo do motor
 *
 * Envolve ARDOUR::init / ARDOUR::AudioEngine / ARDOUR::Session. NENHUM header de
 * toolkit aqui: quem desenha e a camada ui/.
 *
 * Regra de ABI: o build TEM de usar -DWAF_BUILD (ver ../PoC-ENGINE.md). Sem isso o
 * layout de ARDOUR::Session desloca 16 bytes e tudo que e inline no header le
 * lixo. Rode ../tools/abi-check.sh depois de atualizar o pacote `ardour`.
 */
#pragma once

#include <string>
#include <vector>

namespace ARDOUR {
class AudioEngine;
class Session;
class Route;
} // namespace ARDOUR

namespace spook {

struct BackendEntry {
	std::string name;        // ex.: "ALSA", "JACK/Pipewire", "None (Dummy)"
	std::string description; // disponibilidade/config, deduzida de AudioBackendInfo
};

/* --- caminhos do runtime embutido (vendor/ardour ao lado do binario) ---
 * O app NAO depende de /usr/lib/ardour9 nem do pacote `ardour` instalado:
 * setup_env_defaults() aponta ARDOUR_DLL_PATH/DATA_PATH/CONFIG_PATH/VAMP_PATH
 * para vendor/ardour quando ele existe (senao cai para os caminhos do
 * sistema). Idempotente: nao sobrescreve o que ja vier do ambiente. */
std::string exe_dir ();            // diretorio do executavel (via /proc/self/exe)
std::string vendor_dir ();         // exe_dir() + "/vendor/ardour"
void        setup_env_defaults ();
std::string locale_dir ();         // vendor/ardour/share/locale (fallback sistema)

class Engine {
  public:
	Engine ();
	~Engine ();

	Engine (Engine const&) = delete;
	Engine& operator= (Engine const&) = delete;

	/* --- ciclo de vida --- */
	bool init (std::string const& localedir, std::string& err);
	bool initialised () const { return _initialised; }
	void shutdown (); // ARDOUR::cleanup()

	/* --- backends --- */
	std::vector<std::string> available_backends ();
	std::vector<BackendEntry>  backend_details ();
	/* device vazio = deixa o backend escolher o default */
	bool select_backend (std::string const& name, std::string const& device, std::string& err);
	bool start_engine (std::string& err);
	void stop_engine ();
	bool engine_running () const;
	std::string backend_name () const;
	double sample_rate () const;

	/* --- sessao --- */
	bool session_open () const { return _session != 0; }
	/* o dono da sessao e o Engine; a ui/ e o core/daw.cc so a usam. */
	ARDOUR::Session* session () const { return _session; }
	/* dir NAO pode existir se for para criar (ver PoC-ENGINE.md) */
	bool open_session (std::string const& dir, std::string const& snapshot, std::string& err);
	void close_session ();
	std::string session_name () const;
	double session_sample_rate () const;
	std::vector<std::string> route_names () const;
	/* devolve os nomes das trilhas criadas */
	std::vector<std::string> add_audio_track (std::string const& name, unsigned int nchan = 2);

  private:
	ARDOUR::AudioEngine* _engine;
	ARDOUR::Session*     _session;
	bool                 _initialised;
};

} // namespace spook
