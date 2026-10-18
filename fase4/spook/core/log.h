/*
 * SPOOK AUDIO — log
 *
 * O PBD fala pelo Receiver/Transmitter dele (aviso/erro/fatal). Aqui a gente
 * captura isso e joga numa fila segura entre threads; a UI drena no timeout do
 * GTK (nunca mexer em widget a partir da thread do motor).
 */
#pragma once

#include <deque>
#include <mutex>
#include <string>
#include <vector>

namespace spook {

/** Instala o Receiver global. Chamar uma vez, antes de ARDOUR::init(). */
void log_install ();

/** Enfileira uma linha (uso interno / nosso). */
void log_line (std::string const& s);

/** Tira ate `max` linhas da fila (chamado do loop do GTK). */
std::vector<std::string> log_drain (size_t max = 64);

} // namespace spook
