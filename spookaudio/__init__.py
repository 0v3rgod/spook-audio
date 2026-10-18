"""SPOOK AUDIO — painel de controle unificado de áudio (Arch + PipeWire).

Camadas:
  alsa      -> controles ALSA via ctypes (hardware, ex: Scarlett)
  scarlett  -> identificação/modelo da Focusrite
  pw        -> grafo do PipeWire (pw-dump/pw-link)
  osc       -> OSC mínimo (fala com Ardour e Carla)
  ardour    -> sessão + transporte
  carla     -> rack/patchbay de plugins
  app       -> interface GTK4
"""

__version__ = "0.1.0"
__all__ = ["alsa", "scarlett", "pw", "osc"]
