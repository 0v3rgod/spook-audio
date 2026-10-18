#include "core/log.h"

#include <cstdio>

#include "pbd/error.h"
#include "pbd/receiver.h"
#include "pbd/transmitter.h"

namespace spook {

namespace {

std::mutex        _mtx;
std::deque<std::string> _q;

class Recv : public Receiver {
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
		log_line (std::string (p) + str);
	}
};

Recv* _recv = nullptr;

} // namespace

void
log_line (std::string const& s)
{
	{
		std::lock_guard<std::mutex> lk (_mtx);
		_q.push_back (s);
		if (_q.size () > 4000) { _q.pop_front (); }
	}
	std::fprintf (stderr, "%s\n", s.c_str ());
}

std::vector<std::string>
log_drain (size_t max)
{
	std::vector<std::string> out;
	std::lock_guard<std::mutex> lk (_mtx);
	while (!_q.empty () && out.size () < max) {
		out.push_back (_q.front ());
		_q.pop_front ();
	}
	return out;
}

void
log_install ()
{
	if (_recv) { return; }
	_recv = new Recv;
	_recv->listen_to (PBD::warning);
	_recv->listen_to (PBD::error);
	_recv->listen_to (PBD::fatal);
}

} // namespace spook
