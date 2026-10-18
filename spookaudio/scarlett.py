"""Identificacao e agrupamento dos controles da Focusrite Scarlett.

A tabela VID:PID -> modelo foi extraida do driver do kernel na RE 01
(re/kernel/mixer_scarlett2.c), que e a mesma usada pelo alsa-scarlett-gui
(que descobre a placa lendo /proc/asound/cardN/usbid).
"""
from __future__ import annotations

import re
from dataclasses import dataclass, field

from . import alsa

FOCUSRITE_VID = "1235"

# pid -> (nome, geracao)
MODELS: dict[str, tuple[str, str]] = {
    # ---- Scarlett 1st Gen (USB) -------------------------------------------
    "8002": ("Scarlett 2i2 1st Gen", "gen1"),
    "8004": ("Scarlett 2i4 1st Gen", "gen1"),
    "8010": ("Scarlett 6i6 1st Gen", "gen1"),
    "800c": ("Scarlett 8i6 1st Gen", "gen1"),
    "8012": ("Scarlett 18i6 1st Gen", "gen1"),
    "800e": ("Scarlett 18i8 1st Gen", "gen1"),
    "8014": ("Scarlett 18i20 1st Gen", "gen1"),
    # 2nd Gen
    "8203": ("Scarlett 6i6 2nd Gen", "gen2"),
    "8204": ("Scarlett 18i8 2nd Gen", "gen2"),
    "8201": ("Scarlett 18i20 2nd Gen", "gen2"),
    # 3rd Gen
    "8211": ("Scarlett Solo 3rd Gen", "gen3"),
    "8210": ("Scarlett 2i2 3rd Gen", "gen3"),
    "8212": ("Scarlett 4i4 3rd Gen", "gen3"),
    "8213": ("Scarlett 8i6 3rd Gen", "gen3"),
    "8214": ("Scarlett 18i8 3rd Gen", "gen3"),
    "8215": ("Scarlett 18i20 3rd Gen", "gen3"),
    # 4th Gen
    "8218": ("Scarlett Solo 4th Gen", "gen4"),
    "8219": ("Scarlett 2i2 4th Gen", "gen4"),
    "821a": ("Scarlett 4i4 4th Gen", "gen4"),
    # Clarett USB / Clarett+
    "8206": ("Clarett 2Pre (USB)", "clarett"),
    "8207": ("Clarett 4Pre (USB)", "clarett"),
    "8208": ("Clarett 8Pre (USB)", "clarett"),
    "820a": ("Clarett+ 2Pre", "clarett"),
    "820b": ("Clarett+ 4Pre", "clarett"),
    "820c": ("Clarett+ 8Pre", "clarett"),
    # Vocaster
    "8217": ("Vocaster One", "vocaster"),
    "8216": ("Vocaster Two", "vocaster"),
}

# Recursos por modelo — so o Solo 4th Gen foi extraido em detalhe na RE 01
# (struct scarlett2_solo_gen4_info). Nos outros modelos o app usa o que o
# proprio mixer ALSA expoe.
FEATURES: dict[str, dict] = {
    "8218": {
        "level": 1,          # Line/Inst por canal
        "air": 1,
        "phantom": 1,
        "direct_monitor": 1,
        "dsp_count": 2,
        "ports": {"analogue": {"outputs": 2, "inputs": 2},
                  "pcm": {"inputs": 1, "outputs": 0},
                  "mixer": {"inputs": 6, "outputs": 2}},
        "config_sets": {"2115": "firmware antigo", "2417": "firmware atual"},
    },
}

FOCUSRITE_VENDOR_NAME = "Focusrite"

# Agrupamento para a UI (ordem importa)
GRUPOS = [
    ("Entrada / Ganho", ("gain", "autogain")),
    ("Air", ("air",)),
    ("Alimentacao phantom", ("phantom",)),
    ("Direct Monitor", ("direct monitor",)),
    ("Saida / Monitor", ("monitor", "level", "mute", "dim")),
    ("Roteamento (mixer)", ("mix", "route", "pcm", "source", "output")),
    ("Medidores", ("meter", "level meter")),
]


def identify(usb_id: str | None) -> tuple[str, str] | None:
    """'1235:8218' -> ('Scarlett Solo 4th Gen', 'gen4')."""
    if not usb_id or ":" not in usb_id:
        return None
    vid, pid = (p.lower() for p in usb_id.split(":", 1))
    if vid != "1235":
        return None
    return MODELS.get(pid) or (f"Focusrite {vid}:{pid} (modelo nao mapeado)", "desconhecida")


def find(cards=None) -> list[dict]:
    """Devolve as placas Focusrite presentes, com modelo/recursos."""
    from . import alsa as _alsa

    encontrados = []
    for card in (cards if cards is not None else _alsa.list_cards()):
        info = identify(card.usb_id)
        if not info:
            continue
        modelo, geracao = info
        pid = card.usb_id.split(":")[1].lower() if card.usb_id else ""
        encontrados.append({
            "card": card,
            "modelo": modelo,
            "geracao": geracao,
            "usb_id": card.usb_id,
            "features": FEATURES.get(pid, {}),
        })
    return encontrados


def grupo(name: str) -> str:
    """Categoria amigavel de um controle do mixer."""
    low = name.lower()
    for titulo, chaves in GRUPOS:
        for k in chaves:
            if k in low:
                return titulo
    return "Outros"


def friendly(name: str) -> str:
    """Deixa o nome do controle legivel em portugues."""
    n = name
    subs = [
        ("Capture Volume", "volume"),
        ("Capture Switch", "liga/desliga"),
        ("Capture Enum", "seletor"),
        ("Playback Volume", "volume de saida"),
        ("Playback Switch", "liga/desliga (saida)"),
        ("Playback Enum", "seletor de saida"),
        ("Switch", "chave"),
        ("Enum", "seletor"),
        ("Volume", "volume"),
    ]
    for a, b in subs:
        if n.endswith(a):
            n = n[: -len(a)].strip() + " — " + b
            break
    return n.strip()


def resumo(dev: dict) -> str:
    c = dev["card"]
    extra = ""
    if dev["features"]:
        f = dev["features"]
        extra = (f" · phantom={'sim' if f.get('phantom') else 'nao'}"
                 f" · air={'sim' if f.get('air') else 'nao'}"
                 f" · dsp={f.get('dsp_count', 0)}")
    return f"{dev['modelo']} (card {c.index}, usb {dev['usb_id']}){extra}"


if __name__ == "__main__":
    for d in find():
        print(resumo(d))
    if not find():
        print("nenhuma Scarlett plugada agora")
