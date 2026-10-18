/*
 * SPOOK AUDIO — plugins
 *
 * Usa o proprio PluginManager do Ardour (nao linkamos com libcarla por enquanto).
 * `refresh(true)` = so cache: instantaneo. `refresh(false)` = varredura completa
 * (dezenas de segundos) — fica para um botao explicito.
 */
#pragma once

#include <string>
#include <vector>

namespace spook {

struct PluginEntry {
	std::string type;      // "LV2", "LADSPA", "VST2", "VST3", "Lua", ...
	std::string name;
	std::string unique_id;
	std::string category;
	std::string creator;
	std::string io;        // "2 in / 2 out"
};

/** Varredura so-cache (rapida). Devolve quantos plugins achou. */
size_t plugins_from_cache ();

/** Varredura completa (lenta, leva dezenas de segundos). */
size_t plugins_full_scan ();

/** Ultima lista obtida (ordenada por tipo e nome). Copia — a varredura
 *  completa escreve a lista em outra thread. */
std::vector<PluginEntry> plugin_list ();

} // namespace spook
