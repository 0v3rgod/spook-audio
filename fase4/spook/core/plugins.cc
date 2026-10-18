#include "core/plugins.h"
#include "core/log.h"

#include <algorithm>
#include <mutex>

#include "ardour/plugin.h"
#include "ardour/plugin_manager.h"

using namespace ARDOUR;

namespace spook {

namespace {

std::vector<PluginEntry> _list;
/* a lista e escrita pela thread da varredura e lida pela thread da UI. */
std::mutex _list_mtx;

const char*
tname (PluginType t)
{
	switch (t) {
		case LV2:     return "LV2";
		case LADSPA:  return "LADSPA";
		case Windows_VST: return "VST (Windows)";
		case LXVST:   return "VST2";
		case MacVST:  return "VST (Mac)";
		case AudioUnit: return "AU";
		case VST3:    return "VST3";
		case Lua:     return "Lua";
		default:      return "?";
	}
}

void
collect (PluginInfoList const& l, std::vector<PluginEntry>& out)
{
	for (PluginInfoList::const_iterator i = l.begin (); i != l.end (); ++i) {
		PluginEntry e;
		e.type      = tname ((*i)->type);
		e.name      = (*i)->name;
		e.unique_id = (*i)->unique_id;
		e.category  = (*i)->category;
		e.creator   = (*i)->creator;
		e.io        = std::to_string ((long) (*i)->n_inputs.n_audio ()) + " in / "
		            + std::to_string ((long) (*i)->n_outputs.n_audio ()) + " out";
		out.push_back (e);
	}
}

void
gather ()
{
	PluginManager& pm = PluginManager::instance ();
	std::vector<PluginEntry> out;
	collect (pm.lv2_plugin_info (), out);
	collect (pm.ladspa_plugin_info (), out);
	collect (pm.lxvst_plugin_info (), out);
	collect (pm.vst3_plugin_info (), out);
	collect (pm.lua_plugin_info (), out);

	std::sort (out.begin (), out.end (), [] (PluginEntry const& a, PluginEntry const& b) {
		if (a.type != b.type) { return a.type < b.type; }
		return a.name < b.name;
	});
	std::lock_guard<std::mutex> lk (_list_mtx);
	_list = out;
}

size_t
list_size ()
{
	std::lock_guard<std::mutex> lk (_list_mtx);
	return _list.size ();
}

} // namespace

size_t
plugins_from_cache ()
{
	log_line ("[PLUGINS] lendo o cache do PluginManager (rapido)...");
	PluginManager::instance ().refresh (true);
	gather ();
	log_line ("[PLUGINS] " + std::to_string ((long) list_size ()) + " plugins no cache");
	return list_size ();
}

size_t
plugins_full_scan ()
{
	log_line ("[PLUGINS] varredura completa (demora)...");
	PluginManager::instance ().refresh (false);
	gather ();
	log_line ("[PLUGINS] " + std::to_string ((long) list_size ()) + " plugins apos varredura");
	return list_size ();
}

std::vector<PluginEntry>
plugin_list ()
{
	std::lock_guard<std::mutex> lk (_list_mtx);
	return _list;
}

} // namespace spook
