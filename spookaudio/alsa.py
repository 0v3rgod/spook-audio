"""Controle de elementos ALSA via ctypes (libasound) — sem dependencias externas.

Espelha o que o alsa-scarlett-gui faz (ver RE 01):
  snd_ctl_elem_list / snd_ctl_elem_info / snd_ctl_elem_read / snd_ctl_elem_write
  e, para controles ENUMERATED, snd_ctl_elem_value_get/set_enumerated.

Gerenciamento de memoria: cada tipo de ponteiro tem a sua lista de liberacao
(snd_ctl_elem_id_free / snd_ctl_elem_info_free / snd_ctl_elem_value_free) —
liberar com a funcao errada corrompe o heap (segfault).
"""
from __future__ import annotations

import ctypes
import os
import re
from dataclasses import dataclass, field

_lib = ctypes.CDLL("libasound.so.2", use_errno=True)

CTL_ELEM_IFACE_MIXER = 2
T_BOOLEAN, T_INTEGER, T_ENUMERATED, T_BYTES, T_IEC958, T_INTEGER64 = 1, 2, 3, 4, 5, 6
TYPE_NAMES = {T_BOOLEAN: "boolean", T_INTEGER: "integer", T_ENUMERATED: "enum",
              T_BYTES: "bytes", T_IEC958: "iec958", T_INTEGER64: "int64"}


class AlsaError(RuntimeError):
    pass


def _check(rc: int, what: str) -> int:
    if rc < 0:
        raise AlsaError(f"{what} falhou: {os.strerror(-rc)} (rc={rc})")
    return rc


def _fn(name, argtypes, restype=ctypes.c_int):
    f = getattr(_lib, name)
    f.argtypes = argtypes
    f.restype = restype
    return f


P = ctypes.c_void_p

_ctl_open = _fn("snd_ctl_open", [ctypes.POINTER(P), ctypes.c_char_p, ctypes.c_int])
_ctl_close = _fn("snd_ctl_close", [P])
_list_malloc = _fn("snd_ctl_elem_list_malloc", [ctypes.POINTER(P)])
_list_free = _fn("snd_ctl_elem_list_free", [P])
_list_alloc = _fn("snd_ctl_elem_list_alloc_space", [P, ctypes.c_uint])
_list_free_space = _fn("snd_ctl_elem_list_free_space", [P])
_list_call = _fn("snd_ctl_elem_list", [P, P])
_list_count = _fn("snd_ctl_elem_list_get_count", [P], ctypes.c_uint)
_list_get_id = _fn("snd_ctl_elem_list_get_id", [P, ctypes.c_uint, P])

_id_malloc = _fn("snd_ctl_elem_id_malloc", [ctypes.POINTER(P)])
_id_free = _fn("snd_ctl_elem_id_free", [P])
_id_numid = _fn("snd_ctl_elem_id_get_numid", [P], ctypes.c_uint)
_id_name = _fn("snd_ctl_elem_id_get_name", [P], ctypes.c_char_p)
_id_iface = _fn("snd_ctl_elem_id_get_interface", [P])
_id_index = _fn("snd_ctl_elem_id_get_index", [P], ctypes.c_uint)
_id_device = _fn("snd_ctl_elem_id_get_device", [P], ctypes.c_uint)

_info_malloc = _fn("snd_ctl_elem_info_malloc", [ctypes.POINTER(P)])
_info_free = _fn("snd_ctl_elem_info_free", [P])
_info_set_id = _fn("snd_ctl_elem_info_set_id", [P, P])
_info_call = _fn("snd_ctl_elem_info", [P, P])
_info_get_type = _fn("snd_ctl_elem_info_get_type", [P], ctypes.c_int)
_info_get_count = _fn("snd_ctl_elem_info_get_count", [P], ctypes.c_uint)
_info_get_items = _fn("snd_ctl_elem_info_get_items", [P], ctypes.c_uint)
_info_get_min = _fn("snd_ctl_elem_info_get_min", [P], ctypes.c_long)
_info_get_max = _fn("snd_ctl_elem_info_get_max", [P], ctypes.c_long)
_info_get_step = _fn("snd_ctl_elem_info_get_step", [P], ctypes.c_uint)
_info_set_item = _fn("snd_ctl_elem_info_set_item", [P, ctypes.c_uint])
_info_get_item_name = _fn("snd_ctl_elem_info_get_item_name", [P], ctypes.c_char_p)
_info_is_readable = _fn("snd_ctl_elem_info_is_readable", [P], ctypes.c_int)
_info_is_writable = _fn("snd_ctl_elem_info_is_writable", [P], ctypes.c_int)

