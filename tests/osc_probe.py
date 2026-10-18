#!/usr/bin/env python3
"""Sonda OSC: conversa com o Ardour (ou Carla) e mostra o que volta.

    python3 tests/osc_probe.py                 # heartbeat + play + stop no Ardour
    python3 tests/osc_probe.py --port 22752    # fala com o Carla
"""
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from spookaudio import osc  # noqa: E402


def main() -> int:
    porta = 3819
    if "--port" in sys.argv:
        porta = int(sys.argv[sys.argv.index("--port") + 1])

    srv = osc.Server(0)
    srv.start()
    print(f"escutando feedback na porta local {srv.port} -> alvo 127.0.0.1:{porta}")

    cli = osc.Client("127.0.0.1", porta)
    testes = [
        ("/heartbeat", (1.0,), 1.5),
        ("/session_name", (), 1.0),
        ("/transport_play", (), 1.0),
        ("/transport_stop", (), 1.0),
        ("/record_enabled", (), 0.8),
    ]
    recebeu = 0
    for end, args, espera in testes:
        antes = len(srv.received)
        cli.send(end, *args)
        print(f"  -> {end} {args}")
        fim = time.time() + espera
        while time.time() < fim:
            if len(srv.received) > antes:
                break
            time.sleep(0.05)
        novos = srv.received[antes:]
        for a, v, _src in novos:
            print(f"     <- {a} {v}")
            recebeu += 1
        if not novos:
            print("     <- (sem resposta)")

    print()
    if recebeu:
        print(f"RESULTADO: {recebeu} resposta(s) — o OSC está vivo em :{porta}")
        unicos = sorted({a for a, _, _ in srv.received})
        print("endereços de feedback vistos:", ", ".join(unicos[:20]))
        return 0
    print(f"RESULTADO: nenhuma resposta em :{porta} (servidor não está escutando ou OSC desligado)")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
