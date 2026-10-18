#!/usr/bin/env python3
"""SPOOK AUDIO — entrada do painel.

    ./spook-audio.py            # abre a janela
    ./spook-audio.py --check    # autoteste headless (sem GTK)
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))


def main() -> int:
    if "--check" in sys.argv or "--selftest" in sys.argv:
        from tests.selftest import main as check
        return check()
    from spookaudio.app import main as run
    return run()


if __name__ == "__main__":
    sys.exit(main())