_val_malloc = _fn("snd_ctl_elem_value_malloc", [ctypes.POINTER(P)])
_val_free = _fn("snd_ctl_elem_value_free", [P])
_val_set_id = _fn("snd_ctl_elem_value_set_id", [P, P])
_val_read = _fn("snd_ctl_elem_read", [P, P])
_val_write = _fn("snd_ctl_elem_write", [P, P])
_val_get_int = _fn("snd_ctl_elem_value_get_integer", [P, ctypes.c_uint], ctypes.c_long)
_val_set_int = _fn("snd_ctl_elem_value_set_integer", [P, ctypes.c_uint, ctypes.c_long])
_val_get_bool = _fn("snd_ctl_elem_value_get_boolean", [P, ctypes.c_uint], ctypes.c_int)
_val_set_bool = _fn("snd_ctl_elem_value_set_boolean", [P, ctypes.c_uint, ctypes.c_int])
_val_get_enum = _fn("snd_ctl_elem_value_get_enumerated", [P, ctypes.c_uint], ctypes.c_uint)
_val_set_enum = _fn("snd_ctl_elem_value_set_enumerated", [P, ctypes.c_uint, ctypes.c_uint])
_val_get_i64 = _fn("snd_ctl_elem_value_get_integer64", [P, ctypes.c_uint], ctypes.c_longlong)
_val_set_i64 = _fn("snd_ctl_elem_value_set_integer64", [P, ctypes.c_uint, ctypes.c_longlong])


@dataclass
class Element:
    name: str
    iface: int
    index: int
    device: int
    numid: int
    etype: int
    count: int
    minimum: int
    maximum: int
    step: int
    writable: bool
    items: list[str] = field(default_factory=list)
    values: list[int] = field(default_factory=list)
    _id: int | None = None          # snd_ctl_elem_id_t* (mantido para escrita)

    @property
    def type_name(self) -> str:
        return TYPE_NAMES.get(self.etype, f"tipo{self.etype}")

    @property
    def value(self):
        """Valor do primeiro canal, com enum ja traduzido para texto."""
        if not self.values:
            return None
        v = self.values[0]
        if self.etype == T_ENUMERATED and 0 <= v < len(self.items):
            return self.items[v]
        return v

    def __repr__(self) -> str:
        return f"<Element #{self.numid} {self.name!r} {TYPE_NAMES.get(self.etype, '?')}={self.value!r}>"


@dataclass
class Card:
    index: int
    card_id: str
    driver: str
    name: str
    usb_id: str | None = None
    usb_bus: str | None = None

    @property
    def vid_pid(self) -> tuple[str, str] | None:
        if self.usb_id and ":" in self.usb_id:
            vid, pid = self.usb_id.split(":", 1)
            return vid.lower(), pid.lower()
        return None


def list_cards() -> list[Card]:
    """Le /proc/asound/cards (mesma fonte que o alsa-scarlett-gui usa para se achar)."""
    cards: list[Card] = []
    try:
        with open("/proc/asound/cards", encoding="utf-8", errors="replace") as fh:
            text = fh.read()
    except OSError:
        return cards
    for line in text.splitlines():
        m = re.match(r"\s*(\d+)\s+\[([^\]]*)\]\s*:\s*(\S+)\s*-\s*(.*)$", line)
        if not m:
            continue
        idx, cid, drv, name = int(m.group(1)), m.group(2).strip(), m.group(3), m.group(4).strip()
        card = Card(idx, cid, drv, name)
        card.usb_id = _read_proc(idx, "usbid")
        card.usb_bus = _read_proc(idx, "usbbus")
        cards.append(card)
    return cards


def _read_proc(index: int, what: str) -> str | None:
    try:
        with open(f"/proc/asound/card{index}/{what}", encoding="ascii", errors="replace") as fh:
            return fh.read().strip() or None
    except OSError:
        return None


