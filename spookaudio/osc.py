"""OSC (Open Sound Control) mínimo — cliente + servidor, sem dependencias externas.

Implementa o essencial do spec 1.0: endereco, typetag string, int32 (i), float32 (f)
e string (s), com padding de 4 bytes. Suficiente para falar com Ardour e Carla.
"""
from __future__ import annotations

import socket
import struct
import threading

__all__ = ["encode", "decode", "Client", "Server"]


def _pad_impl(b: bytes) -> bytes:
    """Preenche com zeros ate multiplo de 4 (regra do OSC)."""
    rem = len(b) % 4
    return b if rem == 0 else b + b"\x00" * (4 - rem)


def encode(address: str, *args) -> bytes:
    """Monta uma mensagem OSC (sem bundle)."""
    if not address.startswith("/"):
        raise ValueError(f"endereco OSC precisa comecar com '/': {address!r}")
    out = _pad_impl(address.encode("utf-8") + b"\x00")
    tags = ","
    body = b""
    for a in args:
        if isinstance(a, bool):            # antes de int (bool e subclasse)
            tags += "i"
            body += struct.pack(">i", 1 if a else 0)
        elif isinstance(a, int):
            tags += "i"
            body += struct.pack(">i", a)
        elif isinstance(a, float):
            tags += "f"
            body += struct.pack(">f", a)
        elif isinstance(a, str):
            tags += "s"
            body += _pad_impl(a.encode("utf-8") + b"\x00")
        else:
            raise TypeError(f"tipo OSC nao suportado: {type(a).__name__}")
    return out + _pad_impl(tags.encode("ascii") + b"\x00") + body


def decode(data: bytes):
    """Devolve (endereco, [argumentos]). Suporta i, f, s, T, F."""
    if data.startswith(b"#bundle"):
        raise ValueError("bundles OSC ainda nao suportados")
    i = data.index(b"\x00")
    address = data[:i].decode("utf-8", "replace")
    i = (i + 4) & ~3
    if i >= len(data) or data[i:i + 1] != b",":
        return address, []
    j = data.index(b"\x00", i)
    tags = data[i + 1:j].decode("ascii", "replace")
    i = (j + 4) & ~3
    args = []
    for t in tags:
        if t == "i":
            args.append(struct.unpack(">i", data[i:i + 4])[0]); i += 4
        elif t == "f":
            args.append(struct.unpack(">f", data[i:i + 4])[0]); i += 4
        elif t == "s":
            j = data.index(b"\x00", i)
            args.append(data[i:j].decode("utf-8", "replace")); i = (j + 4) & ~3
        elif t == "T":
            args.append(True)
        elif t == "F":
            args.append(False)
        else:
            raise ValueError(f"tag OSC nao suportada: {t!r}")
    return address, args


class Client:
    """Cliente OSC simples (UDP).

    Importante (RE 03): o OSC do Ardour responde para o endereco de ORIGEM da
    mensagem. Como este socket e' quem envia, e ELE que recebe o feedback — por
    isso `bind_local()` + `Server(sock=...)` (via `feedback_server()`), e nao um
    socket separado, que nunca receberia nada.
    """

    def __init__(self, host: str = "127.0.0.1", port: int = 3819, timeout: float = 1.0):
        self.host, self.port = host, port
        self.timeout = timeout
        self._sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

    def bind_local(self, host: str = "0.0.0.0") -> int:
        """Fixa a porta de origem (0 = efemera) e devolve a porta usada."""
        self._sock.bind((host, 0))
        return self._sock.getsockname()[1]

    @property
    def local_port(self) -> int:
        try:
            return self._sock.getsockname()[1]
        except OSError:
            return 0

    def servidor(self, on_message=None) -> "Server":
        """Um Server que escuta NO MESMO socket deste cliente (o feedback do Ardour)."""
        return Server(on_message=on_message, sock=self._sock)

    def send(self, address: str, *args) -> None:
        self._sock.sendto(encode(address, *args), (self.host, self.port))

    def send_raw(self, payload: bytes) -> None:
        self._sock.sendto(payload, (self.host, self.port))

    def wait(self) -> tuple[str, list] | None:
        """Espera uma resposta (usado para heartbeat/feedback)."""
        self._sock.settimeout(self.timeout)
        try:
            data, _ = self._sock.recvfrom(65535)
        except socket.timeout:
            return None
        return decode(data)

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()

    def close(self):
        try:
            self._sock.close()
        except OSError:
            pass


class Server:
    """Servidor OSC em thread (recebe feedback, ex: /transport_frame, /record_enabled).

    Pode adotar um socket existente (sock=...) — é assim que o feedback do Ardour
    chega, já que ele responde para o endereço de origem do Cliente.
    """

    def __init__(self, port: int = 0, host: str = "0.0.0.0", on_message=None,
                 sock: "socket.socket | None" = None):
        self._sock = sock or socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self._dono = sock is None
        if self._dono:
            self._sock.bind((host, port))
        self._proprio = sock is None
        self._sock.settimeout(0.4)
        self.port = self._sock.getsockname()[1]
        self.on_message = on_message
        self._stop = threading.Event()
        self._thread: threading.Thread | None = None
        self.received: list[tuple[str, list, tuple]] = []

    def start(self):
        self._thread = threading.Thread(target=self._loop, name="osc-server", daemon=True)
        self._thread.start()
        return self

    def _loop(self):
        while not self._stop.is_set():
            try:
                data, addr = self._sock.recvfrom(65535)
            except socket.timeout:
                continue
            except OSError:
                break
            try:
                address, args = decode(data)
            except Exception:
                continue
            self.received.append((address, args, addr))
            if self.on_message:
                try:
                    self.on_message(address, args, addr)
                except Exception:
                    pass

    def stop(self):
        self._stop.set()
        if self._thread:
            self._thread.join(timeout=2)
        if self._dono:                      # socket emprestado nao se fecha
            try:
                self._sock.close()
            except OSError:
                pass