class Ctl:
    """Handle de controle ALSA (hw:N) com listagem/leitura/escrita de elementos."""

    def __init__(self, card: int | str):
        self.card = card
        name = f"hw:{card}" if isinstance(card, int) else str(card)
        self._ctl = P()
        _check(_ctl_open(ctypes.byref(self._ctl), name.encode(), 0), f"snd_ctl_open({name})")
        self._ids: list[int] = []
        self.elements: list[Element] = []

    # ---------------------------------------------------------------- leitura
    def refresh(self, only_writable: bool = False) -> list[Element]:
        lst = P()
        _check(_list_malloc(ctypes.byref(lst)), "elem_list_malloc")
        try:
            _check(_list_call(self._ctl, lst), "elem_list(contagem)")
            count = _list_count(lst)
            _check(_list_alloc(lst, count), "elem_list_alloc_space")
            _check(_list_call(self._ctl, lst), "elem_list(dados)")
            out: list[Element] = []
            for i in range(count):
                idp = P()
                if _lib.snd_ctl_elem_id_malloc(ctypes.byref(idp)) < 0:
                    continue
                _check(_lib.snd_ctl_elem_list_get_id(lst, i, idp), "elem_list_get_id")
                el = self._describe(idp)
                if el is None:
                    _lib.snd_ctl_elem_id_free(idp)
                    continue
                if only_writable and not el.writable:
                    _lib.snd_ctl_elem_id_free(idp)
                    continue
                el._id = idp
                self._ids.append(idp)
                el.values = self._read_values(idp, el)
                out.append(el)
            self.elements = out
            return out
        finally:
            _lib.snd_ctl_elem_list_free_space(lst)
            _lib.snd_ctl_elem_list_free(lst)

    def _describe(self, idp) -> Element | None:
        info = P()
        if _lib.snd_ctl_elem_info_malloc(ctypes.byref(info)) < 0:
            return None
        try:
            _check(_info_set_id(info, idp), "elem_info_set_id")
            if _lib.snd_ctl_elem_info(self._ctl, info) < 0:
                return None
            el = Element(
                name=(_lib.snd_ctl_elem_id_get_name(idp) or b"").decode("utf-8", "replace"),
                iface=_lib.snd_ctl_elem_id_get_interface(idp),
                index=_lib.snd_ctl_elem_id_get_index(idp),
                device=_lib.snd_ctl_elem_id_get_device(idp),
                numid=_lib.snd_ctl_elem_id_get_numid(idp),
                etype=_info_get_type(info),
                count=_info_get_count(info),
                minimum=_info_get_min(info),
                maximum=_info_get_max(info),
                step=_info_get_step(info),
                writable=bool(_info_is_writable(info)),
            )
            if el.etype == T_ENUMERATED:
                for k in range(_info_get_items(info)):
                    _info_set_item(info, k)
                    nm = _info_get_item_name(info)
                    el.items.append((nm or b"").decode("utf-8", "replace"))
            return el
        finally:
            _lib.snd_ctl_elem_info_free(info)

    def _read_values(self, idp, el: Element) -> list[int]:
        val = P()
        if _lib.snd_ctl_elem_value_malloc(ctypes.byref(val)) < 0:
            return [0] * el.count
        try:
            _check(_val_set_id(val, idp), "value_set_id")
            if _val_read(self._ctl, val) < 0:
                return [0] * el.count
            if el.etype == T_ENUMERATED:
                get = _val_get_enum
            elif el.etype == T_BOOLEAN:
                get = _val_get_bool
            elif el.etype == T_INTEGER64:
                get = _val_get_i64
            else:
                get = _val_get_int
            return [get(val, i) for i in range(el.count)]
        finally:
            _lib.snd_ctl_elem_value_free(val)

    def read(self, el: Element) -> list[int]:
        el.values = self._read_values(el._id, el)
        return el.values

    # ---------------------------------------------------------------- escrita
    def set_value(self, el: Element, index: int, value) -> None:
        if not el.writable:
            raise AlsaError(f"controle somente-leitura: {el.name}")
        if not 0 <= index < el.count:
            raise AlsaError(f"indice {index} fora do range de {el.name} ({el.count} canais)")
        val = P()
        _check(_val_malloc(ctypes.byref(val)), "value_malloc")
        try:
            _check(_val_set_id(val, el._id), "value_set_id")
            # le primeiro para preservar os outros canais do mesmo controle
            if _val_read(self._ctl, val) < 0:
                for i in range(el.count):
                    el.values[i] = el.values[i] if i < len(el.values) else 0
            if el.etype == T_ENUMERATED:
                for i in range(el.count):
                    _val_set_enum(val, i, int(el.values[i]))
                _val_set_enum(val, index, int(value))
            elif el.etype == T_BOOLEAN:
                for i in range(el.count):
                    _val_set_bool(val, i, int(bool(el.values[i])))
                _val_set_bool(val, index, 1 if value else 0)
            elif el.etype == T_INTEGER64:
                for i in range(el.count):
                    _val_set_i64(val, i, int(el.values[i]))
                _val_set_i64(val, index, int(value))
            else:
                for i in range(el.count):
                    _val_set_int(val, i, int(el.values[i]))
                _val_set_int(val, index, int(value))
            _check(_val_write(self._ctl, val), f"snd_ctl_elem_write({el.name})")
            el.values[index] = int(value) if el.etype != T_BOOLEAN else int(bool(value))
        finally:
            _lib.snd_ctl_elem_value_free(val)

    def set_all(self, el: Element, values) -> None:
        for i, v in enumerate(values):
            if i < el.count:
                self.set_value(el, i, v)

    def find(self, name: str, index: int = 0) -> Element | None:
        for el in self.elements:
            if el.name == name and el.index == index:
                return el
        return None

    def close(self) -> None:
        for ptr in self._ids:
            _lib.snd_ctl_elem_id_free(ptr)
        self._ids.clear()
        if self._ctl:
            _ctl_close(self._ctl)
            self._ctl = P()

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()


if __name__ == "__main__":  # teste rapido: python3 -m spookaudio.alsa
    for c in list_cards():
        print(f"card {c.index}: [{c.card_id}] {c.name}  ({c.driver}) usb={c.usb_id}")
    cards = list_cards()
    if cards:
        with Ctl(cards[0].index) as cl:
            els = cl.refresh()
            print(f"\n{len(els)} controles no card {cards[0].index}:")
            for el in els[:25]:
                print(f"  {el.numid:>4} {el.name[:34]:<34} {el.type_name:<8} {v if (v:=el.value) is not None else ''}")
