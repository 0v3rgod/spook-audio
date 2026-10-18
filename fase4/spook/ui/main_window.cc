/*
 * SPOOK AUDIO — janela principal (gtkmm-3.0)
 *
 * Aqui mora SÓ a UI. O motor (libardour) está em core/engine.cc e o mixer do
 * hardware em core/scarlett.cc — nenhum header de toolkit vaza para eles.
 *
 * O visual imita uma DAW de verdade: barra de transporte com botões redondos e
 * LEDs, abas com sublinhado de acento, "channel strips" com fader vertical para
 * o mixer do hardware, medidor de CPU real e barra de status com readouts.
 * Todo o tema está em kCss (abaixo), aplicado por cima do tema do sistema.
 */

#include "ui/main_window.h"
#include "core/log.h"
#include "core/plugins.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <ctime>
#include <fstream>
#include <sstream>

#include <glibmm.h>
#include <cairomm/context.h>
#include <gdk/gdkkeysyms.h>

namespace spook {

namespace {

/* ===================================================================== TEMA
 *
 * Paleta (inspirada num estúdio à noite):
 *   fundo      #0b0d12   painel   #101420   cartão  #161b26
 *   borda      #232a3a   texto    #e6e9ef   fraco   #7f8798
 *   marca      #ff3d8b (rosa SPOOK)   acento 2 #34e1e1 (ciano)
 *   ligado     #25e08a   alerta   #ffb020   erro    #ff5566
 */
const char* const kCss = R"CSS(
* {
  font-family: "JetBrains Mono", "JetBrainsMono NF", "DejaVu Sans Mono", monospace;
  font-size: 12px;
}
window.spook-window { background-color: #0b0d12; }
.spook-root { background-color: #0b0d12; color: #e6e9ef; }
.spook-glyph { font-family: "DejaVu Sans", "Symbola", sans-serif; font-size: 14px; }

/* ------------------------------------------------------------- cabeçalho */
.spook-header { background-color: #101420; border-bottom: 1px solid #232a3a; padding: 10px 14px; }
.spook-logo { color: #ff3d8b; font-size: 20px; }
.spook-title { color: #f4f6fa; font-size: 16px; font-weight: 700; }
.spook-sub { color: #7f8798; font-size: 10px; }
.spook-ruler { color: #4a5163; font-size: 10px; letter-spacing: 2px; }
.spook-clock { color: #e6e9ef; font-size: 15px; font-weight: 700; }

/* LED/chip de estado */
.spook-led {
  font-size: 9px; font-weight: 700; letter-spacing: 1px;
  padding: 3px 9px; border-radius: 10px;
  background-color: #141926; color: #5f6779; border: 1px solid #232a3a;
}
.spook-led.on   { background-color: #25e08a; color: #06140d; border-color: #25e08a; }
.spook-led.warn { background-color: #ffb020; color: #1a1200; border-color: #ffb020; }
.spook-led.err  { color: #ff5566; border-color: #ff5566; background-color: #1b1116; }
.spook-led.off  { color: #5f6779; }

/* ------------------------------------------------------------ transporte */
.spook-transport { background-color: #0d111a; border-bottom: 1px solid #232a3a; padding: 9px 14px; }
button.spook-tbtn {
  min-width: 40px; min-height: 40px; padding: 0;
  border-radius: 20px; border: 1px solid #2b3346;
  background-image: none; background-color: #1a2030; color: #cfd6e2;
  box-shadow: none;
}
button.spook-tbtn:hover { background-color: #232b3f; border-color: #3b455e; color: #ffffff; }
button.spook-tbtn:active { background-color: #10141d; }
button.spook-tbtn:disabled { background-color: #12161f; color: #454c5c; border-color: #1d2331; }
button.spook-tbtn.play { background-color: #25e08a; color: #06140d; border-color: #25e08a; }
button.spook-tbtn.play:hover { background-color: #4beba3; color: #06140d; }
button.spook-tbtn.armed { background-color: #ff3d8b; color: #16050d; border-color: #ff5ea0; }
button.spook-tbtn.stop { background-color: #2a3346; color: #e6e9ef; }
button.spook-tbtn.rec  { background-color: #3a1620; color: #ff5566; border-color: #ff5566; }
button.spook-tbtn.panic { background-color: #1a2030; color: #ffb020; border-color: #4a3a17; }

/* readout de transporte (label pequeno em cima, valor em destaque) */
.spook-readout { background-color: #0a0d14; border: 1px solid #232a3a; border-radius: 6px; padding: 3px 10px; }
.spook-readout .rl { color: #667089; font-size: 9px; letter-spacing: 1px; }
.spook-readout .rv { color: #d7dce6; font-size: 12px; font-weight: 700; }
.spook-readout.acc .rv { color: #34e1e1; }

/* --------------------------------------------------------------- cartões */
.spook-card { background-color: #161b26; border: 1px solid #232a3a; border-radius: 10px; padding: 12px; }
.spook-card.tight { padding: 8px; }
.spook-card-title { color: #34e1e1; font-size: 10px; font-weight: 700; letter-spacing: 1px; }
.spook-strong { color: #e6e9ef; font-size: 12px; font-weight: 700; }
.spook-dim { color: #7f8798; font-size: 11px; }
.spook-ok { color: #25e08a; }
.spook-warn { color: #ffb020; }
.spook-err { color: #ff5566; }
.spook-cyan { color: #34e1e1; }
.spook-pink { color: #ff3d8b; }

/* ------------------------------------------------------- abas (notebook) */
notebook.spook-tabs { background-color: transparent; border: none; }
notebook.spook-tabs > header { background-color: #0d111a; border-bottom: 1px solid #232a3a; padding: 0 6px; }
notebook.spook-tabs > header > tabs > tab {
  padding: 10px 18px; color: #7f8798; background-color: transparent;
  border: none; border-bottom: 2px solid transparent; border-radius: 0;
  box-shadow: none;
}
notebook.spook-tabs > header > tabs > tab:hover { color: #e6e9ef; }
notebook.spook-tabs > header > tabs > tab:checked { color: #ff3d8b; border-bottom: 2px solid #ff3d8b; }
notebook.spook-tabs > stack { background-color: #0b0d12; padding: 0; }

/* --------------------------------------------------------- channel strip */
.spook-strip {
  background-color: #121722; border: 1px solid #232a3a; border-radius: 8px;
  padding: 6px 4px;
}
.spook-strip-name {
  color: #8b93a5; font-size: 9px; font-weight: 700; letter-spacing: 0;
}
.spook-strip-name.hot { color: #34e1e1; }
.spook-strip-val { color: #d7dce6; font-size: 11px; font-weight: 700; }
.spook-strip-unit { color: #667089; font-size: 9px; }
.spook-strip.ro { border-color: #1d2331; }
.spook-strip.ro .spook-strip-name { color: #5f6779; }
.spook-strip.ro .spook-strip-val { color: #6b7486; }
.spook-strip.arm { border-color: #33405a; }
.spook-strip.arm .spook-strip-val { color: #34e1e1; }
.spook-badge {
  font-size: 9px; font-weight: 700; padding: 2px 7px; border-radius: 6px;
  background-color: #1b2130; color: #8b93a5; border: 1px solid #232a3a;
}
.spook-badge.lv2  { color: #34e1e1; border-color: #1f5b5b; background-color: #0f1f22; }
.spook-badge.ladspa { color: #b98bff; border-color: #4a3a6b; background-color: #171226; }
.spook-badge.vst  { color: #ff9f43; border-color: #5b3d1a; background-color: #211609; }
.spook-badge.lua  { color: #25e08a; border-color: #1f5b3f; background-color: #0d1f16; }
.spook-badge.other { color: #9aa3b5; }

/* --------------------------------------------------------- faders/botões */
scale.trough-cyn trough { background-color: #0a0d14; border: 1px solid #232a3a; border-radius: 4px; min-width: 6px; }
scale.trough-cyn highlight { background-color: #34e1e1; border-radius: 4px; }
scale.trough-cyn slider {
  min-width: 26px; min-height: 12px;
  background-image: none; background-color: #dfe4ee;
  border: 1px solid #6b748a; border-radius: 3px;
}
scale:disabled highlight { background-color: #2a3143; }
scale:disabled slider { background-color: #59617a; }

/* medidor de CPU */
levelbar trough { background-color: #0a0d14; border: 1px solid #232a3a; border-radius: 3px; padding: 1px; }
levelbar block.filled { background-color: #34e1e1; border: none; border-radius: 2px; min-height: 8px; }
levelbar.discrete block.filled { background-color: #34e1e1; }

button.spook-btn {
  background-image: none; background-color: #1a2030; color: #d7dce6;
  border: 1px solid #2b3346; border-radius: 6px; padding: 6px 12px;
  font-size: 11px; font-weight: 700; letter-spacing: 1px; box-shadow: none;
}
button.spook-btn:hover { background-color: #232b3f; border-color: #3b455e; color: #fff; }
button.spook-btn:active { background-color: #10141d; }
button.spook-btn.accent { background-color: #ff3d8b; color: #16050d; border-color: #ff3d8b; }
button.spook-btn.accent:hover { background-color: #ff5ea0; color: #16050d; }
button.spook-btn.cyan { background-color: #34e1e1; color: #04161a; border-color: #34e1e1; }
button.spook-btn.cyan:hover { background-color: #63ecec; color: #04161a; }
button.spook-btn.danger { background-color: #3a1620; color: #ff5566; border-color: #ff5566; }
button.spook-btn.danger:hover { background-color: #571f2c; }
/* estado LIGADO (Gtk::ToggleButton ativo): tem que ser inconfundível */
button.spook-btn:checked { background-color: #ff3d8b; color: #16050d; border-color: #ff5ea0; }
button.spook-btn.danger:checked { background-color: #ff5566; color: #1b0a0e; border-color: #ff8a95; }

entry.spook-entry {
  background-image: none; background-color: #0a0d14; color: #e6e9ef;
  border: 1px solid #232a3a; border-radius: 6px; padding: 6px 10px; box-shadow: none;
}
entry.spook-entry:focus { border-color: #ff3d8b; }
combobox.spook-combo button {
  background-image: none; background-color: #0a0d14; color: #e6e9ef;
  border: 1px solid #232a3a; border-radius: 6px; box-shadow: none;
}
combobox.spook-combo button:hover { border-color: #3b455e; }

/* --------------------------------------------------------------- rodapé */
.spook-statusbar { background-color: #0d111a; border-top: 1px solid #232a3a; padding: 5px 12px; }
.spook-status { color: #a8b0c0; font-size: 11px; }
.spook-status.error { color: #ff5566; }
.spook-status-right { color: #667089; font-size: 10px; }
.spook-ver { color: #4a5163; font-size: 10px; }

/* ------------------------------------------------------------ listagens */
.spook-console { background-color: #070a0f; color: #b9c2d0; }
.spook-console text { background-color: #070a0f; color: #b9c2d0; }
.spook-row { padding: 4px 2px; border-bottom: 1px solid #161b26; }
.spook-row-idx { color: #4a5163; font-size: 10px; }
entry.route-name-entry { font-weight: 700; padding: 3px 6px; }
/* Arrange View: régua/lane são cairo para o playhead atualizar sem widgets
 * por bloco; o canvas continua responsivo até em sessões com muitas faixas. */
.arrange-canvas { background-color: #07090e; border: 1px solid #232a3a; border-radius: 6px; }
.spook-empty { color: #5f6779; font-size: 11px; }
scrollbar trough { background-color: transparent; border: none; }
scrollbar slider { background-color: #2a3143; border-radius: 6px; min-width: 7px; min-height: 7px; }
scrollbar slider:hover { background-color: #3b455e; }

/* ------------------------------------------------------- LCD do transporte */
.spook-lcd {
  background-color: #05070a; border: 1px solid #1b2530; border-radius: 6px;
  padding: 3px 12px;
}
.spook-lcd .ll { color: #4d5a6b; font-size: 9px; letter-spacing: 1px; }
.spook-lcd .lv { color: #9fb0c0; font-size: 16px; font-weight: 700; }
.spook-lcd.acc .lv { color: #34e1e1; }
.spook-lcd.pink .lv { color: #ff5ea0; }
.spook-lcd.warn .lv { color: #ffb020; }
.spook-lcd.big .lv { font-size: 21px; }

/* -------------------------------------------------- toolbar estilo Reaper */
.spook-toolbar { background-color: #0a0e16; border-bottom: 1px solid #1b2530; padding: 6px 12px; }
button.spook-tool {
  background-image: none; background-color: #141b28; color: #b9c2d0;
  border: 1px solid #22303f; border-radius: 4px; padding: 5px 10px;
  font-size: 10px; font-weight: 700; letter-spacing: 0.5px; box-shadow: none;
}
button.spook-tool:hover { background-color: #1d2637; border-color: #33465c; color: #fff; }
button.spook-tool:active { background-color: #0d131d; }
button.spook-tool:checked { background-color: #ff3d8b; color: #16050d; border-color: #ff5ea0; }
button.spook-tool.accent { color: #34e1e1; border-color: #1f4048; }
button.spook-tool.danger { color: #ff5566; border-color: #4a2029; }
.spook-tool-sep { color: #26313f; }

/* --------------------------------------------------- marcadores / regiões */
.spook-mark-row { border-bottom: 1px solid #161d29; padding: 3px 2px; }
.spook-mark-kind { color: #ffb020; font-size: 9px; font-weight: 700; letter-spacing: 0.5px; }
.spook-mark-kind.loop { color: #34e1e1; }
.spook-mark-kind.range { color: #b98bff; }
.spook-mark-pos  { color: #34e1e1; font-size: 10px; font-weight: 700; }
.spook-mark-name { color: #c8d2e0; font-size: 11px; }
button.spook-go {
  min-width: 18px; min-height: 16px; padding: 0 4px;
  background-image: none; background-color: #14202c; color: #34e1e1;
  border: 1px solid #1f4048; border-radius: 3px; font-size: 9px; box-shadow: none;
}
button.spook-go:hover { background-color: #1d2e3d; color: #fff; }

/* ------------------------------------------------------------ mixer (tiras) */
.mixer-strip {
  background-color: #101620; border: 1px solid #22303f; border-radius: 6px;
  padding: 6px 4px;
}
.mixer-strip.master { background-color: #17111c; border-color: #4a2440; }
.mixer-strip.armed  { border-color: #7a2440; }
.mixer-name { color: #c8d2e0; font-size: 10px; font-weight: 700; }
.mixer-strip.master .mixer-name { color: #ff8ab8; }
/* Track Header editável: não é label decorativo, é um nome real de Route. */
entry.mixer-name-entry { min-height: 18px; padding: 2px 3px; font-size: 10px; font-weight: 700; }
.mixer-strip.master entry.mixer-name-entry { color: #ff8ab8; }
button.track-color { min-width: 16px; min-height: 16px; padding: 0; border-radius: 4px; }
button.track-color colorswatch { border-radius: 3px; }
.mixer-badge {
  font-size: 8px; font-weight: 700; letter-spacing: 0.5px;
  padding: 1px 5px; border-radius: 4px;
  background-color: #1b2130; color: #8b93a5; border: 1px solid #232a3a;
}
.mixer-badge.master { color: #ff8ab8; border-color: #5a2a48; background-color: #241322; }
.mixer-badge.midi   { color: #b98bff; border-color: #43356b; background-color: #171226; }
.mixer-db { color: #34e1e1; font-size: 10px; font-weight: 700; }
.mixer-pan { color: #8b93a5; font-size: 9px; }
button.spook-ms {
  min-width: 20px; min-height: 18px; padding: 0 3px;
  background-image: none; background-color: #171f2c; color: #7f8798;
  border: 1px solid #22303f; border-radius: 3px;
  font-size: 9px; font-weight: 700; box-shadow: none;
}
button.spook-ms:hover { border-color: #3b455e; color: #fff; }
button.spook-ms.mute:checked { background-color: #ffb020; color: #1a1200; border-color: #ffb020; }
button.spook-ms.solo:checked { background-color: #34e1e1; color: #04161a; border-color: #34e1e1; }
button.spook-ms.arm:checked  { background-color: #ff3d8b; color: #16050d; border-color: #ff3d8b; }
scale.mixer-fader trough { background-color: #05070a; border: 1px solid #1b2530; border-radius: 3px; min-width: 5px; }
scale.mixer-fader highlight { background-color: #2b6f8f; border-radius: 3px; }
scale.mixer-fader slider {
  min-width: 30px; min-height: 13px;
  background-image: none; background-color: #cfd8e6;
  border: 1px solid #8794a8; border-radius: 3px;
}
scale.mixer-pan trough { background-color: #05070a; border: 1px solid #1b2530; border-radius: 3px; min-height: 5px; }
scale.mixer-pan highlight { background-color: #6b7486; }
scale.mixer-pan slider {
  min-width: 9px; min-height: 13px;
  background-image: none; background-color: #9fb0c0;
  border: 1px solid #6b7486; border-radius: 3px;
}

/* ------------------------------------------------- action list do Reaper */
treeview.spook-actions { background-color: #070a0f; color: #c8d2e0; }
treeview.spook-actions:selected { background-color: #1d2637; color: #ffffff; }
treeview.spook-actions header button {
  background-image: none; background-color: #101620; color: #7f8798;
  border: none; border-bottom: 1px solid #22303f; border-radius: 0;
  padding: 4px 8px; font-size: 10px; font-weight: 700; box-shadow: none;
}
treeview.spook-actions header button:hover { color: #e6e9ef; }
)CSS";

/* ---------------------------------------------------------------- helpers */

void
install_theme ()
{
	Glib::RefPtr<Gtk::CssProvider> css = Gtk::CssProvider::create ();
	css->load_from_data (kCss);
	Gtk::StyleContext::add_provider_for_screen (Gdk::Screen::get_default (), css,
	                                            GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
}

void
cls (Gtk::Widget& w, const char* c)
{
	w.get_style_context ()->add_class (c);
}

Gtk::Label*
label (std::string const& text, const char* c = nullptr)
{
	Gtk::Label* l = Gtk::manage (new Gtk::Label (text));
	l->set_xalign (0.0f);
	if (c) { cls (*l, c); }
	return l;
}

Gtk::Label*
strong (std::string const& text)
{
	return label (text, "spook-strong");
}

Gtk::Label*
dim (std::string const& text)
{
	return label (text, "spook-dim");
}

Gtk::Label*
card_title (std::string const& text)
{
	return label (text, "spook-card-title");
}

Gtk::Box*
box (Gtk::Orientation o, int spacing = 8, int margin = 0)
{
	Gtk::Box* b = Gtk::manage (new Gtk::Box (o, spacing));
	if (margin) {
		b->set_margin_top (margin);
		b->set_margin_bottom (margin);
		b->set_margin_start (margin);
		b->set_margin_end (margin);
	}
	return b;
}

Gtk::Box*
card (std::string const& titulo, int spacing = 8)
{
	Gtk::Box* c = box (Gtk::ORIENTATION_VERTICAL, spacing);
	cls (*c, "spook-card");
	if (!titulo.empty ()) { c->pack_start (*card_title (titulo), false, false, 0); }
	return c;
}

Gtk::ScrolledWindow*
scrolled (Gtk::Widget& child, Gtk::PolicyType h = Gtk::POLICY_AUTOMATIC)
{
	Gtk::ScrolledWindow* s = Gtk::manage (new Gtk::ScrolledWindow);
	s->add (child);
	s->set_vexpand (true);
	s->set_policy (h, Gtk::POLICY_AUTOMATIC);
	return s;
}

/* gtkmm3 nao tem get_first_child(): esvaziar e listar os filhos. */
void
clear (Gtk::Container& c)
{
	std::vector<Gtk::Widget*> kids = c.get_children ();
	for (size_t i = 0; i < kids.size (); ++i) { c.remove (*kids[i]); }
}

/* LED/chip de estado: 'on' | 'off' | 'warn' | 'err' */
Gtk::Label*
led (std::string const& text, const char* state = "off")
{
	Gtk::Label* l = Gtk::manage (new Gtk::Label (text));
	l->set_xalign (0.5f);
	cls (*l, "spook-led");
	cls (*l, state);
	return l;
}

void
set_led (Gtk::Label* l, const char* state, std::string const& text)
{
	if (!l) { return; }
	l->set_text (text);
	Glib::RefPtr<Gtk::StyleContext> sc = l->get_style_context ();
	sc->remove_class ("on");
	sc->remove_class ("off");
	sc->remove_class ("warn");
	sc->remove_class ("err");
	sc->add_class (state);
}

/* botão de transporte redondo com um glyph no meio */
Gtk::Button*
tbtn (std::string const& glyph, const char* variant = nullptr)
{
	Gtk::Button* b = Gtk::manage (new Gtk::Button ());
	Gtk::Label* g = Gtk::manage (new Gtk::Label (glyph));
	cls (*g, "spook-glyph");
	b->add (*g);
	cls (*b, "spook-tbtn");
	if (variant) { cls (*b, variant); }
	return b;
}

Gtk::Button*
push (std::string const& text, const char* variant = nullptr)
{
	Gtk::Button* b = Gtk::manage (new Gtk::Button (text));
	cls (*b, "spook-btn");
	if (variant) { cls (*b, variant); }
	return b;
}

Gtk::Entry*
entry (std::string const& placeholder = "")
{
	Gtk::Entry* e = Gtk::manage (new Gtk::Entry);
	cls (*e, "spook-entry");
	if (!placeholder.empty ()) { e->set_placeholder_text (placeholder); }
	return e;
}

Gtk::ComboBoxText*
combo ()
{
	Gtk::ComboBoxText* c = Gtk::manage (new Gtk::ComboBoxText);
	cls (*c, "spook-combo");
	return c;
}

/* readout estilo DAW: rótulo pequeno em cima, valor em destaque embaixo */
Gtk::Widget*
readout (std::string const& rotulo, Gtk::Label** valor_out, bool acento = false)
{
	Gtk::Box* outer = box (Gtk::ORIENTATION_VERTICAL, 0);
	cls (*outer, "spook-readout");
	if (acento) { cls (*outer, "acc"); }
	Gtk::Label* rl = label (rotulo, "rl");
	Gtk::Label* rv = label ("—", "rv");
	outer->pack_start (*rl, false, false, 0);
	outer->pack_start (*rv, false, false, 0);
	if (valor_out) { *valor_out = rv; }
	return outer;
}

Gtk::Widget*
hsep ()
{
	Gtk::Separator* s = Gtk::manage (new Gtk::Separator (Gtk::ORIENTATION_HORIZONTAL));
	return s;
}

/* menu/barra de ferramentas: botão retangular pequeno (toolbar do Reaper) */
Gtk::Button*
tool (std::string const& text, const char* variant = nullptr)
{
	Gtk::Button* b = Gtk::manage (new Gtk::Button (text));
	cls (*b, "spook-tool");
	if (variant) { cls (*b, variant); }
	return b;
}

Gtk::ToggleButton*
tool_toggle (std::string const& text, const char* variant = nullptr)
{
	Gtk::ToggleButton* b = Gtk::manage (new Gtk::ToggleButton (text));
	cls (*b, "spook-tool");
	if (variant) { cls (*b, variant); }
	return b;
}

/* LCD do transporte: rótulo pequeno + dígitos grandes, com variante de cor */
Gtk::Widget*
lcd (std::string const& rotulo, Gtk::Label** valor_out, const char* variant = nullptr)
{
	Gtk::Box* outer = box (Gtk::ORIENTATION_VERTICAL, 0);
	cls (*outer, "spook-lcd");
	if (variant) { cls (*outer, variant); }
	Gtk::Label* rl = label (rotulo, "ll");
	Gtk::Label* rv = label ("—", "lv");
	outer->pack_start (*rl, false, false, 0);
	outer->pack_start (*rv, false, false, 0);
	if (valor_out) { *valor_out = rv; }
	return outer;
}

/* botãozinho de M / S / ● do mixer */
Gtk::ToggleButton*
ms (std::string const& text, const char* variant)
{
	Gtk::ToggleButton* b = Gtk::manage (new Gtk::ToggleButton (text));
	cls (*b, "spook-ms");
	cls (*b, variant);
	return b;
}

/* dB (-60..+6) -> 0..1 (o que o medidor desenha) */
double
db_to_unit (double db)
{
	if (db <= -60.0) { return 0.0; }
	if (db >= 0.0)   { return 1.0; }
	return (db + 60.0) / 60.0;
}

/* linha de medidor estilo DAW: rótulo · barra · valor */
Gtk::Widget*
meter_row (std::string const& rotulo, Gtk::LevelBar** bar_out, Gtk::Label** val_out)
{
	Gtk::Box* row = box (Gtk::ORIENTATION_HORIZONTAL, 10);
	Gtk::Label* l = label (rotulo, "spook-ruler");
	l->set_size_request (78, -1);
	row->pack_start (*l, false, false, 0);

	Gtk::LevelBar* b = Gtk::manage (new Gtk::LevelBar);
	b->set_min_value (0.0);
	b->set_max_value (100.0);
	b->set_value (0.0);
	b->set_hexpand (true);
	b->set_valign (Gtk::ALIGN_CENTER);
	row->pack_start (*b, true, true, 0);

	Gtk::Label* v = label ("0%", "spook-strip-val");
	v->set_size_request (78, -1);
	v->set_xalign (1.0f);
	row->pack_start (*v, false, false, 0);

	if (bar_out) { *bar_out = b; }
	if (val_out) { *val_out = v; }
	return row;
}

/* memoria do sistema: devolve o % usado e preenche "usado / total GiB". */
double
mem_load (std::string& texto)
{
	std::ifstream f ("/proc/meminfo");
	if (!f) { return 0.0; }
	std::string k, u;
	double total = 0, avail = 0;
	while (f >> k >> u) {
		if (k == "MemTotal:")      { total = std::atof (u.c_str ()); }
		else if (k == "MemAvailable:") { avail = std::atof (u.c_str ()); break; }
	}
	if (total <= 0.0) { return 0.0; }
	double used = total - avail;
	char buf[64];
	std::snprintf (buf, sizeof (buf), "%.1f / %.1f GiB",
	               used / 1048576.0, total / 1048576.0);
	texto = buf;
	return 100.0 * used / total;
}

/* carga de CPU do Arch (deltas de /proc/stat). Devolve 0..100. */
double
cpu_load (double& prev_total, double& prev_idle)
{
	std::ifstream f ("/proc/stat");
	if (!f) { return 0.0; }
	std::string cpu;
	double user = 0, nice = 0, sys = 0, idle = 0, iow = 0, irq = 0, sirq = 0;
	f >> cpu >> user >> nice >> sys >> idle >> iow >> irq >> sirq;
	double total = user + nice + sys + idle + iow + irq + sirq;
	if (prev_total <= 0.0) { prev_total = total; prev_idle = idle; return 0.0; }
	double dt = total - prev_total;
	double di = idle - prev_idle;
	prev_total = total;
	prev_idle  = idle;
	if (dt <= 0.0) { return 0.0; }
	double pct = 100.0 * (1.0 - di / dt);
	if (pct < 0.0) { pct = 0.0; }
	if (pct > 100.0) { pct = 100.0; }
	return pct;
}

} // namespace

/* ============================================================= construção */

MainWindow::MainWindow ()
{
	install_theme ();

	set_title ("SPECTER");
	/* Casa com StartupWMClass do .desktop: o compositor deixa de associar a
	 * janela ao launcher/ícone legado do Spook. */
	set_wmclass ("com.taylor.specter", "com.taylor.specter");
	set_icon_name ("com.taylor.specter");
	/* Ícone instalado também no tema hicolor; o caminho direto garante que a
	 * janela GTK o tenha mesmo antes de o cache do tema ser recarregado. */
	try { set_icon_from_file (exe_dir () + "/assets/specter-icon.png"); }
	catch (const Glib::Error&) { /* launcher ainda pode resolver pelo hicolor */ }
	set_default_size (1080, 760);
	cls (*this, "spook-window");

	_engine.reset (new Engine);
	std::string err;
	if (!_engine->init (locale_dir (), err)) {
		log_line ("[APP] FATAL: " + err);
	}
	_daw.reset (new Daw);

	Gtk::Box* root = box (Gtk::ORIENTATION_VERTICAL, 0);
	cls (*root, "spook-root");

	root->pack_start (*build_header (), false, false, 0);
	root->pack_start (*build_toolbar (), false, false, 0);
	root->pack_start (*build_transport (), false, false, 0);

	Gtk::Notebook* nb = Gtk::manage (new Gtk::Notebook);
	cls (*nb, "spook-tabs");
	_nb = nb;
	nb->append_page (*build_mixer_tab (),   *label (" MIXER "));
	nb->append_page (*build_arrange_tab (), *label (" ARRANJO "));
	nb->append_page (*build_session_tab (), *label (" SESSÃO "));
	nb->append_page (*build_actions_tab (), *label (" AÇÕES "));
	nb->append_page (*build_engine_tab (),  *label (" ENGINE "));
	nb->append_page (*build_scarlett_tab (),*label (" SCARLETT "));
	nb->append_page (*build_plugins_tab (), *label (" PLUGINS "));
	nb->append_page (*build_log_tab (),     *label (" LOG "));
	root->pack_start (*nb, true, true, 0);

	root->pack_start (*build_statusbar (), false, false, 0);
	add (*root);

	/* Só agora, com TODA a árvore construída (inclusive o rodapé), é seguro
	 * ouvir a troca de aba: o Gtk::Notebook emite "switch-page" já durante o
	 * append_page(), e o handler mexe em widgets que ainda não existiriam. */
	_sig_page = nb->signal_switch_page ().connect (sigc::mem_fun (*this, &MainWindow::on_switch_page));
	/* atalhos de teclado (estilo Reaper): espaço=play/stop, R=rec, L=loop,
	 * M=marcador, Ctrl+Z/Ctrl+Y, Ctrl+S. Só valem fora de campos de texto. */
	signal_key_press_event ().connect (sigc::mem_fun (*this, &MainWindow::on_key_press), false);

	/* backends (o Dummy entra por último; ver core/engine.cc) */
	std::vector<std::string> bs = _engine->available_backends ();
	for (size_t i = 0; i < bs.size (); ++i) { _backend_combo->append (bs[i]); }
	_backend_combo->set_active_text ("None (Dummy)"); /* modo seguro: nao toca som */

	_sess_dir->set_text (default_session_dir ());

	if (_eng_cards) { _eng_cards->set_text (std::to_string ((long) AlsaMixer::cards ().size ())); }

	_plugins_done.connect (sigc::mem_fun (*this, &MainWindow::on_plugins_scanned));

	sync_daw ();
	refresh_transport ();

	/* Depuracao: escolher a aba inicial (SPOOK_TAB=0..7). Serve para tirar
	 * capturas headless de cada aba sem precisar clicar. Precisa esperar o
	 * show_all() (feito pelo main.cc depois do construtor): o Gtk::Notebook
	 * recusa trocar para uma pagina que ainda nao esta visivel. */
	if (const char* t = std::getenv ("SPOOK_TAB")) {
		int n = std::atoi (t);
		Glib::signal_idle ().connect_once ([this, n] () {
			if (n >= 0 && n < (int) _nb->get_n_pages ()) { _nb->set_current_page (n); }
		});
	}

	update_engine_visuals ();
	_tick = Glib::signal_timeout ().connect (sigc::mem_fun (*this, &MainWindow::on_tick), 500);
}

MainWindow::~MainWindow ()
{
	/* ANTES de qualquer membro morrer: o dispose do notebook emite
	 * switch-page e o handler leria _engine/_daw já destruídos. */
	_sig_page.disconnect ();
	_tick.disconnect ();
	if (_piano_window) { delete _piano_window; _piano_window = nullptr; }
	if (_scan_thread.joinable ()) { _scan_thread.join (); }
}

std::string
MainWindow::default_session_dir () const
{
	const char* home = std::getenv ("HOME");
	std::string h = home ? home : "/tmp";
	return h + "/spook-audio-sessions/sessao";
}

void
MainWindow::set_status (std::string const& s, bool error)
{
	if (!_status) { return; }   /* pode ser chamado antes do rodapé existir */
	_status->set_text (s);
	if (error) { cls (*_status, "error"); }
	else       { _status->get_style_context ()->remove_class ("error"); }
}

void
MainWindow::update_engine_visuals ()
{
	const bool run  = _engine->engine_running ();
	const bool sess = _engine->session_open ();
	const std::string be = run ? _engine->backend_name () : std::string ("—");
	const std::string sr = run ? std::to_string ((long) _engine->sample_rate ()) + " Hz" : "—";

	set_led (_led_engine, run ? "on" : "off", run ? "ENGINE" : "ENGINE");
	set_led (_led_session, sess ? "on" : "off", "SESSION");
	set_led (_led_mixer, _mixer && _mixer->is_open () ? "on" : "off", "MIXER");

	if (_head_sr) { _head_sr->set_text (sr); }
	if (_tr_backend) { _tr_backend->set_text (be); }
	if (_tr_sr) { _tr_sr->set_text (sr); }
	if (_tr_session) {
		_tr_session->set_text (sess ? _engine->session_name () : std::string ("—"));
	}
	if (_btn_start) { _btn_start->set_sensitive (!run); }
	if (_btn_stop2) { _btn_stop2->set_sensitive (run); }

	/* transporte: só faz sentido com motor no ar (o Session exige motor) */
	const bool tr = run && sess;
	if (_tn_play)  { _tn_play->set_sensitive (tr); }
	if (_tn_stop)  { _tn_stop->set_sensitive (tr); }
	if (_tn_rec)   { _tn_rec->set_sensitive (tr); }
	if (_tn_start) { _tn_start->set_sensitive (tr); }
	if (_tn_end)   { _tn_end->set_sensitive (tr); }
	if (_tn_rew)   { _tn_rew->set_sensitive (tr); }
	if (_tn_panic) { _tn_panic->set_sensitive (tr); }

	if (_eng_led_init) { set_led (_eng_led_init, _engine->initialised () ? "on" : "off", "CORE"); }
	if (_eng_led_back) { set_led (_eng_led_back, run ? "on" : "off", "BACKEND"); }
	if (_eng_led_run)  { set_led (_eng_led_run, run ? "on" : "off", "MOTOR"); }
}

/* Liga/desliga a camada DAW junto com a sessão: sem sessão não há transporte. */
void
MainWindow::sync_daw ()
{
	_daw->attach (_engine->session ());
	/* O Ardour NÃO cria o bus master sozinho (é o GUI que chama
	 * Session::add_master_bus) — sem master não há soma nem o canal master
	 * no mixer. Idempotente. */
	_daw->ensure_master_bus ();
}

/* ---------------------------------------------------------------- TOOLBAR */

Gtk::Widget*
MainWindow::build_toolbar ()
{
	Gtk::Box* t = box (Gtk::ORIENTATION_HORIZONTAL, 4);
	cls (*t, "spook-toolbar");

	Gtk::Button* b;

	b = tool ("+ TRILHA", "accent");
	b->set_tooltip_text ("nova trilha de áudio (aba SESSÃO)");
	b->signal_clicked ().connect (sigc::mem_fun (*this, &MainWindow::on_track_new));
	t->pack_start (*b, false, false, 0);

	b = tool ("+ MIDI");
	b->set_tooltip_text ("nova trilha MIDI");
	b->signal_clicked ().connect (sigc::mem_fun (*this, &MainWindow::on_track_new_midi));
	t->pack_start (*b, false, false, 0);

	t->pack_start (*label ("│", "spook-tool-sep"), false, false, 0);

	b = tool ("MARCADOR");
	b->set_tooltip_text ("inserir marcador no cursor");
	b->signal_clicked ().connect (sigc::mem_fun (*this, &MainWindow::on_marker_add));
	t->pack_start (*b, false, false, 0);

	_tb_loop = tool_toggle ("LOOP");
	_tb_loop->set_tooltip_text ("região de loop (4 compassos a partir do cursor)");
	_tb_loop->signal_clicked ().connect (sigc::mem_fun (*this, &MainWindow::on_loop));
	t->pack_start (*_tb_loop, false, false, 0);

	_tb_click = tool_toggle ("CLICK");
	_tb_click->set_tooltip_text ("metrônomo (só audível com um backend real)");
	_tb_click->signal_clicked ().connect (sigc::mem_fun (*this, &MainWindow::on_click));
	t->pack_start (*_tb_click, false, false, 0);

	t->pack_start (*label ("│", "spook-tool-sep"), false, false, 0);

	b = tool ("DESFAZER");
	b->signal_clicked ().connect (sigc::mem_fun (*this, &MainWindow::on_undo));
	t->pack_start (*b, false, false, 0);

	b = tool ("REFAZER");
	b->signal_clicked ().connect (sigc::mem_fun (*this, &MainWindow::on_redo));
	t->pack_start (*b, false, false, 0);

	b = tool ("SALVAR", "accent");
	b->signal_clicked ().connect (sigc::mem_fun (*this, &MainWindow::on_save));
	t->pack_start (*b, false, false, 0);

	t->pack_start (*label ("│", "spook-tool-sep"), false, false, 0);

	b = tool ("AÇÕES");
	b->set_tooltip_text ("lista de ações pesquisável");
	b->signal_clicked ().connect ([this] () { _nb->set_current_page (3); });
	t->pack_start (*b, false, false, 0);

	b = tool ("PÂNICO MIDI", "danger");
	b->signal_clicked ().connect (sigc::mem_fun (*this, &MainWindow::on_panic));
	t->pack_start (*b, false, false, 0);

	Gtk::Label* sp = Gtk::manage (new Gtk::Label (""));
	t->pack_start (*sp, true, true, 0);

	_tr_state = label ("STOPPED", "spook-led");
	t->pack_start (*_tr_state, false, false, 0);

	return t;
}

/* ------------------------------------------------------------------ HEADER */

Gtk::Widget*
MainWindow::build_header ()
{
	Gtk::Box* h = box (Gtk::ORIENTATION_HORIZONTAL, 12);
	cls (*h, "spook-header");

	Gtk::Label* logo = label ("◆", "spook-logo");
	h->pack_start (*logo, false, false, 0);

	Gtk::Box* tit = box (Gtk::ORIENTATION_VERTICAL, 0);
	tit->pack_start (*label ("SPECTER", "spook-title"), false, false, 0);
	tit->pack_start (*label ("ARDOUR CORE · ALSA · PIPEWIRE · JACK", "spook-sub"), false, false, 0);
	h->pack_start (*tit, false, false, 0);

	Gtk::Label* spacer = Gtk::manage (new Gtk::Label (""));
	h->pack_start (*spacer, true, true, 0);

	_led_engine  = led ("ENGINE", "off");
	_led_session = led ("SESSION", "off");
	_led_mixer   = led ("MIXER", "off");
	h->pack_start (*_led_engine, false, false, 0);
	h->pack_start (*_led_session, false, false, 0);
	h->pack_start (*_led_mixer, false, false, 0);

	Gtk::Box* srbox = box (Gtk::ORIENTATION_VERTICAL, 0);
	srbox->set_halign (Gtk::ALIGN_END);
	srbox->pack_start (*label ("SAMPLE RATE", "spook-ruler"), false, false, 0);
	_head_sr = label ("—", "spook-clock");
	srbox->pack_start (*_head_sr, false, false, 0);
	h->pack_start (*srbox, false, false, 0);

	return h;
}

/* --------------------------------------------------------------- TRANSPORT */

Gtk::Widget*
MainWindow::build_transport ()
{
	Gtk::Box* t = box (Gtk::ORIENTATION_HORIZONTAL, 10);
	cls (*t, "spook-transport");

	/* motor (libardour) — o resto do transporte só vive depois disto */
	Gtk::Box* motor = box (Gtk::ORIENTATION_HORIZONTAL, 4);
	_tn_motor = tool ("MOTOR ▶", "accent");
	_tn_motor->set_tooltip_text ("sobe/derruba o motor (backend escolhido na aba ENGINE)");
	_tn_motor->signal_clicked ().connect ([this] () {
		if (_engine->engine_running ()) { on_stop (); } else { on_start (); }
	});
	motor->pack_start (*_tn_motor, false, false, 0);
	t->pack_start (*motor, false, false, 0);

	t->pack_start (*label ("│", "spook-tool-sep"), false, false, 0);

	/* cluster de transporte */
	Gtk::Box* cluster = box (Gtk::ORIENTATION_HORIZONTAL, 6);
	_tn_start = tbtn ("|◀");
	_tn_start->set_tooltip_text ("voltar ao início");
	_tn_rew = tbtn ("◀◀");
	_tn_rew->set_tooltip_text ("rebobinar");
	_tn_play = tbtn ("▶", "play");
	_tn_play->set_tooltip_text ("tocar / pausar (espaço)");
	_tn_stop = tbtn ("■", "stop");
	_tn_stop->set_tooltip_text ("parar");
	_tn_rec = tbtn ("●", "rec");
	_tn_rec->set_tooltip_text ("armar gravação (R)");
	_tn_end = tbtn ("▶|");
	_tn_end->set_tooltip_text ("ir ao fim");
	_tn_panic = tbtn ("⊘", "panic");
	_tn_panic->set_tooltip_text ("pânico: corta todas as notas MIDI (não é o motor — o motor é o MOTOR)");

	_tn_start->signal_clicked ().connect (sigc::mem_fun (*this, &MainWindow::on_transport_start));
	_tn_rew->signal_clicked ().connect (sigc::mem_fun (*this, &MainWindow::on_rewind));
	_tn_play->signal_clicked ().connect (sigc::mem_fun (*this, &MainWindow::on_transport_play));
	_tn_stop->signal_clicked ().connect (sigc::mem_fun (*this, &MainWindow::on_transport_stop));
	_tn_rec->signal_clicked ().connect (sigc::mem_fun (*this, &MainWindow::on_transport_rec));
	_tn_end->signal_clicked ().connect (sigc::mem_fun (*this, &MainWindow::on_transport_end));
	_tn_panic->signal_clicked ().connect (sigc::mem_fun (*this, &MainWindow::on_panic));

	cluster->pack_start (*_tn_start, false, false, 0);
	cluster->pack_start (*_tn_rew, false, false, 0);
	cluster->pack_start (*_tn_play, false, false, 0);
	cluster->pack_start (*_tn_stop, false, false, 0);
	cluster->pack_start (*_tn_rec, false, false, 0);
	cluster->pack_start (*_tn_end, false, false, 0);
	cluster->pack_start (*_tn_panic, false, false, 0);
	t->pack_start (*cluster, false, false, 0);

	t->pack_start (*label ("│", "spook-tool-sep"), false, false, 0);

	/* LCD do transporte: barras | tempo | amostras */
	t->pack_start (*lcd ("BARRAS·BATIDAS", &_lcd_bars, "acc"), false, false, 0);
	t->pack_start (*lcd ("TEMPO", &_lcd_time), false, false, 0);
	t->pack_start (*lcd ("AMOSTRAS", &_lcd_samples), false, false, 0);

	/* BPM + fórmula de compasso (o tempo do Ardour é o do mapa da sessão) */
	Gtk::Box* tempo = box (Gtk::ORIENTATION_HORIZONTAL, 6);
	cls (*tempo, "spook-lcd");
	Gtk::Label* bl = label ("BPM", "ll");
	_bpm_spin = Gtk::manage (new Gtk::SpinButton (Gtk::Adjustment::create (120.0, 20.0, 300.0, 1.0, 10.0, 0.0), 1.0, 1));
	_bpm_spin->set_numeric (true);
	_bpm_spin->set_value (120.0);
	_bpm_spin->set_size_request (74, -1);
	_bpm_spin->signal_value_changed ().connect (sigc::mem_fun (*this, &MainWindow::on_bpm_apply));
	tempo->pack_start (*bl, false, false, 0);
	tempo->pack_start (*_bpm_spin, false, false, 0);
	_sig_label = label ("4/4", "lv");
	_sig_label->set_tooltip_text ("fórmula de compasso do mapa de tempo");
	tempo->pack_start (*_sig_label, false, false, 0);
	t->pack_start (*tempo, false, false, 0);

	Gtk::Label* spacer = Gtk::manage (new Gtk::Label (""));
	t->pack_start (*spacer, true, true, 0);

	t->pack_start (*readout ("BACKEND", &_tr_backend, true), false, false, 0);
	t->pack_start (*readout ("SAMPLE RATE", &_tr_sr), false, false, 0);
	t->pack_start (*readout ("SESSÃO", &_tr_session), false, false, 0);

	/* medidor de CPU (dado real de /proc/stat) */
	Gtk::Box* cpu = box (Gtk::ORIENTATION_VERTICAL, 2);
	cpu->set_size_request (150, -1);
	cpu->pack_start (*label ("CPU ARCH", "spook-ruler"), false, false, 0);
	_cpu_bar = Gtk::manage (new Gtk::LevelBar);
	_cpu_bar->set_min_value (0.0);
	_cpu_bar->set_max_value (100.0);
	_cpu_bar->set_value (0.0);
	_cpu_label = label ("0%", "spook-strip-val");
	cpu->pack_start (*_cpu_bar, false, false, 0);
	cpu->pack_start (*_cpu_label, false, false, 0);
	t->pack_start (*cpu, false, false, 0);

	return t;
}

/* ======================================================== MEDIDOR VU (cairo)
 *
 * O Gtk::LevelBar não pinta por faixa de valor (verde/amarelo/vermelho), então o
 * medidor é desenhado à mão — como numa DAW. Duas colunas (L/R) com as zonas
 * -60..-18 dB verde, -18..-6 amarelo e -6..0 vermelho, mais o pico (hold) que cai
 * devagar. Clique no medidor zera o hold.
 */

namespace {

void
pinta_zonas (Cairo::RefPtr<Cairo::Context> const& cr, double x, double w, double h,
             double de, double ate, double v, double r, double g, double b)
{
	if (v <= de) { return; }
	const double topo = (v < ate) ? v : ate;
	const double y0 = h * (1.0 - topo);
	const double y1 = h * (1.0 - de);
	cr->set_source_rgb (r, g, b);
	cr->rectangle (x, y0, w, y1 - y0);
	cr->fill ();
}

} // namespace

/* "L37" / "C" / "R37" a partir do valor cru e do alcance REAL do controle de
 * pan (o panner mono do Ardour vai de 0 a 1 com centro em 0,5; o estéreo, de
 * -1 a 1). Normaliza pelo centro — sem isso o centro do panner mono sai "R50". */
std::string
pan_txt (double pan, double lo, double hi)
{
	const double c    = (lo + hi) * 0.5;
	const double meia = (hi - lo) * 0.5;
	if (meia <= 0.0) { return "C"; }
	const double n = (pan - c) / meia;
	char b[16];
	if (n < -0.02)     { std::snprintf (b, sizeof (b), "L%d", (int) (-n * 100.0)); }
	else if (n > 0.02) { std::snprintf (b, sizeof (b), "R%d", (int) ( n * 100.0)); }
	else               { return std::string ("C"); }
	return std::string (b);
}

/* só escreve se mudou: set_text/set_label idêntico a cada tick faz o GTK pedir
 * relayout sem necessidade (o transporte é atualizado 2x por segundo). */
void
txt (Gtk::Label* l, std::string const& s)
{
	if (l && l->get_text () != s) { l->set_text (s); }
}

void
txt (Gtk::Button* b, std::string const& s)
{
	if (b && b->get_label () != s) { b->set_label (s); }
}

class VuMeter : public Gtk::DrawingArea {  public:
	VuMeter ()
	{
		set_size_request (10, -1);
		set_vexpand (true);
		add_events (Gdk::BUTTON_PRESS_MASK);
		signal_button_press_event ().connect ([this] (GdkEventButton*) { reset_hold (); return true; });
	}

	void set_levels (float l, float r)
	{
		_db[0] = l; _db[1] = r;
		for (int c = 0; c < 2; ++c) {
			if (_db[c] > _hold[c]) { _hold[c] = _db[c]; }
			else                   { _hold[c] -= 0.7f; }
		}
		queue_draw ();
	}

	void reset_hold () { _hold[0] = _hold[1] = -120.0f; queue_draw (); }

  protected:
	bool on_draw (Cairo::RefPtr<Cairo::Context> const& cr) override
	{
		const Gtk::Allocation a = get_allocation ();
		const double w = (double) a.get_width ();
		const double h = (double) a.get_height ();
		if (w < 2.0 || h < 2.0) { return true; }

		cr->set_source_rgb (0.016, 0.024, 0.033);
		cr->rectangle (0, 0, w, h);
		cr->fill ();

		const double gap = 2.0;
		const double cw  = (w - gap) / 2.0;

		for (int c = 0; c < 2; ++c) {
			const double x = (double) c * (cw + gap);
			const double v = unit (_db[c]);
			pinta_zonas (cr, x, cw, h, 0.00, 0.70, v, 0.145, 0.878, 0.541); /* verde   -60..-18 */
			pinta_zonas (cr, x, cw, h, 0.70, 0.90, v, 1.000, 0.831, 0.000); /* amarelo -18..-6  */
			pinta_zonas (cr, x, cw, h, 0.90, 1.00, v, 1.000, 0.333, 0.400); /* vermelho -6..0   */

			/* réguas de escala */
			cr->set_source_rgba (0.0, 0.0, 0.0, 0.42);
			for (int m = 1; m < 5; ++m) {
				cr->rectangle (x, h * (double) m / 5.0, cw, 1.0);
			}
			cr->fill ();

			/* pico (hold) */
			const double hv = unit (_hold[c]);
			if (hv > 0.0) {
				cr->set_source_rgb (0.85, 0.92, 1.0);
				cr->rectangle (x, h * (1.0 - hv) - 1.0, cw, 2.0);
				cr->fill ();
			}
		}
		return true;
	}

  private:
	static double unit (float db)
	{
		if (db <= -60.0f) { return 0.0; }
		if (db >= 0.0f)   { return 1.0; }
		return ((double) db + 60.0) / 60.0;
	}

	float _db[2]   = { -120.0f, -120.0f };
	float _hold[2] = { -120.0f, -120.0f };
};

/* ==================================================================== MIXER */

Gtk::Widget*
MainWindow::build_mixer_tab ()
{
	Gtk::Box* b = box (Gtk::ORIENTATION_VERTICAL, 8, 10);

	Gtk::Box* head = box (Gtk::ORIENTATION_HORIZONTAL, 8);
	head->pack_start (*strong ("MIXER"), false, false, 0);
	_mixer_info = dim ("0 tiras");
	head->pack_start (*_mixer_info, false, false, 0);

	Gtk::Button* b1 = tool ("LIMPAR SOLO");
	b1->set_tooltip_text ("desliga todos os solos (o \"clear solos\" do Reaper)");
	b1->signal_clicked ().connect ([this] () { if (_daw->attached ()) { _daw->clear_solo (); refresh_mixer (); } });
	head->pack_start (*b1, false, false, 0);

	Gtk::Button* b2 = tool ("ARMAR TODAS");
	b2->set_tooltip_text ("arma gravação em todas as trilhas");
	b2->signal_clicked ().connect ([this] () { if (_daw->attached ()) { _daw->arm_all (true); refresh_mixer (); } });
	head->pack_start (*b2, false, false, 0);

	Gtk::Button* b3 = tool ("DESARMAR");
	b3->set_tooltip_text ("desarma todas as trilhas");
	b3->signal_clicked ().connect ([this] () { if (_daw->attached ()) { _daw->arm_all (false); refresh_mixer (); } });
	head->pack_start (*b3, false, false, 0);

	Gtk::Button* b4 = tool ("ZERAR GANHOS");
	b4->set_tooltip_text ("devolve todos os faders para 0 dB");
	b4->signal_clicked ().connect ([this] () {
		if (!_daw->attached ()) { return; }
		std::vector<RouteStrip> ss = _daw->strips ();
		std::string err;
		for (size_t i = 0; i < ss.size (); ++i) { _daw->set_gain ((int) i, 0.0, err); }
		refresh_mixer ();
	});
	head->pack_start (*b4, false, false, 0);

	Gtk::Label* sp = Gtk::manage (new Gtk::Label (""));
	head->pack_start (*sp, true, true, 0);
	b->pack_start (*head, false, false, 0);

	_mixer_state = dim ("abra uma sessão (aba SESSÃO) para o mixer aparecer.");
	b->pack_start (*_mixer_state, false, false, 0);

	_mixer_box = box (Gtk::ORIENTATION_HORIZONTAL, 6);
	_mixer_box->set_valign (Gtk::ALIGN_FILL);

	Gtk::ScrolledWindow* sc = Gtk::manage (new Gtk::ScrolledWindow);
	sc->add (*_mixer_box);
	sc->set_policy (Gtk::POLICY_AUTOMATIC, Gtk::POLICY_NEVER);
	sc->set_vexpand (true);
	b->pack_start (*sc, true, true, 0);

	return b;
}

void
MainWindow::refresh_mixer ()
{
	if (!_mixer_box) { return; }

	if (!_daw->attached ()) {
		if (_mixer_sig != "!none") {
			_mixer_sig = "!none";
			_mixer_updating = true;
			_strip_ui.clear ();
			clear (*_mixer_box);
			Gtk::Label* e = label ("(sem sessão — abra uma em SESSÃO)", "spook-empty");
			e->set_margin_top (8);
			_mixer_box->pack_start (*e, false, false, 0);
			_mixer_box->show_all ();
			_mixer_updating = false;
		}
		if (_mixer_info)  { _mixer_info->set_text ("0 tiras"); }
		if (_mixer_state) { _mixer_state->set_text ("abra uma sessão (aba SESSÃO) para o mixer aparecer."); }
		return;
	}

	const std::vector<RouteStrip> ss = _daw->strips ();

	/* a lista só é remontada quando muda (nome/tipo/ordem) — senão o fader
	 * perderia o arrasto a cada tick */
	std::string sig;
	for (size_t i = 0; i < ss.size (); ++i) {
		sig += ss[i].name;
		sig += ss[i].is_master ? "#" : ".";
		sig += ss[i].is_midi ? "i" : "a";
		sig += "|";
	}

	if (sig != _mixer_sig) {
		_mixer_sig = sig;
		_mixer_updating = true;
		clear (*_mixer_box);
		_strip_ui.clear ();
		_strip_ui.reserve (ss.size ());
		if (_mixer_state) { _mixer_state->set_text (""); }

		for (size_t i = 0; i < ss.size (); ++i) {
			const RouteStrip& m = ss[i];

			Gtk::Box* strip = box (Gtk::ORIENTATION_VERTICAL, 3);
			cls (*strip, "mixer-strip");
			if (m.is_master) { cls (*strip, "master"); }
			if (m.armed)     { cls (*strip, "armed"); }
			strip->set_size_request (92, -1);
			strip->set_valign (Gtk::ALIGN_FILL);

			/* Nome é um Track Header editável, como Logic/Studio One: clique e
			 * digite; Enter ou perder foco confirma. O master fica só leitura. */
			Gtk::Box* title = box (Gtk::ORIENTATION_HORIZONTAL, 3);
			Gtk::Entry* nm = entry ();
			cls (*nm, "mixer-name-entry");
			nm->set_text (m.name);
			nm->set_alignment (0.5f);
			nm->set_width_chars (8);
			nm->set_hexpand (true);
			nm->set_tooltip_text (m.is_master ? "canal master" : "clique para renomear · Enter confirma");
			nm->set_editable (!m.is_master);
			title->pack_start (*nm, true, true, 0);
			if (!m.is_master) {
				Gdk::RGBA c;
				const unsigned int rgba = m.color ? m.color : 0x34e1e1ffu;
				c.set_rgba (((rgba >> 24) & 255) / 255.0, ((rgba >> 16) & 255) / 255.0,
				            ((rgba >> 8) & 255) / 255.0, (rgba & 255) / 255.0);
				Gtk::ColorButton* cor = Gtk::manage (new Gtk::ColorButton (c));
				cls (*cor, "track-color");
				cor->set_tooltip_text ("cor da trilha (metadado salvo na sessão)");
				cor->signal_color_set ().connect ([this, i, cor] () { on_route_color ((int) i, cor->get_rgba ()); });
				title->pack_start (*cor, false, false, 0);
			}
			strip->pack_start (*title, false, false, 0);

			Gtk::Label* bd = label (m.is_master ? "MASTER" : (m.is_midi ? "MIDI" : "AUDIO"), "mixer-badge");
			if (m.is_master) { cls (*bd, "master"); }
			else if (m.is_midi) { cls (*bd, "midi"); }
			bd->set_xalign (0.5f);
			strip->pack_start (*bd, false, false, 0);

			/* pan */
			Gtk::Scale* pan = Gtk::manage (new Gtk::Scale (
			    Gtk::Adjustment::create (m.pan, m.pan_min, m.pan_max, 0.05, 0.25, 0.0), Gtk::ORIENTATION_HORIZONTAL));
			cls (*pan, "mixer-pan");
			pan->set_draw_value (false);
			pan->set_size_request (78, -1);
			Gtk::Label* panv = label (pan_txt (m.pan, m.pan_min, m.pan_max), "mixer-pan");
			panv->set_xalign (0.5f);
			strip->pack_start (*pan, false, false, 0);
			strip->pack_start (*panv, false, false, 0);

			/* fader + 2 medidores */
			Gtk::Box* row = box (Gtk::ORIENTATION_HORIZONTAL, 3);
			row->set_vexpand (true);
			Gtk::Scale* fader = Gtk::manage (new Gtk::Scale (
			    Gtk::Adjustment::create (m.gain_db, -60.0, 6.0, 0.5, 6.0, 0.0), Gtk::ORIENTATION_VERTICAL));
			cls (*fader, "mixer-fader");
			fader->set_draw_value (false);
			fader->set_inverted (true);   /* topo = +6 dB */
			fader->set_vexpand (true);
			fader->set_size_request (32, 130);
			row->pack_start (*fader, false, false, 0);
			VuMeter* ml = Gtk::manage (new VuMeter);
			VuMeter* mr = Gtk::manage (new VuMeter);
			row->pack_start (*ml, false, false, 0);
			row->pack_start (*mr, false, false, 0);
			strip->pack_start (*row, true, true, 0);

			char dbuf[32];
			std::snprintf (dbuf, sizeof (dbuf), "%+.1f dB", m.gain_db);
			Gtk::Label* dl = label (dbuf, "mixer-db");
			dl->set_xalign (0.5f);
			strip->pack_start (*dl, false, false, 0);

			/* M · S · ● (arm) */
			Gtk::Box* btns = box (Gtk::ORIENTATION_HORIZONTAL, 3);
			btns->set_halign (Gtk::ALIGN_CENTER);
			Gtk::ToggleButton* bm = ms ("M", "mute");
			Gtk::ToggleButton* bs = ms ("S", "solo");
			bm->set_tooltip_text ("mute");
			bs->set_tooltip_text ("solo");
			bm->set_active (m.muted);
			bs->set_active (m.soloed);
			btns->pack_start (*bm, false, false, 0);
			btns->pack_start (*bs, false, false, 0);
			Gtk::ToggleButton* ba = nullptr;
			if (!m.is_master) {
				ba = ms ("●", "arm");
				ba->set_tooltip_text ("armar gravação desta trilha");
				ba->set_active (m.armed);
				btns->pack_start (*ba, false, false, 0);
			}
			strip->pack_start (*btns, false, false, 0);

			_mixer_box->pack_start (*strip, false, false, 0);

			/* a tira entra no vetor ANTES de ligar os sinais: os lambdas
			 * consultam _strip_ui pelo índice (o reserve() evita realloc) */
			StripUI u;
			u.idx  = (int) i;
			u.name = m.name;
			u.strip = strip;
			u.fader = fader; u.pan = pan; u.db = dl; u.panv = panv; u.name_entry = nm;
			u.pan_lo = m.pan_min; u.pan_hi = m.pan_max;
			u.meter[0] = ml; u.meter[1] = mr;
			u.mute = bm; u.solo = bs; u.arm = ba;
			_strip_ui.push_back (u);
			const int k = (int) _strip_ui.size () - 1;

			if (!m.is_master) {
				nm->signal_activate ().connect ([this, k, nm] () {
					on_route_rename (_strip_ui[k].idx, nm);
				});
				nm->signal_focus_out_event ().connect ([this, k, nm] (GdkEventFocus*) {
					on_route_rename (_strip_ui[k].idx, nm);
					return false;
				});
			}

			pan->signal_value_changed ().connect ([this, k] () {
				if (_mixer_updating) { return; }
				StripUI& s = _strip_ui[k];
				s.touched = (double) g_get_monotonic_time ();
				std::string err;
				if (!_daw->set_pan (s.idx, s.pan->get_value (), err)) { set_status (err, true); }
				const double p = s.pan->get_value ();
				if (s.panv) { s.panv->set_text (pan_txt (p, s.pan_lo, s.pan_hi)); }
			});

			fader->signal_value_changed ().connect ([this, k] () {
				if (_mixer_updating) { return; }
				StripUI& s = _strip_ui[k];
				s.touched = (double) g_get_monotonic_time ();
				const double db = s.fader->get_value ();
				std::string err;
				if (!_daw->set_gain (s.idx, db, err)) { set_status (err, true); }
				char b[32];
				std::snprintf (b, sizeof (b), "%+.1f dB", db);
				if (s.db) { s.db->set_text (b); }
			});

			bm->signal_toggled ().connect ([this, k] () {
				if (_mixer_updating) { return; }
				StripUI& s = _strip_ui[k];
				s.touched = (double) g_get_monotonic_time ();
				std::string err;
				if (!_daw->set_mute (s.idx, s.mute->get_active (), err)) { set_status (err, true); }
			});

			bs->signal_toggled ().connect ([this, k] () {
				if (_mixer_updating) { return; }
				StripUI& s = _strip_ui[k];
				s.touched = (double) g_get_monotonic_time ();
				std::string err;
				if (!_daw->set_solo (s.idx, s.solo->get_active (), err)) { set_status (err, true); }
			});

			if (ba) {
				ba->signal_toggled ().connect ([this, k] () {
					if (_mixer_updating) { return; }
					StripUI& s = _strip_ui[k];
					s.touched = (double) g_get_monotonic_time ();
					std::string err;
					if (!_daw->set_arm (s.idx, s.arm->get_active (), err)) { set_status (err, true); }
					if (s.strip) {
						Glib::RefPtr<Gtk::StyleContext> psc = s.strip->get_style_context ();
						psc->remove_class ("armed");
						if (s.arm->get_active ()) { psc->add_class ("armed"); }
					}
				});
			}
		}
		_mixer_box->show_all ();
		_mixer_updating = false;
	}

	if (_strip_ui.size () != ss.size ()) { return; }

	/* --- valores do modelo -> widgets (sem atropelar quem está arrastando) --- */
	const double agora = (double) g_get_monotonic_time ();
	for (size_t i = 0; i < _strip_ui.size (); ++i) {
		StripUI& s = _strip_ui[i];
		const RouteStrip& m = ss[i];

		if (s.meter[0]) { s.meter[0]->set_levels (m.peak[0], m.peak[1]); }

		if ((agora - s.touched) > 500000.0) {   /* 0,5 s de folga */
			_mixer_updating = true;
			if (s.fader && std::abs (s.fader->get_value () - m.gain_db) > 0.05) { s.fader->set_value (m.gain_db); }
			if (s.pan   && std::abs (s.pan->get_value () - m.pan) > 0.02)      { s.pan->set_value (m.pan); }
			if (s.mute  && s.mute->get_active () != m.muted)  { s.mute->set_active (m.muted); }
			if (s.solo  && s.solo->get_active () != m.soloed) { s.solo->set_active (m.soloed); }
			if (s.arm   && s.arm->get_active () != m.armed)   { s.arm->set_active (m.armed); }
			_mixer_updating = false;
		}

		char b[32];
		if (s.db) { std::snprintf (b, sizeof (b), "%+.1f dB", m.gain_db); s.db->set_text (b); }
		if (s.panv) { s.panv->set_text (pan_txt (m.pan, s.pan_lo, s.pan_hi)); }
		if (s.name_entry && !s.name_entry->has_focus () && s.name_entry->get_text () != m.name) {
			s.name_entry->set_text (m.name);
		}
	}

	if (_mixer_info) {
		int trk = 0, bus = 0, mast = 0;
		for (size_t i = 0; i < ss.size (); ++i) {
			if (ss[i].is_master) { ++mast; continue; }
			if (ss[i].is_track)  { ++trk; } else { ++bus; }
		}
		_mixer_info->set_text (std::to_string (trk) + " trilhas · " + std::to_string (bus) + " buses · "
		                       + std::to_string (mast) + " master");
	}
}

/* ================================================================ ARRANJO
 *
 * Inspirado na Arrange View do Studio One e na Tracks Area do Logic: a régua
 * temporal fica acima, as faixas ficam em lanes e as seções globais são
 * blocos coloridos. Nesta etapa o modelo real são Location::IsRangeMarker do
 * Ardour — portanto as seções sobrevivem save/reopen e servem inclusive para
 * loop, markers e export. Não fingimos que os blocos sejam áudio gravado. */

Gtk::Widget*
MainWindow::build_arrange_tab ()
{
	Gtk::Box* b = box (Gtk::ORIENTATION_VERTICAL, 8, 10);
	Gtk::Box* tools = box (Gtk::ORIENTATION_HORIZONTAL, 8);
	tools->pack_start (*strong ("ARRANGER"), false, false, 0);
	_arrange_name = entry ("Intro, Verso, Refrão…");
	_arrange_name->set_text ("Intro");
	_arrange_name->set_hexpand (true);
	tools->pack_start (*_arrange_name, true, true, 0);
	tools->pack_start (*label ("COMPASSOS", "spook-ruler"), false, false, 0);
	_arrange_bars = Gtk::manage (new Gtk::SpinButton (Gtk::Adjustment::create (8.0, 1.0, 256.0, 1.0, 4.0, 0.0), 1.0, 0));
	_arrange_bars->set_numeric (true);
	_arrange_bars->set_tooltip_text ("duração da nova seção no cursor");
	tools->pack_start (*_arrange_bars, false, false, 0);
	tools->pack_start (*label ("ZOOM", "spook-ruler"), false, false, 0);
	_arrange_zoom = Gtk::manage (new Gtk::Scale (Gtk::Adjustment::create (32.0, 8.0, 128.0, 1.0, 8.0, 0.0), Gtk::ORIENTATION_HORIZONTAL));
	_arrange_zoom->set_draw_value (false);
	_arrange_zoom->set_size_request (90, -1);
	_arrange_zoom->set_tooltip_text ("quantos compassos aparecem na tela");
	_arrange_zoom->signal_value_changed ().connect ([this] () { refresh_arrange (); });
	tools->pack_start (*_arrange_zoom, false, false, 0);
	Gtk::Button* add = push ("+ SEÇÃO", "accent");
	add->set_tooltip_text ("cria seção global no cursor (Arranger Track do Studio One)");
	add->signal_clicked ().connect (sigc::mem_fun (*this, &MainWindow::on_arranger_add));
	tools->pack_start (*add, false, false, 0);
	_arrange_midi_track = combo ();
	_arrange_midi_track->set_size_request (135, -1);
	_arrange_midi_track->set_tooltip_text ("trilha MIDI que receberá o clip vazio");
	tools->pack_start (*_arrange_midi_track, false, false, 0);
	Gtk::Button* clip = push ("+ CLIP MIDI", "cyan");
	clip->set_tooltip_text ("cria um clip MIDI real, vazio e editável, no cursor");
	clip->signal_clicked ().connect (sigc::mem_fun (*this, &MainWindow::on_arranger_midi_clip));
	tools->pack_start (*clip, false, false, 0);
	Gtk::Button* dup_clip = push ("DUP CLIP", nullptr);
	dup_clip->set_tooltip_text ("duplica o clip selecionado à direita");
	dup_clip->signal_clicked ().connect (sigc::mem_fun (*this, &MainWindow::on_arranger_clip_duplicate));
	tools->pack_start (*dup_clip, false, false, 0);
	Gtk::Button* del_clip = push ("APAGAR CLIP", "danger");
	del_clip->set_tooltip_text ("apaga o clip selecionado");
	del_clip->signal_clicked ().connect (sigc::mem_fun (*this, &MainWindow::on_arranger_clip_delete));
	tools->pack_start (*del_clip, false, false, 0);
	Gtk::Button* piano = push ("PIANO ROLL", "accent");
	piano->set_tooltip_text ("abre o editor de notas do clip MIDI selecionado");
	piano->signal_clicked ().connect (sigc::mem_fun (*this, &MainWindow::on_piano_roll_open));
	tools->pack_start (*piano, false, false, 0);
	b->pack_start (*tools, false, false, 0);
	Gtk::Box* audio_tools = box (Gtk::ORIENTATION_HORIZONTAL, 8);
	audio_tools->pack_start (*dim ("AUDIO CLIP"), false, false, 0);
	_arrange_audio_track = combo ();
	_arrange_audio_track->set_size_request (170, -1);
	_arrange_audio_track->set_tooltip_text ("trilha de áudio que receberá o arquivo importado");
	audio_tools->pack_start (*_arrange_audio_track, false, false, 0);
	Gtk::Button* import = push ("IMPORTAR ÁUDIO", nullptr);
	import->set_tooltip_text ("adiciona um arquivo de áudio como Region real; não toca nem arma entrada");
	import->signal_clicked ().connect (sigc::mem_fun (*this, &MainWindow::on_arranger_import_audio));
	audio_tools->pack_start (*import, false, false, 0);
	audio_tools->pack_start (*dim ("WAV · FLAC · AIFF · MP3 · OGG — fonte externa, sem playback automático"), false, false, 0);
	b->pack_start (*audio_tools, false, false, 0);

	_arrange_state = dim ("abra uma sessão para criar seções.");
	b->pack_start (*_arrange_state, false, false, 0);
	b->pack_start (*dim ("Clique na régua ou numa lane para mover o cursor. As seções são regiões globais: Intro, Verso, Refrão, Ponte…"), false, false, 0);

	_arrange_canvas = Gtk::manage (new Gtk::DrawingArea);
	cls (*_arrange_canvas, "arrange-canvas");
	_arrange_canvas->set_size_request (-1, 420);
	_arrange_canvas->set_vexpand (true);
	_arrange_canvas->add_events (Gdk::BUTTON_PRESS_MASK | Gdk::BUTTON_RELEASE_MASK | Gdk::POINTER_MOTION_MASK);
	_arrange_canvas->signal_draw ().connect ([this] (Cairo::RefPtr<Cairo::Context> const& cr) -> bool {
		const Gtk::Allocation a = _arrange_canvas->get_allocation ();
		const double w = a.get_width (), h = a.get_height ();
		cr->set_source_rgb (0.027, 0.035, 0.050); cr->paint ();
		const double left = 150.0, ruler = 30.0;
		const double sr = (_engine->session_open () && _engine->session_sample_rate () > 0) ? _engine->session_sample_rate () : 48000.0;
		const double bpm = (_daw->attached () && _daw->bpm () > 1.0) ? _daw->bpm () : 120.0;
		const int beats = _daw->attached () ? _daw->meter_beats () : 4;
		const double samples_bar = sr * (60.0 / bpm) * beats;
		const int visible_bars = _arrange_zoom ? (int) std::lround (_arrange_zoom->get_value ()) : 32;
		const double px_bar = std::max (10.0, (w - left) / visible_bars);
		const std::vector<RouteStrip> ss = _daw->attached () ? _daw->strips () : std::vector<RouteStrip> ();
		const std::vector<MarkerInfo> ms = _daw->attached () ? _daw->markers () : std::vector<MarkerInfo> ();
		const double lane_h = std::max (52.0, (h - ruler) / std::max (1, (int) ss.size ()));

		/* régua e grid de compassos */
		cr->set_source_rgb (0.055, 0.071, 0.10); cr->rectangle (0, 0, w, ruler); cr->fill ();
		for (int bar = 0; bar <= visible_bars; ++bar) {
			const double x = left + bar * px_bar;
			cr->set_source_rgba (0.20, 0.26, 0.35, bar % 4 == 0 ? 0.75 : 0.36);
			cr->rectangle (x, 0, 1, h); cr->fill ();
			if (bar < visible_bars) {
				cr->set_source_rgb (0.48, 0.56, 0.68);
				cr->select_font_face ("Sans", Cairo::FONT_SLANT_NORMAL, Cairo::FONT_WEIGHT_BOLD);
				cr->set_font_size (10); cr->move_to (x + 4, 19);
				cr->show_text (std::to_string (bar + 1));
			}
		}
		cr->set_source_rgb (0.055, 0.071, 0.10); cr->rectangle (0, ruler, left, h - ruler); cr->fill ();

		/* lanes: Track List integrada à esquerda */
		for (size_t i = 0; i < ss.size (); ++i) {
			const double y = ruler + i * lane_h;
			cr->set_source_rgba (i % 2 ? 0.045 : 0.055, i % 2 ? 0.060 : 0.070, i % 2 ? 0.085 : 0.10, 1.0);
			cr->rectangle (left, y, w - left, lane_h - 1); cr->fill ();
			cr->set_source_rgb (0.12, 0.16, 0.22); cr->rectangle (0, y + lane_h - 1, w, 1); cr->fill ();
			const unsigned int c = ss[i].color ? ss[i].color : (ss[i].is_midi ? 0xb98bffffu : 0x34e1e1ffu);
			cr->set_source_rgba (((c >> 24) & 255) / 255.0, ((c >> 16) & 255) / 255.0, ((c >> 8) & 255) / 255.0, 0.85);
			cr->rectangle (8, y + 11, 4, lane_h - 22); cr->fill ();
			cr->set_source_rgb (0.82, 0.86, 0.92); cr->set_font_size (11); cr->move_to (20, y + 24);
			std::string nm = ss[i].name; if (nm.size () > 18) { nm = nm.substr (0, 17) + "…"; }
			cr->show_text (nm);
			cr->set_source_rgb (0.42, 0.48, 0.58); cr->set_font_size (9); cr->move_to (20, y + 41);
			cr->show_text (ss[i].is_master ? "MASTER" : (ss[i].is_midi ? "MIDI" : "AUDIO"));
		}

		/* Clips são regiões materiais de Playlists: uma instância desenhada na
		 * lane dela, não uma simulação visual. */
		const std::vector<ClipInfo> clips = _daw->attached () ? _daw->clips () : std::vector<ClipInfo> ();
		for (size_t i = 0; i < clips.size (); ++i) {
			if (clips[i].route_idx < 0 || (size_t) clips[i].route_idx >= ss.size ()) { continue; }
			const double y = ruler + clips[i].route_idx * lane_h + 7;
			const double x = left + ((double) clips[i].start / samples_bar) * px_bar;
			const double end = left + ((double) clips[i].end / samples_bar) * px_bar;
			if (end <= left || x >= w) { continue; }
			const unsigned int c = ss[(size_t) clips[i].route_idx].color ? ss[(size_t) clips[i].route_idx].color : 0x34e1e1ffu;
			const double bx = std::max (left, x), bw = std::max (4.0, std::min (w, end) - bx), bh = lane_h - 14;
			const bool selected = (int) i == _arrange_selected_clip;
			cr->set_source_rgba (((c >> 24) & 255) / 255.0, ((c >> 16) & 255) / 255.0, ((c >> 8) & 255) / 255.0, selected ? 0.62 : 0.38);
			cr->rectangle (bx, y, bw, bh); cr->fill ();
			cr->set_source_rgba (((c >> 24) & 255) / 255.0, ((c >> 16) & 255) / 255.0, ((c >> 8) & 255) / 255.0, 0.95);
			cr->rectangle (bx, y, 3, bh); cr->fill ();
			if (selected) { cr->set_source_rgb (1.0, 0.24, 0.55); cr->set_line_width (2); cr->rectangle (bx + 1, y + 1, bw - 2, bh - 2); cr->stroke (); }
			/* Região de áudio: mini-waveform baseada nos samples verdadeiros da
			 * AudioSource. É uma prévia segura no canvas; não aciona playback. */
			if (!clips[i].is_midi) {
				const int columns = std::max (1, (int) std::min (180.0, bw - 10.0));
				std::string wave_err;
				const std::vector<float> peaks = _daw->audio_waveform ((int) i, columns, wave_err);
				if (!peaks.empty ()) {
					cr->set_source_rgba (0.90, 0.96, 1.0, 0.48); cr->set_line_width (1);
					for (size_t col = 0; col < peaks.size (); ++col) {
						const double cx = bx + 5 + col * std::max (1.0, (bw - 10) / peaks.size ());
						const double amp = std::min ((bh - 10) / 2.0, peaks[col] * (bh - 10) / 2.0);
						cr->move_to (cx, y + bh / 2.0 - amp); cr->line_to (cx, y + bh / 2.0 + amp);
					}
					cr->stroke ();
				}
			}
			cr->set_source_rgb (0.91, 0.95, 1.0); cr->set_font_size (10); cr->move_to (std::max (left, x) + 8, y + 17);
			cr->show_text (clips[i].is_midi ? "♫ " + clips[i].name : clips[i].name);
		}

		/* Arranger Sections atravessam todas as lanes: são globais, igual Studio One. */
		const double lanes_h = std::max (0.0, h - ruler);
		for (size_t i = 0; i < ms.size (); ++i) {
			if (ms[i].kind != "REGIÃO") { continue; }
			const double x = left + ((double) ms[i].start / samples_bar) * px_bar;
			const double end = left + ((double) ms[i].end / samples_bar) * px_bar;
			if (end <= left || x >= w) { continue; }
			const unsigned int pal[] = { 0xff3d8bu, 0x34e1e1u, 0xb98bffu, 0xffb020u, 0x25e08au };
			const unsigned int p = pal[i % 5];
			cr->set_source_rgba (((p >> 16) & 255) / 255.0, ((p >> 8) & 255) / 255.0, (p & 255) / 255.0, 0.16);
			cr->rectangle (std::max (left, x), ruler, std::min (w, end) - std::max (left, x), lanes_h); cr->fill ();
			cr->set_source_rgba (((p >> 16) & 255) / 255.0, ((p >> 8) & 255) / 255.0, (p & 255) / 255.0, 0.9);
			cr->rectangle (std::max (left, x), ruler + 2, 3, lanes_h - 4); cr->fill ();
			cr->set_font_size (10); cr->move_to (std::max (left, x) + 7, ruler + 17); cr->show_text (ms[i].name);
		}

		/* playhead */
		const long long pos = _daw->attached () ? _daw->position () : 0;
		const double px = left + ((double) pos / samples_bar) * px_bar;
		if (px >= left && px <= w) {
			cr->set_source_rgb (1.0, 0.24, 0.55); cr->rectangle (px, 0, 2, h); cr->fill ();
			cr->move_to (px - 5, 0); cr->line_to (px + 7, 0); cr->line_to (px + 1, 8); cr->close_path (); cr->fill ();
		}
		return true;
	});
	_arrange_canvas->signal_button_press_event ().connect ([this] (GdkEventButton* ev) -> bool {
		if (!_daw->attached () || ev->x < 150.0) { return false; }
		const Gtk::Allocation a = _arrange_canvas->get_allocation ();
		const double sr = _engine->session_sample_rate () > 0 ? _engine->session_sample_rate () : 48000.0;
		const double bpm = _daw->bpm () > 1.0 ? _daw->bpm () : 120.0;
		const double samples_bar = sr * (60.0 / bpm) * _daw->meter_beats ();
		const int visible_bars = _arrange_zoom ? (int) std::lround (_arrange_zoom->get_value ()) : 32;
		const double px_bar = std::max (10.0, ((double) a.get_width () - 150.0) / visible_bars);
		const double ruler = 30.0, lane_h = std::max (52.0, ((double) a.get_height () - ruler) / std::max (1, (int) _daw->strips ().size ()));
		const std::vector<ClipInfo> clips = _daw->clips ();
		for (size_t i = 0; i < clips.size (); ++i) {
			const double y = ruler + clips[i].route_idx * lane_h + 7;
			const double x = 150.0 + ((double) clips[i].start / samples_bar) * px_bar;
			const double end = 150.0 + ((double) clips[i].end / samples_bar) * px_bar;
			if (ev->y >= y && ev->y <= y + lane_h - 14 && ev->x >= x && ev->x <= end) {
				_arrange_selected_clip = (int) i;
				_arrange_drag_clip = (int) i;
				_arrange_drag_resize = ev->x >= end - 10.0;
				const long long at = (long long) std::llround (((ev->x - 150.0) / px_bar) * samples_bar);
				_arrange_drag_offset = at - clips[i].start;
				refresh_arrange (); return true;
			}
		}
		_arrange_selected_clip = -1;
		_daw->locate ((long long) std::llround (((ev->x - 150.0) / px_bar) * samples_bar));
		refresh_transport (); refresh_arrange ();
		return true;
	});
	_arrange_canvas->signal_motion_notify_event ().connect ([this] (GdkEventMotion* ev) -> bool {
		if (_arrange_drag_clip < 0 || !_daw->attached ()) { return false; }
		const Gtk::Allocation a = _arrange_canvas->get_allocation ();
		const double sr = _engine->session_sample_rate () > 0 ? _engine->session_sample_rate () : 48000.0;
		const double bpm = _daw->bpm () > 1.0 ? _daw->bpm () : 120.0;
		const double samples_bar = sr * (60.0 / bpm) * _daw->meter_beats ();
		const int vb = _arrange_zoom ? (int) std::lround (_arrange_zoom->get_value ()) : 32;
		const double px_bar = std::max (10.0, ((double) a.get_width () - 150.0) / vb);
		long long at = (long long) std::llround (((ev->x - 150.0) / px_bar) * samples_bar);
		/* snap de um compasso, como o grid da DAW */
		at = std::max (0LL, (long long) std::llround ((double) at / samples_bar) * (long long) std::llround (samples_bar));
		std::string err;
		bool ok = false;
		if (_arrange_drag_resize) { ok = _daw->resize_clip (_arrange_drag_clip, at, err); }
		else { ok = _daw->move_clip (_arrange_drag_clip, at - _arrange_drag_offset, err); }
		if (!ok && !err.empty ()) { set_status (err, true); }
		refresh_arrange (); return true;
	});
	_arrange_canvas->signal_button_release_event ().connect ([this] (GdkEventButton*) -> bool {
		if (_arrange_drag_clip >= 0) { _arrange_drag_clip = -1; _arrange_drag_resize = false; set_status ("clip atualizado (snap por compasso)."); }
		return true;
	});

	Gtk::ScrolledWindow* sc = Gtk::manage (new Gtk::ScrolledWindow);
	sc->add (*_arrange_canvas); sc->set_policy (Gtk::POLICY_AUTOMATIC, Gtk::POLICY_AUTOMATIC); sc->set_vexpand (true);
	b->pack_start (*sc, true, true, 0);
	return b;
}

void
MainWindow::on_arranger_add ()
{
	if (!_daw->attached ()) { set_status ("abra uma sessão primeiro.", true); return; }
	std::string err;
	const std::string nome = _arrange_name ? std::string (_arrange_name->get_text ()) : std::string ();
	const int bars = _arrange_bars ? _arrange_bars->get_value_as_int () : 8;
	if (!_daw->add_arranger_section (nome, bars, err)) { set_status (err, true); return; }
	refresh_arrange (); refresh_markers ();
	set_status ("seção do Arranger criada no cursor: " + (nome.empty () ? std::string ("Seção") : nome));
}

void
MainWindow::on_arranger_midi_clip ()
{
	if (!_daw->attached () || !_arrange_midi_track) { set_status ("abra uma sessão primeiro.", true); return; }
	const int idx = _arrange_midi_track->get_active_row_number ();
	if (idx < 0) { set_status ("crie ou selecione uma trilha MIDI primeiro.", true); return; }
	/* O combo armazena a posição na lista de MIDI; resolver na ordem verdadeira
	 * das rotas evita que Audio/Master desloquem o índice. */
	const std::vector<RouteStrip> ss = _daw->strips ();
	int midi_n = -1, route_idx = -1;
	for (size_t i = 0; i < ss.size (); ++i) {
		if (!ss[i].is_midi || ss[i].is_master) { continue; }
		if (++midi_n == idx) { route_idx = (int) i; break; }
	}
	if (route_idx < 0) { set_status ("trilha MIDI selecionada não existe mais.", true); return; }
	std::string err;
	const std::string nome = _arrange_name ? std::string (_arrange_name->get_text ()) : std::string ();
	const int bars = _arrange_bars ? _arrange_bars->get_value_as_int () : 8;
	if (!_daw->add_midi_clip (route_idx, nome, bars, err)) { set_status (err, true); return; }
	refresh_arrange ();
	set_status ("clip MIDI real criado no cursor: " + (nome.empty () ? std::string ("MIDI clip") : nome));
}

void
MainWindow::on_arranger_clip_duplicate ()
{
	if (!_daw->attached () || _arrange_selected_clip < 0) { set_status ("selecione um clip no Arranjo primeiro.", true); return; }
	std::string err;
	if (!_daw->duplicate_clip (_arrange_selected_clip, err)) { set_status (err, true); return; }
	refresh_arrange (); set_status ("clip duplicado à direita.");
}

void
MainWindow::on_arranger_clip_delete ()
{
	if (!_daw->attached () || _arrange_selected_clip < 0) { set_status ("selecione um clip no Arranjo primeiro.", true); return; }
	std::string err;
	if (!_daw->remove_clip (_arrange_selected_clip, err)) { set_status (err, true); return; }
	_arrange_selected_clip = -1;
	refresh_arrange (); set_status ("clip apagado.");
}

void
MainWindow::on_arranger_import_audio ()
{
	if (!_daw->attached () || !_arrange_audio_track) { set_status ("abra uma sessão primeiro.", true); return; }
	const int chosen = _arrange_audio_track->get_active_row_number ();
	if (chosen < 0) { set_status ("crie ou selecione uma trilha de áudio primeiro.", true); return; }
	Gtk::FileChooserDialog choose (*this, "Importar arquivo de áudio", Gtk::FILE_CHOOSER_ACTION_OPEN);
	choose.add_button ("CANCELAR", Gtk::RESPONSE_CANCEL);
	choose.add_button ("IMPORTAR", Gtk::RESPONSE_OK);
	Glib::RefPtr<Gtk::FileFilter> filter = Gtk::FileFilter::create ();
	filter->set_name ("Áudio suportado pelo Ardour");
	filter->add_mime_type ("audio/*"); filter->add_pattern ("*.wav"); filter->add_pattern ("*.flac"); filter->add_pattern ("*.aiff"); filter->add_pattern ("*.mp3"); filter->add_pattern ("*.ogg");
	choose.add_filter (filter);
	if (choose.run () != Gtk::RESPONSE_OK) { return; }
	const std::string path = choose.get_filename ();
	const std::vector<RouteStrip> ss = _daw->strips ();
	int audio_n = -1, route_idx = -1;
	for (size_t i = 0; i < ss.size (); ++i) {
		if (!ss[i].is_track || ss[i].is_midi || ss[i].is_master) { continue; }
		if (++audio_n == chosen) { route_idx = (int) i; break; }
	}
	if (route_idx < 0) { set_status ("trilha de áudio selecionada não existe mais.", true); return; }
	std::string err;
	if (!_daw->import_audio_clip (route_idx, path, err)) { set_status (err, true); return; }
	refresh_arrange (); set_status ("áudio importado como Region no cursor; nenhuma reprodução iniciada.");
}

void
MainWindow::on_piano_roll_open ()
{
	if (!_daw->attached () || _arrange_selected_clip < 0) {
		set_status ("selecione um clip MIDI no Arranjo antes de abrir o Piano Roll.", true); return;
	}
	std::string err;
	std::vector<MidiNoteInfo> check = _daw->midi_notes (_arrange_selected_clip, err);
	if (!err.empty ()) { set_status (err, true); return; }
	_piano_clip = _arrange_selected_clip;
	_piano_selected_note = -1;
	if (_piano_window) {
		_piano_window->present ();
		if (_piano_canvas) { _piano_canvas->queue_draw (); }
		return;
	}

	_piano_window = new Gtk::Window;
	_piano_window->set_title ("SPECTER — Piano Roll");
	_piano_window->set_default_size (820, 520);
	_piano_window->set_transient_for (*this);
	_piano_window->property_destroy_with_parent () = true;
	cls (*_piano_window, "spook-window");
	Gtk::Box* root = Gtk::manage (new Gtk::Box (Gtk::ORIENTATION_VERTICAL, 7));
	root->set_border_width (10); cls (*root, "spook-root");
	Gtk::Box* head = Gtk::manage (new Gtk::Box (Gtk::ORIENTATION_HORIZONTAL, 8));
	head->pack_start (*strong ("PIANO ROLL · MIDI REAL"), false, false, 0);
	_piano_state = dim ("Clique num espaço para inserir uma nota · botão direito apaga · 16 beats visíveis");
	_piano_state->set_hexpand (true); head->pack_start (*_piano_state, true, true, 0);
	Gtk::Button* close = push ("FECHAR", nullptr);
	close->signal_clicked ().connect ([this] () { if (_piano_window) { _piano_window->hide (); } });
	head->pack_start (*close, false, false, 0); root->pack_start (*head, false, false, 0);
	_piano_canvas = Gtk::manage (new Gtk::DrawingArea);
	_piano_canvas->set_size_request (-1, 420); _piano_canvas->set_vexpand (true);
	cls (*_piano_canvas, "arrange-canvas");
	_piano_canvas->add_events (Gdk::BUTTON_PRESS_MASK | Gdk::BUTTON_RELEASE_MASK | Gdk::POINTER_MOTION_MASK);
	_piano_canvas->signal_draw ().connect ([this] (Cairo::RefPtr<Cairo::Context> const& cr) -> bool {
		const Gtk::Allocation a = _piano_canvas->get_allocation ();
		const double w = a.get_width (), h = a.get_height (), left = 66.0, top = 25.0;
		const int low = 48, rows = 24; /* C3 .. B4, duas oitavas úteis */
		const double row_h = std::max (12.0, (h - top) / rows), beat_w = std::max (15.0, (w - left) / 16.0);
		cr->set_source_rgb (0.025, 0.032, 0.045); cr->paint ();
		for (int r = 0; r < rows; ++r) {
			const int pitch = low + rows - 1 - r, pc = pitch % 12;
			const bool black = pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
			const double y = top + r * row_h;
			cr->set_source_rgb (black ? 0.045 : 0.10, black ? 0.055 : 0.12, black ? 0.075 : 0.15);
			cr->rectangle (0, y, w, row_h - 1); cr->fill ();
			cr->set_source_rgb (black ? 0.15 : 0.26, black ? 0.18 : 0.30, black ? 0.23 : 0.37);
			cr->rectangle (0, y, left - 1, row_h - 1); cr->fill ();
			if (pc == 0) { cr->set_source_rgb (0.70, 0.76, 0.86); cr->set_font_size (10); cr->move_to (8, y + row_h - 4); cr->show_text (std::string ("C") + std::to_string (pitch / 12 - 1)); }
		}
		for (int b = 0; b <= 16; ++b) {
			const double x = left + b * beat_w;
			cr->set_source_rgba (0.25, 0.32, 0.43, b % 4 == 0 ? 0.85 : 0.38); cr->rectangle (x, 0, 1, h); cr->fill ();
			if (b < 16) { cr->set_source_rgb (0.48, 0.56, 0.68); cr->set_font_size (10); cr->move_to (x + 4, 17); cr->show_text (std::to_string (b + 1)); }
		}
		std::string note_err; const std::vector<MidiNoteInfo> notes = _daw->midi_notes (_piano_clip, note_err);
		for (size_t i = 0; i < notes.size (); ++i) {
			if (notes[i].pitch < low || notes[i].pitch >= low + rows) { continue; }
			const double x = left + notes[i].start_beats * beat_w;
			const double y = top + (low + rows - 1 - notes[i].pitch) * row_h + 2;
			const double nw = std::max (5.0, notes[i].length_beats * beat_w - 2);
			const bool sel = (int) i == _piano_selected_note;
			cr->set_source_rgba (1.0, 0.24, 0.55, sel ? 0.90 : 0.70); cr->rectangle (x, y, nw, row_h - 4); cr->fill ();
			cr->set_source_rgb (1.0, 0.75, 0.86); cr->set_line_width (sel ? 2 : 1); cr->rectangle (x + 1, y + 1, nw - 2, row_h - 6); cr->stroke ();
		}
		if (_piano_state) { _piano_state->set_text (note_err.empty () ? std::to_string ((long) notes.size ()) + " nota(s) · clique cria 1 beat · direito apaga" : note_err); }
		return true;
	});
	_piano_canvas->signal_button_press_event ().connect ([this] (GdkEventButton* ev) -> bool {
		if (!_daw->attached () || _piano_clip < 0 || ev->x < 66 || ev->y < 25) { return false; }
		const Gtk::Allocation a = _piano_canvas->get_allocation ();
		const int low = 48, rows = 24; const double top = 25.0, left = 66.0;
		const double row_h = std::max (12.0, ((double) a.get_height () - top) / rows), beat_w = std::max (15.0, ((double) a.get_width () - left) / 16.0);
		const int pitch = low + rows - 1 - (int) ((ev->y - top) / row_h);
		const double beat = std::max (0.0, std::floor ((ev->x - left) / beat_w));
		std::string err; std::vector<MidiNoteInfo> notes = _daw->midi_notes (_piano_clip, err);
		int hit = -1;
		for (size_t i = 0; i < notes.size (); ++i) {
			if (notes[i].pitch == pitch && beat >= notes[i].start_beats && beat < notes[i].start_beats + notes[i].length_beats) { hit = (int) i; break; }
		}
		if (ev->button == 3 && hit >= 0) {
			if (!_daw->remove_midi_note (_piano_clip, notes[(size_t) hit].id, err)) { set_status (err, true); }
			else { _piano_selected_note = -1; set_status ("nota MIDI apagada (Ctrl+Z restaura)."); }
		} else if (hit >= 0) {
			_piano_selected_note = hit;
			_piano_dragging = ev->button == 1;
			_piano_drag_resize = beat >= notes[(size_t) hit].start_beats + notes[(size_t) hit].length_beats - 0.35;
			_piano_drag_delta = beat - notes[(size_t) hit].start_beats;
			set_status (_piano_drag_resize ? "arraste a borda direita para redimensionar a nota." : "arraste para mover a nota; snap de 1 beat.");
		} else if (ev->button == 1) {
			if (!_daw->add_midi_note (_piano_clip, pitch, beat, 1.0, _piano_velocity, err)) { set_status (err, true); }
			else { _piano_selected_note = -1; set_status ("nota MIDI adicionada (velocidade 100; Ctrl+Z desfaz)."); }
		}
		_piano_canvas->queue_draw (); refresh_arrange (); return true;
	});
	_piano_canvas->signal_motion_notify_event ().connect ([this] (GdkEventMotion* ev) -> bool {
		if (!_piano_dragging || !_daw->attached () || _piano_clip < 0 || !_piano_canvas) { return false; }
		const Gtk::Allocation a = _piano_canvas->get_allocation (); const double left = 66.0;
		const double beat_w = std::max (15.0, ((double) a.get_width () - left) / 16.0);
		const double beat = std::max (0.0, std::floor ((ev->x - left) / beat_w));
		std::string err; std::vector<MidiNoteInfo> notes = _daw->midi_notes (_piano_clip, err);
		if (_piano_selected_note < 0 || _piano_selected_note >= (int) notes.size ()) { _piano_dragging = false; return false; }
		bool ok = _piano_drag_resize
			? _daw->resize_midi_note (_piano_clip, notes[(size_t) _piano_selected_note].id, beat - notes[(size_t) _piano_selected_note].start_beats, err)
			: _daw->move_midi_note (_piano_clip, notes[(size_t) _piano_selected_note].id, beat - _piano_drag_delta, err);
		if (!ok && !err.empty ()) { set_status (err, true); }
		_piano_canvas->queue_draw (); refresh_arrange (); return true;
	});
	_piano_canvas->signal_button_release_event ().connect ([this] (GdkEventButton*) -> bool {
		if (_piano_dragging) { _piano_dragging = false; set_status ("nota MIDI atualizada (snap de 1 beat; Ctrl+Z desfaz)."); }
		return true;
	});
	root->pack_start (*_piano_canvas, true, true, 0);
	_piano_window->add (*root);
	_piano_window->signal_delete_event ().connect ([this] (GdkEventAny*) -> bool { if (_piano_window) { _piano_window->hide (); } return true; });
	_piano_window->show_all ();
}

void
MainWindow::refresh_arrange ()
{
	if (!_arrange_canvas) { return; }
	if (!_daw->attached ()) {
		if (_arrange_state) { _arrange_state->set_text ("abra uma sessão para criar seções."); }
		_arrange_canvas->queue_draw (); return;
	}
	const std::vector<RouteStrip> ss = _daw->strips ();
	const std::vector<MarkerInfo> ms = _daw->markers ();
	const std::vector<ClipInfo> cs = _daw->clips ();
	int secoes = 0; for (size_t i = 0; i < ms.size (); ++i) { if (ms[i].kind == "REGIÃO") { ++secoes; } }
	if (_arrange_midi_track) {
		const Glib::ustring anterior = _arrange_midi_track->get_active_text ();
		_arrange_midi_track->remove_all ();
		int ativo = -1, n = 0;
		for (size_t i = 0; i < ss.size (); ++i) {
			if (!ss[i].is_midi || ss[i].is_master) { continue; }
			_arrange_midi_track->append (ss[i].name);
			if (ss[i].name == anterior) { ativo = n; }
			++n;
		}
		if (ativo >= 0) { _arrange_midi_track->set_active (ativo); }
		else if (n > 0) { _arrange_midi_track->set_active (0); }
	}
	if (_arrange_audio_track) {
		const Glib::ustring anterior = _arrange_audio_track->get_active_text ();
		_arrange_audio_track->remove_all ();
		int ativo = -1, n = 0;
		for (size_t i = 0; i < ss.size (); ++i) {
			if (!ss[i].is_track || ss[i].is_midi || ss[i].is_master) { continue; }
			_arrange_audio_track->append (ss[i].name);
			if (ss[i].name == anterior) { ativo = n; }
			++n;
		}
		if (ativo >= 0) { _arrange_audio_track->set_active (ativo); }
		else if (n > 0) { _arrange_audio_track->set_active (0); }
	}
	if (_arrange_selected_clip >= (int) cs.size ()) { _arrange_selected_clip = -1; }
	if (_arrange_state) {
		const int visible_bars = _arrange_zoom ? (int) std::lround (_arrange_zoom->get_value ()) : 32;
		_arrange_state->set_text (std::to_string ((long) ss.size ()) + " lanes · " + std::to_string ((long) cs.size ()) +
		                         " clips · " + std::to_string (secoes) + " seções · " + std::to_string (visible_bars) + " compassos visíveis · " + std::to_string ((int) _daw->bpm ()) + " BPM");
	}
	_arrange_canvas->queue_draw ();
}

/* ================================================== TRANSPORTE: ações / LCD */

void
MainWindow::refresh_transport ()
{
	const bool ok = _daw->attached ();

	/* Os strings do LCD só são refeitos quando a posição muda: em STOP o
	 * playhead fica parado e reconstruir/set_text a cada 500 ms só faria o
	 * GTK pedir relayout à toa. */
	const long long sp = ok ? _daw->position () : -1;
	if (sp != _lcd_pos) {
		_lcd_pos = sp;
		if (_lcd_bars)    { _lcd_bars->set_text (ok ? _daw->pos_bars () : "—"); }
		if (_lcd_time)    { _lcd_time->set_text (ok ? _daw->pos_time () : "—"); }
		if (_lcd_samples) { _lcd_samples->set_text (ok ? _daw->pos_samples () : "—"); }
	}

	if (_sig_label) {
		txt (_sig_label, ok ? (std::to_string (_daw->meter_beats ()) + "/" + std::to_string (_daw->meter_div ())) : "—");
	}

	if (_bpm_spin && ok && !_bpm_spin->has_focus ()) {
		const double b = _daw->bpm ();
		if (b >= 1.0 && std::abs (b - _bpm_spin->get_value ()) > 0.05) {
			_bpm_guard = true;
			_bpm_spin->set_value (b);
			_bpm_guard = false;
		}
	}

	_toggle_guard = true;
	if (_tb_loop)  { _tb_loop->set_active (ok && _daw->loop_active ()); }
	if (_tb_click) { _tb_click->set_active (ok && _daw->click_on ()); }
	_toggle_guard = false;

	if (_tn_rec) {
		Glib::RefPtr<Gtk::StyleContext> sc = _tn_rec->get_style_context ();
		sc->remove_class ("armed");
		if (ok && _daw->record_enabled ()) { sc->add_class ("armed"); }
	}

	if (_tn_motor) { txt (_tn_motor, _engine->engine_running () ? "MOTOR ■" : "MOTOR ▶"); }

	if (_tr_state) {
		std::string st = "NO SESSION";
		if (ok) {
			st = _daw->rolling () ? "PLAYING" : "STOPPED";
			if (_daw->record_enabled ()) { st += "  ·  REC"; }
			if (_daw->loop_active ())    { st += "  ·  LOOP"; }
		}
		txt (_tr_state, st);
	}
}

void
MainWindow::on_transport_play ()
{
	if (!_daw->attached ()) { set_status ("suba o motor e abra uma sessão primeiro.", true); return; }
	_daw->toggle_roll ();
	refresh_transport ();
}

void
MainWindow::on_transport_stop ()
{
	if (!_daw->attached ()) { return; }
	_daw->stop ();
	refresh_transport ();
}

void
MainWindow::on_transport_rec ()
{
	if (!_daw->attached ()) { set_status ("abra uma sessão primeiro.", true); return; }
	const bool on = !_daw->record_enabled ();
	_daw->set_record_enabled (on);
	set_status (on ? "gravação ARMADA — arme trilhas no MIXER e dê play."
	               : "gravação desarmada.");
	refresh_transport ();
	refresh_mixer ();
}

void
MainWindow::on_transport_start () { if (_daw->attached ()) { _daw->goto_start (); refresh_transport (); } }
void
MainWindow::on_transport_end   () { if (_daw->attached ()) { _daw->goto_end ();   refresh_transport (); } }
void
MainWindow::on_rewind          () { if (_daw->attached ()) { _daw->rewind ();     refresh_transport (); } }
void
MainWindow::on_ffwd            () { if (_daw->attached ()) { _daw->ffwd ();       refresh_transport (); } }

void
MainWindow::on_loop ()
{
	if (_toggle_guard) { return; }
	if (!_daw->attached ()) { set_status ("abra uma sessão primeiro.", true); return; }
	_daw->toggle_loop ();
	set_status (_daw->loop_active () ? "loop ligado (4 compassos a partir do cursor)." : "loop desligado.");
	refresh_transport ();
}

void
MainWindow::on_click ()
{
	if (_toggle_guard) { return; }
	if (!_daw->attached ()) { set_status ("abra uma sessão primeiro.", true); return; }
	_daw->toggle_click ();
	set_status (std::string ("metrônomo ") + (_daw->click_on () ? "ligado" : "desligado"));
	refresh_transport ();
}

void
MainWindow::on_marker_add ()
{
	if (!_daw->attached ()) { set_status ("abra uma sessão primeiro.", true); return; }
	const std::string mn = _marker_name ? std::string (_marker_name->get_text ()) : std::string ();
	_daw->add_marker (mn);
	refresh_markers ();
	set_status ("marcador inserido no cursor.");
}

void
MainWindow::on_marker_remove ()
{
	if (!_daw->attached ()) { set_status ("abra uma sessão primeiro.", true); return; }
	_daw->remove_marker_at_playhead ();
	refresh_markers ();
}

void
MainWindow::on_undo ()
{
	if (!_daw->attached ()) { set_status ("abra uma sessão primeiro.", true); return; }
	_daw->undo ();
	refresh_routes (); refresh_markers (); refresh_mixer ();
	set_status ("desfeito.");
}

void
MainWindow::on_redo ()
{
	if (!_daw->attached ()) { set_status ("abra uma sessão primeiro.", true); return; }
	_daw->redo ();
	refresh_routes (); refresh_markers (); refresh_mixer ();
	set_status ("refeito.");
}

void
MainWindow::on_save ()
{
	if (!_daw->attached ()) { set_status ("abra uma sessão primeiro.", true); return; }
	std::string err;
	if (!_daw->save (err)) { set_status (err, true); return; }
	set_status ("sessão salva.");
}

void
MainWindow::on_panic ()
{
	if (_daw->attached ()) { _daw->midi_panic (); }
	set_status ("pânico: notas MIDI cortadas.");
}

void
MainWindow::on_bpm_apply ()
{
	if (_bpm_guard || !_daw->attached ()) { return; }
	std::string err;
	if (!_daw->set_bpm (_bpm_spin->get_value (), err)) { set_status (err, true); return; }
	char b[40];
	std::snprintf (b, sizeof (b), "BPM = %.1f", _bpm_spin->get_value ());
	set_status (b);
}

/* Atalhos no estilo Reaper. Não rouba tecla de quem está digitando. */
bool
MainWindow::on_key_press (GdkEventKey* event)
{
	if (!_daw->attached ()) { return false; }

	Gtk::Widget* f = get_focus ();
	if (dynamic_cast<Gtk::Entry*> (f) || dynamic_cast<Gtk::SpinButton*> (f) ||
	    dynamic_cast<Gtk::TextView*> (f) || dynamic_cast<Gtk::Scale*> (f) ||
	    dynamic_cast<Gtk::TreeView*> (f)) {
		return false;
	}

	if (event->keyval == GDK_KEY_F2) {
		/* Logic/Studio One: F2 leva direto à Track List, onde todo nome é
		 * um campo editável. Sem seleção implícita para nunca renomear a faixa
		 * errada por acidente. */
		_nb->set_current_page (2);
		set_status ("Track List aberta — clique no nome da trilha e aperte Enter.");
		return true;
	}

	if (event->state & GDK_CONTROL_MASK) {
		switch (event->keyval) {
		case GDK_KEY_z: case GDK_KEY_Z: on_undo (); return true;
		case GDK_KEY_y: case GDK_KEY_Y: on_redo (); return true;
		case GDK_KEY_s: case GDK_KEY_S: on_save (); return true;
		default: break;
		}
		return false;
	}

	switch (event->keyval) {
	case GDK_KEY_space:  on_transport_play (); return true;
	case GDK_KEY_Home:   on_transport_start (); return true;
	case GDK_KEY_End:    on_transport_end (); return true;
	case GDK_KEY_Delete: on_marker_remove (); return true;
	case GDK_KEY_r: case GDK_KEY_R: on_transport_rec (); return true;
	case GDK_KEY_l: case GDK_KEY_L: on_loop (); return true;
	case GDK_KEY_m: case GDK_KEY_M: on_marker_add (); return true;
	default: break;
	}
	return false;
}

/* ================================================================== AÇÕES */

void
MainWindow::do_action (std::string const& id)
{
	if (id == "Transport/Play")            { on_transport_play (); }
	else if (id == "Transport/Stop")       { on_transport_stop (); }
	else if (id == "Transport/RecordArm")  { on_transport_rec (); }
	else if (id == "Transport/GotoStart")  { on_transport_start (); }
	else if (id == "Transport/GotoEnd")    { on_transport_end (); }
	else if (id == "Transport/Rewind")     { on_rewind (); }
	else if (id == "Transport/Forward")    { on_ffwd (); }
	else if (id == "Transport/Back1Bar")   { if (_daw->attached ()) { _daw->jump_bars (-1); refresh_transport (); } }
	else if (id == "Transport/Fwd1Bar")    { if (_daw->attached ()) { _daw->jump_bars (1);  refresh_transport (); } }
	else if (id == "Transport/Loop")       { on_loop (); }
	else if (id == "Transport/Click")      { on_click (); }
	else if (id == "Transport/MidiPanic")  { on_panic (); }
	else if (id == "Marker/Add")           { on_marker_add (); }
	else if (id == "Marker/RemoveAtCursor"){ on_marker_remove (); }
	else if (id == "Marker/Next")          { if (_daw->attached ()) { _daw->next_marker (); refresh_transport (); } }
	else if (id == "Marker/Prev")          { if (_daw->attached ()) { _daw->prev_marker (); refresh_transport (); } }
	else if (id == "Edit/Undo")            { on_undo (); }
	else if (id == "Edit/Redo")            { on_redo (); }
	else if (id == "Session/Save")         { on_save (); }
	else if (id == "Session/Open")         { _nb->set_current_page (2); }
	else if (id == "Arrange/Open")          { _nb->set_current_page (1); }
	else if (id == "Arrange/AddSection")    { on_arranger_add (); }
	else if (id == "Arrange/AddMidiClip")   { on_arranger_midi_clip (); }
	else if (id == "Arrange/OpenPianoRoll")  { on_piano_roll_open (); }
	else if (id == "Arrange/ImportAudio")    { on_arranger_import_audio (); }
	else if (id == "Track/NewAudio")       { on_track_new (); }
	else if (id == "Track/NewMidi")        { on_track_new_midi (); }
	else if (id == "Track/Rename")         { _nb->set_current_page (2); set_status ("edite o nome na Track List e aperte Enter."); }
	else if (id == "Track/RemoveLast")     { on_track_remove (); }
	else if (id == "Mixer/ArmAll")         { if (_daw->attached ()) { _daw->arm_all (true);  refresh_mixer (); } }
	else if (id == "Mixer/DisarmAll")      { if (_daw->attached ()) { _daw->arm_all (false); refresh_mixer (); } }
	else if (id == "Mixer/ClearSolo")      { if (_daw->attached ()) { _daw->clear_solo (); refresh_mixer (); } }
	else if (id == "Mixer/ResetGains")     {
		if (!_daw->attached ()) { return; }
		std::vector<RouteStrip> ss = _daw->strips ();
		std::string err;
		for (size_t i = 0; i < ss.size (); ++i) { _daw->set_gain ((int) i, 0.0, err); }
		refresh_mixer ();
	}
	else if (id == "Engine/Start")         { on_start (); }
	else if (id == "Engine/Stop")          { on_stop (); }
	else if (id == "Plugins/Cache")        { on_plugins_cache (); }
	else if (id == "Plugins/FullScan")     { on_plugins_full (); }
	else if (id == "View/Log")             { _nb->set_current_page (7); }
	else { set_status ("ação desconhecida: " + id, true); }
}

Gtk::Widget*
MainWindow::build_actions_tab ()
{
	class Cols : public Gtk::TreeModel::ColumnRecord {
	  public:
		Cols () { add (id); add (desc); add (key); }
		Gtk::TreeModelColumn<Glib::ustring> id, desc, key;
	};
	static Cols cols;

	_actions.clear ();
	_actions.push_back (std::make_pair (std::string ("Transport/Play"),            std::string ("Transporte: tocar / pausar")));
	_actions.push_back (std::make_pair (std::string ("Transport/Stop"),            std::string ("Transporte: parar")));
	_actions.push_back (std::make_pair (std::string ("Transport/RecordArm"),       std::string ("Transporte: armar gravação")));
	_actions.push_back (std::make_pair (std::string ("Transport/GotoStart"),       std::string ("Transporte: ir ao início")));
	_actions.push_back (std::make_pair (std::string ("Transport/GotoEnd"),         std::string ("Transporte: ir ao fim")));
	_actions.push_back (std::make_pair (std::string ("Transport/Rewind"),          std::string ("Transporte: rebobinar")));
	_actions.push_back (std::make_pair (std::string ("Transport/Forward"),         std::string ("Transporte: avançar")));
	_actions.push_back (std::make_pair (std::string ("Transport/Back1Bar"),        std::string ("Transporte: voltar 1 compasso")));
	_actions.push_back (std::make_pair (std::string ("Transport/Fwd1Bar"),         std::string ("Transporte: avançar 1 compasso")));
	_actions.push_back (std::make_pair (std::string ("Transport/Loop"),            std::string ("Transporte: ligar/desligar loop")));
	_actions.push_back (std::make_pair (std::string ("Transport/Click"),           std::string ("Transporte: metrônomo (click)")));
	_actions.push_back (std::make_pair (std::string ("Transport/MidiPanic"),       std::string ("Transporte: pânico MIDI")));
	_actions.push_back (std::make_pair (std::string ("Marker/Add"),                std::string ("Marcador: inserir no cursor")));
	_actions.push_back (std::make_pair (std::string ("Marker/RemoveAtCursor"),     std::string ("Marcador: remover o do cursor")));
	_actions.push_back (std::make_pair (std::string ("Marker/Next"),               std::string ("Marcador: próximo")));
	_actions.push_back (std::make_pair (std::string ("Marker/Prev"),               std::string ("Marcador: anterior")));
	_actions.push_back (std::make_pair (std::string ("Edit/Undo"),                 std::string ("Edição: desfazer")));
	_actions.push_back (std::make_pair (std::string ("Edit/Redo"),                 std::string ("Edição: refazer")));
	_actions.push_back (std::make_pair (std::string ("Session/Save"),              std::string ("Sessão: salvar")));
	_actions.push_back (std::make_pair (std::string ("Session/Open"),              std::string ("Sessão: abrir a aba SESSÃO")));
	_actions.push_back (std::make_pair (std::string ("Arrange/Open"),               std::string ("Arranjo: abrir Arrange View")));
	_actions.push_back (std::make_pair (std::string ("Arrange/AddSection"),         std::string ("Arranjo: adicionar seção no cursor")));
	_actions.push_back (std::make_pair (std::string ("Arrange/AddMidiClip"),        std::string ("Arranjo: criar clip MIDI na trilha selecionada")));
	_actions.push_back (std::make_pair (std::string ("Arrange/OpenPianoRoll"),     std::string ("Arranjo: abrir Piano Roll do clip MIDI selecionado")));
	_actions.push_back (std::make_pair (std::string ("Arrange/ImportAudio"),       std::string ("Arranjo: importar arquivo como clip de áudio")));
	_actions.push_back (std::make_pair (std::string ("Track/NewAudio"),            std::string ("Trilha: nova trilha de áudio")));
	_actions.push_back (std::make_pair (std::string ("Track/NewMidi"),             std::string ("Trilha: nova trilha MIDI")));
	_actions.push_back (std::make_pair (std::string ("Track/Rename"),              std::string ("Trilha: abrir a Track List para renomear")));
	_actions.push_back (std::make_pair (std::string ("Track/RemoveLast"),          std::string ("Trilha: remover a última")));
	_actions.push_back (std::make_pair (std::string ("Mixer/ArmAll"),              std::string ("Mixer: armar todas as trilhas")));
	_actions.push_back (std::make_pair (std::string ("Mixer/DisarmAll"),           std::string ("Mixer: desarmar todas")));
	_actions.push_back (std::make_pair (std::string ("Mixer/ClearSolo"),           std::string ("Mixer: limpar solos")));
	_actions.push_back (std::make_pair (std::string ("Mixer/ResetGains"),          std::string ("Mixer: zerar todos os ganhos")));
	_actions.push_back (std::make_pair (std::string ("Engine/Start"),              std::string ("Motor: iniciar")));
	_actions.push_back (std::make_pair (std::string ("Engine/Stop"),               std::string ("Motor: parar")));
	_actions.push_back (std::make_pair (std::string ("Plugins/Cache"),             std::string ("Plugins: varrer o cache")));
	_actions.push_back (std::make_pair (std::string ("Plugins/FullScan"),          std::string ("Plugins: varredura completa")));
	_actions.push_back (std::make_pair (std::string ("View/Log"),                  std::string ("Ver: aba LOG")));

	/* atalho de teclado por id (coluna ATALHO, como na action list do Reaper) */
	struct { const char* id; const char* key; } atalhos[] = {
		{ "Transport/Play", "espaço" }, { "Transport/RecordArm", "R" },
		{ "Transport/Loop", "L" }, { "Marker/Add", "M" },
		{ "Marker/RemoveAtCursor", "Del" }, { "Transport/GotoStart", "Home" },
		{ "Transport/GotoEnd", "End" }, { "Track/Rename", "F2" },
		{ "Edit/Undo", "Ctrl+Z" }, { "Edit/Redo", "Ctrl+Y" }, { "Session/Save", "Ctrl+S" },
	};

	Gtk::Box* b = box (Gtk::ORIENTATION_VERTICAL, 8, 10);

	Gtk::Box* head = box (Gtk::ORIENTATION_HORIZONTAL, 8);
	head->pack_start (*strong ("LISTA DE AÇÕES"), false, false, 0);
	_actions_filter = entry ("filtrar… (transporte, trilha, mixer, marcador…)");
	_actions_filter->set_hexpand (true);
	head->pack_start (*_actions_filter, true, true, 0);
	_actions_state = dim ("");
	head->pack_start (*_actions_state, false, false, 0);
	b->pack_start (*head, false, false, 0);
	b->pack_start (*dim ("Dois cliques (ou Enter na linha) executa. É a \"action list\" do Reaper — "
	                     "só com as ações que este painel controla de verdade."), false, false, 0);

	_actions_store = Gtk::ListStore::create (cols);
	for (size_t i = 0; i < _actions.size (); ++i) {
		Gtk::TreeModel::Row row = *_actions_store->append ();
		row[cols.id]   = _actions[i].first;
		row[cols.desc] = _actions[i].second;
		std::string k;
		for (size_t j = 0; j < sizeof (atalhos) / sizeof (atalhos[0]); ++j) {
			if (_actions[i].first == atalhos[j].id) { k = atalhos[j].key; break; }
		}
		row[cols.key] = k;
	}

	_actions_filter_model = Gtk::TreeModelFilter::create (_actions_store);
	_actions_filter_model->set_visible_func ([this] (Gtk::TreeModel::const_iterator const& it) -> bool {
		if (!_actions_filter) { return true; }
		const std::string q = Glib::ustring (_actions_filter->get_text ()).lowercase ();
		if (q.empty ()) { return true; }
		const Gtk::TreeModel::Row row = *it;
		std::string alvo = Glib::ustring (row[cols.id]).lowercase () + " " + Glib::ustring (row[cols.desc]).lowercase ();
		return alvo.find (q) != std::string::npos;
	});
	_actions_filter->signal_changed ().connect ([this] () { _actions_filter_model->refilter (); });

	_actions_view = Gtk::manage (new Gtk::TreeView (_actions_filter_model));
	cls (*_actions_view, "spook-actions");
	_actions_view->set_headers_visible (true);
	_actions_view->append_column ("AÇÃO (ID)", cols.id);
	_actions_view->append_column ("DESCRIÇÃO", cols.desc);
	_actions_view->append_column ("ATALHO", cols.key);
	_actions_view->set_tooltip_text ("Enter / duplo clique executa a ação");

	_actions_view->signal_row_activated ().connect ([this] (const Gtk::TreeModel::Path& path, Gtk::TreeViewColumn*) {
		Glib::RefPtr<Gtk::TreeModel> model = _actions_view->get_model ();
		Gtk::TreeModel::iterator it = model->get_iter (path);
		if (!it) { return; }
		const std::string id = Glib::ustring ((*it)[cols.id]);
		set_status ("ação: " + id);
		do_action (id);
	});

	Gtk::ScrolledWindow* sc = Gtk::manage (new Gtk::ScrolledWindow);
	sc->add (*_actions_view);
	sc->set_policy (Gtk::POLICY_AUTOMATIC, Gtk::POLICY_AUTOMATIC);
	sc->set_vexpand (true);
	Gtk::Box* c = card ("");
	c->pack_start (*sc, true, true, 0);
	b->pack_start (*c, true, true, 0);

	if (_actions_state) { _actions_state->set_text (std::to_string ((long) _actions.size ()) + " ações"); }

	return b;
}

/* --------------------------------------------------------- MARCADORES (UI) */

void
MainWindow::refresh_markers ()
{
	if (!_marker_box) { return; }
	clear (*_marker_box);

	if (!_daw->attached ()) {
		_marker_box->pack_start (*dim ("(sem sessão)"), false, false, 0);
		if (_marker_count) { _marker_count->set_text ("—"); }
		_marker_box->show_all ();
		return;
	}

	const std::vector<MarkerInfo> ms = _daw->markers ();
	if (_marker_count) {
		_marker_count->set_text (std::to_string ((long) ms.size ()) + (ms.size () == 1 ? " local" : " locais"));
	}

	if (ms.empty ()) {
		_marker_box->pack_start (*dim ("(nenhum marcador — use MARCADOR ou a tecla M)"), false, false, 0);
	}

	const long sr = _engine->session_sample_rate ();
	const double sr_d = sr > 0 ? (double) sr : 48000.0;

	for (size_t i = 0; i < ms.size (); ++i) {
		Gtk::Box* row = box (Gtk::ORIENTATION_HORIZONTAL, 8);
		cls (*row, "spook-mark-row");

		Gtk::Label* k = label (ms[i].kind, "spook-mark-kind");
		if (ms[i].kind == "LOOP")   { cls (*k, "loop"); }
		if (ms[i].kind == "REGIÃO") { cls (*k, "range"); }
		k->set_size_request (58, -1);
		row->pack_start (*k, false, false, 0);

		long long seg = (long long) ((double) ms[i].start / sr_d);
		char tbuf[32];
		std::snprintf (tbuf, sizeof (tbuf), "%02lld:%02lld:%02lld",
		               seg / 3600, (seg / 60) % 60, seg % 60);
		Gtk::Label* p = label (tbuf, "spook-mark-pos");
		p->set_size_request (72, -1);
		row->pack_start (*p, false, false, 0);

		row->pack_start (*label (ms[i].name.empty () ? "(sem nome)" : ms[i].name, "spook-mark-name"), true, true, 0);

		Gtk::Button* go = Gtk::manage (new Gtk::Button ("▶"));
		cls (*go, "spook-go");
		go->set_tooltip_text ("ir para este local");
		go->set_valign (Gtk::ALIGN_CENTER);
		const long long alvo = ms[i].start;
		go->signal_clicked ().connect ([this, alvo] () { _daw->locate (alvo); refresh_transport (); });
		row->pack_start (*go, false, false, 0);

		_marker_box->pack_start (*row, false, false, 0);
	}
	_marker_box->show_all ();
}

/* ------------------------------------------------------------------ ENGINE */

Gtk::Widget*
MainWindow::build_engine_tab ()
{
	Gtk::Box* b = box (Gtk::ORIENTATION_VERTICAL, 10, 12);

	Gtk::Box* c1 = card ("BACKEND & DISPOSITIVO");
	Gtk::Grid* g = Gtk::manage (new Gtk::Grid);
	g->set_row_spacing (8);
	g->set_column_spacing (12);

	int r = 0;
	g->attach (*strong ("Backend"), 0, r, 1, 1);
	_backend_combo = combo ();
	_backend_combo->set_hexpand (true);
	_backend_combo->signal_changed ().connect (sigc::mem_fun (*this, &MainWindow::on_backend_changed));
	g->attach (*_backend_combo, 1, r++, 1, 1);

	g->attach (*strong ("Dispositivo"), 0, r, 1, 1);
	_device_entry = entry ("vazio = default do backend");
	_device_entry->set_hexpand (true);
	g->attach (*_device_entry, 1, r++, 1, 1);
	c1->pack_start (*g, false, false, 0);

	Gtk::Box* btns = box (Gtk::ORIENTATION_HORIZONTAL, 8);
	_btn_start = push ("▶  INICIAR MOTOR", "cyan");
	_btn_start->signal_clicked ().connect (sigc::mem_fun (*this, &MainWindow::on_start));
	_btn_stop2 = push ("■  PARAR", nullptr);
	_btn_stop2->signal_clicked ().connect (sigc::mem_fun (*this, &MainWindow::on_stop));
	btns->pack_start (*_btn_start, false, false, 0);
	btns->pack_start (*_btn_stop2, false, false, 0);
	c1->pack_start (*btns, false, false, 0);
	c1->pack_start (*dim ("O backend \"None (Dummy)\" é o modo seguro: sobe o motor e processa "
	                      "silêncio, sem abrir dispositivo de som."), false, false, 0);
	b->pack_start (*c1, false, false, 0);

	Gtk::Box* c2 = card ("ESTADO DO MOTOR");
	Gtk::Box* leds = box (Gtk::ORIENTATION_HORIZONTAL, 8);
	_eng_led_init = led ("CORE", "off");
	_eng_led_back = led ("BACKEND", "off");
	_eng_led_run  = led ("MOTOR", "off");
	leds->pack_start (*_eng_led_init, false, false, 0);
	leds->pack_start (*_eng_led_back, false, false, 0);
	leds->pack_start (*_eng_led_run, false, false, 0);
	c2->pack_start (*leds, false, false, 0);
	_engine_state = strong ("parado.");
	c2->pack_start (*_engine_state, false, false, 0);
	b->pack_start (*c2, false, false, 0);

	/* monitor do sistema (dados reais): CPU e memória do Arch, cartões ALSA */
	Gtk::Box* c3 = card ("MONITOR DO SISTEMA");
	Gtk::Box* m = box (Gtk::ORIENTATION_VERTICAL, 6);
	m->pack_start (*meter_row ("CPU", &_eng_cpu_bar, &_eng_cpu_val), false, false, 0);
	m->pack_start (*meter_row ("MEMÓRIA", &_eng_mem_bar, &_eng_mem_val), false, false, 0);
	c3->pack_start (*m, false, false, 0);

	Gtk::Box* cds = box (Gtk::ORIENTATION_HORIZONTAL, 8);
	cds->pack_start (*label ("CARTÕES ALSA", "spook-ruler"), false, false, 0);
	_eng_cards = label ("—", "spook-cyan");
	cds->pack_start (*_eng_cards, false, false, 0);
	c3->pack_start (*cds, false, false, 0);
	b->pack_start (*c3, false, false, 0);

	b->pack_start (*Gtk::manage (new Gtk::Label ("")), true, true, 0);
	return b;
}

void
MainWindow::on_backend_changed ()
{
	set_status ("backend selecionado: " + _backend_combo->get_active_text ());
}

/* Ao trocar de aba, preenche o que ainda estiver vazio: o mixer, a lista de
 * cartões do mixer de hardware (leitura ALSA, instantânea) e o catálogo de
 * plugins (só cache). Assim a janela já mostra conteúdo sem obrigar a clicar. */
void
MainWindow::on_switch_page (Gtk::Widget* page, guint page_num)
{
	(void) page;

	switch (page_num) {
	case 0:                                   /* MIXER */
		refresh_mixer ();
		break;
	case 1:                                   /* ARRANJO */
		refresh_arrange ();
		break;
	case 2:                                   /* SESSÃO */
		refresh_routes ();
		refresh_markers ();
		break;
	case 3:                                   /* AÇÕES */
		if (_actions_filter_model) { _actions_filter_model->refilter (); }
		break;
	case 5:                                   /* SCARLETT */
		if (_card_combo && _card_combo->get_active_row_number () < 0 && _ctls.empty ()) {
			on_scarlett_scan ();
		}
		break;
	case 6:                                   /* PLUGINS */
		if (_plugins && plugin_list ().empty ()) { on_plugins_cache (); }
		break;
	default:
		break;
	}
}

void
MainWindow::on_start ()
{
	std::string err;
	const std::string be = _backend_combo->get_active_text ();
	const std::string dev = _device_entry->get_text ();

	if (!_engine->select_backend (be, dev, err)) { set_status (err, true); return; }
	if (!_engine->start_engine (err))            { set_status (err, true); return; }

	_engine_state->set_text ("no ar: " + _engine->backend_name ()
	                         + "  @ " + std::to_string ((long) _engine->sample_rate ()) + " Hz");
	set_status ("motor no ar com backend \"" + _engine->backend_name () + "\".");
	update_engine_visuals ();
	refresh_transport ();
}

void
MainWindow::on_stop ()
{
	_engine->stop_engine ();
	sync_daw ();
	_engine_state->set_text ("parado.");
	set_status ("motor parado.");
	refresh_mixer ();
	refresh_transport ();
	update_engine_visuals ();
}

/* ----------------------------------------------------------------- SESSÃO */

Gtk::Widget*
MainWindow::build_session_tab ()
{
	Gtk::Box* b = box (Gtk::ORIENTATION_VERTICAL, 10, 12);

	Gtk::Box* c1 = card ("SESSÃO");
	Gtk::Grid* g = Gtk::manage (new Gtk::Grid);
	g->set_row_spacing (8);
	g->set_column_spacing (12);
	int r = 0;
	g->attach (*strong ("Diretório"), 0, r, 1, 1);
	_sess_dir = entry ();
	_sess_dir->set_hexpand (true);
	g->attach (*_sess_dir, 1, r++, 1, 1);
	g->attach (*strong ("Nome"), 0, r, 1, 1);
	_sess_name = entry ("sessao");
	_sess_name->set_text ("sessao");
	_sess_name->set_hexpand (true);
	g->attach (*_sess_name, 1, r++, 1, 1);
	c1->pack_start (*g, false, false, 0);

	Gtk::Box* btns = box (Gtk::ORIENTATION_HORIZONTAL, 8);
	_btn_sess_open = push ("◆  ABRIR / CRIAR", "accent");
	_btn_sess_open->signal_clicked ().connect (sigc::mem_fun (*this, &MainWindow::on_session_open));
	_btn_sess_close = push ("FECHAR", nullptr);
	_btn_sess_close->signal_clicked ().connect (sigc::mem_fun (*this, &MainWindow::on_session_close));
	btns->pack_start (*_btn_sess_open, false, false, 0);
	btns->pack_start (*_btn_sess_close, false, false, 0);
	c1->pack_start (*btns, false, false, 0);
	c1->pack_start (*dim ("Para CRIAR, o diretório não pode existir — o Ardour cria tudo dentro "
	                      "(interchange/, peaks/, plugins/)."), false, false, 0);
	_sess_state = dim ("nenhuma sessão aberta.");
	c1->pack_start (*_sess_state, false, false, 0);
	b->pack_start (*c1, false, false, 0);

	Gtk::Box* c2 = card ("TRILHAS");
	Gtk::Box* trk = box (Gtk::ORIENTATION_HORIZONTAL, 8);
	_track_name = entry ("nome da trilha");
	_track_name->set_text ("Trilha 1");
	_track_name->set_hexpand (true);
	_btn_track = push ("+  ÁUDIO", "cyan");
	_btn_track->set_tooltip_text ("nova trilha de áudio");
	_btn_track->signal_clicked ().connect (sigc::mem_fun (*this, &MainWindow::on_track_new));
	_btn_track_midi = push ("+  MIDI", nullptr);
	_btn_track_midi->set_tooltip_text ("nova trilha MIDI (recebe o mesmo nome)");
	_btn_track_midi->signal_clicked ().connect (sigc::mem_fun (*this, &MainWindow::on_track_new_midi));
	_btn_track_rm = push ("REMOVER ÚLTIMA", "danger");
	_btn_track_rm->set_tooltip_text ("apaga a última rota que não é o master");
	_btn_track_rm->signal_clicked ().connect (sigc::mem_fun (*this, &MainWindow::on_track_remove));
	trk->pack_start (*_track_name, true, true, 0);
	trk->pack_start (*_btn_track, false, false, 0);
	trk->pack_start (*_btn_track_midi, false, false, 0);
	trk->pack_start (*_btn_track_rm, false, false, 0);
	c2->pack_start (*trk, false, false, 0);
	b->pack_start (*c2, false, false, 0);

	/* MARCADORES & REGIÕES (os \"locais\" da sessão) */
	Gtk::Box* cm = card ("MARCADORES & REGIÕES");
	Gtk::Box* mrow = box (Gtk::ORIENTATION_HORIZONTAL, 8);
	_marker_name = entry ("nome do marcador");
	_marker_name->set_text ("Marcador");
	_marker_name->set_hexpand (true);
	Gtk::Button* madd = push ("+  MARCADOR", "accent");
	madd->set_tooltip_text ("inserir no cursor (tecla M)");
	madd->signal_clicked ().connect (sigc::mem_fun (*this, &MainWindow::on_marker_add));
	Gtk::Button* mrem = push ("REMOVER @ CURSOR", nullptr);
	mrem->set_tooltip_text ("apaga o marcador que estiver sob o cursor");
	mrem->signal_clicked ().connect (sigc::mem_fun (*this, &MainWindow::on_marker_remove));
	mrow->pack_start (*_marker_name, true, true, 0);
	mrow->pack_start (*madd, false, false, 0);
	mrow->pack_start (*mrem, false, false, 0);
	cm->pack_start (*mrow, false, false, 0);
	_marker_count = dim ("—");
	cm->pack_start (*_marker_count, false, false, 0);
	_marker_box = box (Gtk::ORIENTATION_VERTICAL, 0);
	cm->pack_start (*scrolled (*_marker_box), true, true, 0);
	b->pack_start (*cm, true, true, 0);

	/* rotas como "channel strip" horizontal (nome + índice) */
	Gtk::Box* cr = card ("ROTAS DA SESSÃO");
	_routes_count = dim ("0");
	cr->pack_start (*_routes_count, false, false, 0);
	_routes = box (Gtk::ORIENTATION_VERTICAL, 0);
	cr->pack_start (*scrolled (*_routes), true, true, 0);
	b->pack_start (*cr, true, true, 0);
	return b;
}

void
MainWindow::on_session_open ()
{
	std::string err;
	if (!_engine->engine_running ()) {
		set_status ("suba o motor primeiro (aba ENGINE ou ▶ na barra de transporte).", true);
		return;
	}
	if (!_engine->open_session (_sess_dir->get_text (), _sess_name->get_text (), err)) {
		set_status (err, true);
		return;
	}
	_sess_state->set_text ("sessão \"" + _engine->session_name () + "\" @ "
	                       + std::to_string ((long) _engine->session_sample_rate ()) + " Hz");
	sync_daw ();
	refresh_routes ();
	refresh_markers ();
	refresh_mixer ();
	refresh_transport ();
	set_status ("sessão aberta — transporte e mixer liberados.");
	update_engine_visuals ();
}

void
MainWindow::on_session_close ()
{
	_engine->close_session ();
	sync_daw ();
	_sess_state->set_text ("nenhuma sessão aberta.");
	refresh_routes ();
	refresh_markers ();
	refresh_mixer ();
	refresh_transport ();
	set_status ("sessão fechada.");
	update_engine_visuals ();
}

void
MainWindow::on_track_new ()
{
	if (!_engine->session_open ()) { set_status ("nenhuma sessão aberta.", true); return; }
	std::vector<std::string> made = _engine->add_audio_track (_track_name->get_text ());
	sync_daw ();
	refresh_routes ();
	refresh_mixer ();
	refresh_transport ();
	set_status ("trilhas de áudio criadas: " + std::to_string ((long) made.size ()));
}

void
MainWindow::on_track_new_midi ()
{
	if (!_daw->attached ()) { set_status ("nenhuma sessão aberta.", true); return; }
	std::string n = _track_name ? std::string (_track_name->get_text ()) : std::string ();
	if (n.empty ()) { n = "MIDI"; }
	const std::vector<std::string> made = _daw->new_midi_track (n);
	refresh_routes ();
	refresh_mixer ();
	refresh_transport ();
	set_status (made.empty () ? std::string ("não deu para criar a trilha MIDI.")
	                          : ("trilha MIDI criada: " + made[0]));
}

void
MainWindow::on_route_rename (int idx, Gtk::Entry* input)
{
	if (!_daw->attached () || !input) { return; }
	const std::string pedido = input->get_text ();
	std::string err;
	if (!_daw->rename_route (idx, pedido, err)) {
		set_status ("não renomeou: " + err, true);
		const std::vector<std::string> nomes = _daw->route_names ();
		if (idx >= 0 && (size_t) idx < nomes.size ()) { input->set_text (nomes[(size_t) idx]); }
		return;
	}
	refresh_routes ();
	refresh_mixer ();
	set_status ("trilha renomeada para \"" + pedido + "\".");
}

void
MainWindow::on_route_duplicate (int idx)
{
	if (!_daw->attached ()) { set_status ("nenhuma sessão aberta.", true); return; }
	std::string err;
	if (!_daw->duplicate_route (idx, err)) { set_status ("não duplicou: " + err, true); return; }
	refresh_routes ();
	refresh_mixer ();
	set_status ("trilha duplicada — conteúdo não é copiado nesta fase; mix e propriedades sim.");
}

void
MainWindow::on_route_color (int idx, Gdk::RGBA const& color)
{
	if (!_daw->attached ()) { return; }
	auto byte = [] (double v) -> unsigned int {
		if (v < 0.0) { v = 0.0; }
		if (v > 1.0) { v = 1.0; }
		return (unsigned int) std::lround (v * 255.0);
	};
	const unsigned int rgba = (byte (color.get_red ()) << 24) |
	                          (byte (color.get_green ()) << 16) |
	                          (byte (color.get_blue ()) << 8) |
	                           byte (color.get_alpha ());
	std::string err;
	if (!_daw->set_route_color (idx, rgba, err)) { set_status (err, true); return; }
	set_status ("cor da trilha alterada.");
}

void
MainWindow::on_track_remove ()
{
	if (!_daw->attached ()) { set_status ("nenhuma sessão aberta.", true); return; }
	const std::vector<RouteStrip> ss = _daw->strips ();
	int alvo = -1;
	for (size_t i = 0; i < ss.size (); ++i) {
		if (!ss[i].is_master) { alvo = (int) i; }
	}
	if (alvo < 0) { set_status ("nenhuma rota para remover.", true); return; }
	const std::string nm = ss[(size_t) alvo].name;
	std::string err;
	if (!_daw->remove_route (alvo, err)) { set_status (err, true); return; }
	refresh_routes ();
	refresh_mixer ();
	refresh_transport ();
	set_status ("rota removida: " + nm);
}

void
MainWindow::refresh_routes ()
{
	clear (*_routes);
	std::vector<std::string> rs = _engine->route_names ();
	if (_routes_count) {
		_routes_count->set_text (std::to_string ((long) rs.size ()) + " rotas");
	}
	if (rs.empty ()) {
		Gtk::Label* e = label ("(sem rotas — abra uma sessão e crie uma trilha)", "spook-empty");
		e->set_margin_top (6);
		_routes->pack_start (*e, false, false, 0);
	} else {
		const std::vector<RouteStrip> ss = _daw->attached () ? _daw->strips () : std::vector<RouteStrip> ();
		for (size_t i = 0; i < rs.size (); ++i) {
			Gtk::Box* row = box (Gtk::ORIENTATION_HORIZONTAL, 8);
			cls (*row, "spook-row");
			char idx[8];
			std::snprintf (idx, sizeof (idx), "%02zu", i + 1);
			Gtk::Label* n = label (idx, "spook-row-idx");
			n->set_size_request (24, -1);
			row->pack_start (*n, false, false, 0);

			/* Studio One Track List / Logic Inspector: o nome é sempre visível
			 * e editável aqui, não só no campo de criação. */
			Gtk::Entry* nome = entry ();
			cls (*nome, "route-name-entry");
			nome->set_text (rs[i]);
			const bool master = i < ss.size () && ss[i].is_master;
			nome->set_editable (!master);
			nome->set_tooltip_text (master ? "canal master" : "clique, edite e aperte Enter");
			row->pack_start (*nome, true, true, 0);
			if (!master) {
				nome->signal_activate ().connect ([this, i, nome] () { on_route_rename ((int) i, nome); });
				nome->signal_focus_out_event ().connect ([this, i, nome] (GdkEventFocus*) {
					on_route_rename ((int) i, nome); return false;
				});
				Gtk::Button* dup = push ("DUP", nullptr);
				dup->set_tooltip_text ("duplicar trilha e seu estado de mix (Studio One)");
				dup->signal_clicked ().connect ([this, i] () { on_route_duplicate ((int) i); });
				row->pack_start (*dup, false, false, 0);
			}
			row->pack_start (*label (master ? "MASTER" : (i < ss.size () && ss[i].is_midi ? "MIDI" : "AUDIO"), "spook-strip-unit"), false, false, 0);
			_routes->pack_start (*row, false, false, 0);
		}
	}
	_routes->show_all ();
}

/* --------------------------------------------------------------- SCARLETT */

Gtk::Widget*
MainWindow::build_scarlett_tab ()
{
	Gtk::Box* b = box (Gtk::ORIENTATION_VERTICAL, 10, 12);
	b->pack_start (*strong ("Mixer do hardware (ALSA snd_ctl)"), false, false, 0);
	b->pack_start (*dim ("Lê o mixer do cartão pela interface de CONTROLE — não abre PCM, não toca som. "
	                     "Os faders ficam bloqueados (READ-ONLY) até você liberar a ESCRITA."), false, false, 0);

	_card_combo = combo ();
	_card_combo->set_hexpand (true);
	_card_combo->signal_changed ().connect (sigc::mem_fun (*this, &MainWindow::on_scarlett_card_changed));
	_btn_scan = push ("PROCURAR CARTÕES", nullptr);
	_btn_scan->signal_clicked ().connect (sigc::mem_fun (*this, &MainWindow::on_scarlett_scan));

	_write_sw = Gtk::manage (new Gtk::ToggleButton ("ESCRITA"));
	cls (*_write_sw, "spook-btn");
	cls (*_write_sw, "danger");
	_write_sw->set_tooltip_text ("Liberar ESCRITA nos faders (grava ganho real no hardware)");
	_write_sw->signal_toggled ().connect ([this] () {
		_writing = _write_sw->get_active ();
		if (_write_badge) {
			_write_badge->set_text (_writing ? "ESCRITA LIBERADA" : "READ-ONLY");
			if (_writing) { cls (*_write_badge, "danger"); }
			else { _write_badge->get_style_context ()->remove_class ("danger"); }
		}
		refresh_ctl_list ();   /* re-renderiza os faders com a nova sensibilidade */
	});

	_write_badge = label ("READ-ONLY", "spook-badge");

	Gtk::Box* top = box (Gtk::ORIENTATION_HORIZONTAL, 8);
	top->pack_start (*_card_combo, true, true, 0);
	top->pack_start (*_btn_scan, false, false, 0);
	top->pack_start (*_write_badge, false, false, 0);
	top->pack_start (*_write_sw, false, false, 0);
	b->pack_start (*top, false, false, 0);

	_ctl_filter = entry ("filtrar controles (ex.: Mix A Input, Headphone…)");
	_ctl_filter->signal_changed ().connect (sigc::mem_fun (*this, &MainWindow::refresh_ctl_list));
	b->pack_start (*_ctl_filter, false, false, 0);

	/* faixa de canais (faders verticais) */
	Gtk::Box* ct = card ("FAIXA DE CANAIS", 6);
	_strip_box = box (Gtk::ORIENTATION_HORIZONTAL, 6);
	Gtk::ScrolledWindow* ss = Gtk::manage (new Gtk::ScrolledWindow);
	ss->add (*_strip_box);
	ss->set_vexpand (true);
	ss->set_policy (Gtk::POLICY_AUTOMATIC, Gtk::POLICY_AUTOMATIC);
	ss->set_size_request (-1, 210);
	ct->pack_start (*ss, true, true, 0);
	b->pack_start (*ct, false, false, 0);

	/* comutadores (enum/bool) */
	Gtk::Box* cs = card ("COMUTADORES");
	_switch_box = box (Gtk::ORIENTATION_VERTICAL, 2);
	cs->pack_start (*scrolled (*_switch_box), true, true, 0);
	b->pack_start (*cs, true, true, 0);

	_scarlett_state = dim ("");
	b->pack_start (*_scarlett_state, false, false, 0);
	return b;
}

void
MainWindow::on_scarlett_scan ()
{
	_card_combo->remove_all ();
	std::vector<AlsaMixer::Card> cs = AlsaMixer::cards ();
	int first_scarlett = -1;
	for (size_t i = 0; i < cs.size (); ++i) {
		std::string l = std::to_string (cs[i].index) + " — " + cs[i].name
		              + " (" + cs[i].id + ")" + (cs[i].scarlett ? "  ◄ Scarlett" : "");
		_card_combo->append (l);
		if (first_scarlett < 0 && cs[i].scarlett) { first_scarlett = (int) i; }
	}
	if (cs.empty ()) {
		_scarlett_state->set_text ("nenhum cartão de som encontrado.");
		set_status ("nenhum cartão de som.", true);
		return;
	}

	/* Sem Scarlett, o cartão 0 costuma ser um HDMI (só 4 chaves IEC958, zero
	 * faders). Escolhe então o cartão com MAIS controles de mixer — é o que o
	 * usuário quer ver. */
	int escolhido = first_scarlett;
	if (escolhido < 0) {
		int melhor = -1, melhor_n = -1;
		for (size_t i = 0; i < cs.size (); ++i) {
			AlsaMixer tmp;
			std::string e;
			std::vector<CtlElem> els;
			if (tmp.open (cs[i].index, e) && tmp.read_all (els, e)) {
				if ((int) els.size () > melhor_n) { melhor_n = (int) els.size (); melhor = (int) i; }
			}
		}
		escolhido = melhor >= 0 ? melhor : 0;
	}
	_card_combo->set_active (escolhido);
	_scarlett_state->set_text (first_scarlett >= 0
	    ? "Scarlett encontrada." : "Nenhuma Scarlett conectada — mostrando o cartão com mais controles.");
	set_status ("cartões encontrados: " + std::to_string ((long) cs.size ()));
	on_scarlett_card_changed ();
}

void
MainWindow::on_scarlett_card_changed ()
{
	std::string sel = _card_combo->get_active_text ();
	if (sel.empty ()) { return; }

	int idx = std::atoi (sel.c_str ());
	std::string err;
	if (!_mixer) { _mixer.reset (new AlsaMixer); }
	if (!_mixer->open (idx, err)) { set_status (err, true); return; }

	_ctls.clear ();
	if (!_mixer->read_all (_ctls, err)) { set_status (err, true); return; }

	_scarlett_state->set_text ("cartão " + std::to_string (idx) + ": "
	                           + std::to_string ((long) _ctls.size ()) + " controles de mixer.");
	_ctl_filter->set_text ("");
	refresh_ctl_list ();
	set_status ("mixer lido (somente leitura).");
	update_engine_visuals ();
}

/* Redesenha os faders e comutadores aplicando o filtro atual. */
void
MainWindow::refresh_ctl_list ()
{
	if (!_strip_box || !_ctl_filter || !_switch_box) { return; }
	const std::string f = _ctl_filter->get_text ();
	clear (*_strip_box);
	clear (*_switch_box);
	int strips = 0, switches = 0;

	for (size_t i = 0; i < _ctls.size (); ++i) {
		const CtlElem& e = _ctls[i];
		if (!f.empty () && e.name.find (f) == std::string::npos) { continue; }

		if (e.kind == "INTEGER" && e.has_range) {
			/* fader vertical */
			Gtk::Box* strip = box (Gtk::ORIENTATION_VERTICAL, 4);
			cls (*strip, "spook-strip");
			cls (*strip, e.writable ? "arm" : "ro");
			strip->set_size_request (86, -1);

			Gtk::Label* nm = label (e.name, "spook-strip-name");
			nm->set_line_wrap (true);
			nm->set_max_width_chars (12);
			nm->set_lines (2);
			nm->set_ellipsize (Pango::ELLIPSIZE_END);
			nm->set_justify (Gtk::JUSTIFY_CENTER);
			nm->set_yalign (0.0f);

			Gtk::Label* val = label (std::to_string (e.value), "spook-strip-val");
			Gtk::Label* unit = label (e.unit.empty () ? "level" : e.unit, "spook-strip-unit");

			double lo = (double) e.min;
			double hi = (double) e.max;
			if (hi <= lo) { hi = lo + 1.0; }
			Glib::RefPtr<Gtk::Adjustment> adj =
			    Gtk::Adjustment::create ((double) e.value, lo, hi,
			                             e.step > 0 ? (double) e.step : 1.0, 10.0, 0.0);
			Gtk::Scale* sc = Gtk::manage (new Gtk::Scale (adj, Gtk::ORIENTATION_VERTICAL));
			cls (*sc, "trough-cyn");
			sc->set_draw_value (false);
			sc->set_inverted (true);          /* maior valor em cima */
			sc->set_vexpand (true);
			sc->set_size_request (-1, 120);
			sc->set_sensitive (e.writable && _writing);

			const std::string nome = e.name;
			const bool writable = e.writable;
			sc->signal_value_changed ().connect ([this, sc, val, nome, writable] () {
				long v = (long) (sc->get_value () + 0.5);
				val->set_text (std::to_string (v));
				if (!writable || !_writing) { return; }
				std::string err;
				if (!_mixer->set_integer (nome, v, err)) { set_status (err, true); }
			});

			strip->pack_start (*nm, false, false, 0);
			strip->pack_start (*sc, true, true, 0);
			strip->pack_start (*val, false, false, 0);
			strip->pack_start (*unit, false, false, 0);
			_strip_box->pack_start (*strip, false, false, 0);
			++strips;
		} else if (e.kind == "ENUM" || e.kind == "BOOL") {
			Gtk::Box* row = box (Gtk::ORIENTATION_HORIZONTAL, 10);
			cls (*row, "spook-row");
			Gtk::Label* nm = label (e.name, "spook-strong");
			nm->set_ellipsize (Pango::ELLIPSIZE_END);
			row->pack_start (*nm, true, true, 0);
			Gtk::Label* ty = label (e.kind, "spook-badge");
			row->pack_start (*ty, false, false, 0);

			if (e.kind == "ENUM" && !e.items.empty ()) {
				Gtk::ComboBoxText* cb = combo ();
				for (size_t k = 0; k < e.items.size (); ++k) { cb->append (e.items[k]); }
				cb->set_active_text (e.value_text);
				cb->set_sensitive (e.writable && _writing);
				const std::string nome = e.name;
				const bool writable = e.writable;
				cb->signal_changed ().connect ([this, cb, nome, writable] () {
					if (!writable || !_writing) { return; }
					std::string err;
					if (!_mixer->set_enum (nome, cb->get_active_text (), err)) { set_status (err, true); }
				});
				cb->set_size_request (150, -1);
				row->pack_start (*cb, false, false, 0);
			} else {
				row->pack_start (*label (std::to_string (e.value), "spook-strip-val"), false, false, 0);
			}
			_switch_box->pack_start (*row, false, false, 0);
			++switches;
		}
	}

	if (strips == 0) {
		_strip_box->pack_start (*label ("(nenhum fader — clique em PROCURAR CARTÕES)", "spook-empty"),
		                        false, false, 0);
	}
	if (switches == 0) {
		_switch_box->pack_start (*label ("(nada)", "spook-empty"), false, false, 0);
	}
	_strip_box->show_all ();
	_switch_box->show_all ();
}

/* ----------------------------------------------------------------- PLUGINS */

static const char*
plugin_badge_class (std::string const& type)
{
	if (type == "LV2")    { return "lv2"; }
	if (type == "LADSPA") { return "ladspa"; }
	if (type == "VST2" || type == "VST3" || type == "VST") { return "vst"; }
	if (type == "Lua")    { return "lua"; }
	return "other";
}

Gtk::Widget*
MainWindow::build_plugins_tab ()
{
	Gtk::Box* b = box (Gtk::ORIENTATION_VERTICAL, 10, 12);
	b->pack_start (*strong ("Plugins (PluginManager do Ardour)"), false, false, 0);
	b->pack_start (*dim ("\"Só cache\" é instantâneo. A varredura completa leva dezenas de segundos "
	                     "e é o mesmo scan que o Ardour faz ao abrir."), false, false, 0);

	Gtk::Box* btns = box (Gtk::ORIENTATION_HORIZONTAL, 8);
	_btn_pcache = push ("LISTAR (SÓ CACHE)", "cyan");
	_btn_pcache->signal_clicked ().connect (sigc::mem_fun (*this, &MainWindow::on_plugins_cache));
	_btn_pfull = push ("VARREDURA COMPLETA", nullptr);
	_btn_pfull->signal_clicked ().connect (sigc::mem_fun (*this, &MainWindow::on_plugins_full));
	btns->pack_start (*_btn_pcache, false, false, 0);
	btns->pack_start (*_btn_pfull, false, false, 0);
	b->pack_start (*btns, false, false, 0);

	_plug_state = dim ("nada listado.");
	b->pack_start (*_plug_state, false, false, 0);

	Gtk::Box* c = card ("CATÁLOGO");
	_plugins = box (Gtk::ORIENTATION_VERTICAL, 0);
	c->pack_start (*scrolled (*_plugins), true, true, 0);
	b->pack_start (*c, true, true, 0);
	return b;
}

void
MainWindow::on_plugins_cache ()
{
	plugins_from_cache ();
	on_plugins_scanned ();
}

void
MainWindow::on_plugins_full ()
{
	if (_plugins_busy) { set_status ("varredura já em andamento…"); return; }
	if (_scan_thread.joinable ()) { _scan_thread.join (); }

	_plugins_busy = true;
	_btn_pcache->set_sensitive (false);
	_btn_pfull->set_sensitive (false);
	on_plugins_cache ();
	set_status ("varrendo plugins — isso leva dezenas de segundos, pode continuar usando a janela…");
	log_line ("[PLUGINS] varredura completa iniciada em segundo plano");

	_scan_thread = std::thread ([this] () {
		plugins_full_scan ();
		_plugins_done.emit ();
	});
}

void
MainWindow::on_plugins_scanned ()
{
	_plugins_busy = false;
	if (_btn_pcache) { _btn_pcache->set_sensitive (true); }
	if (_btn_pfull)  { _btn_pfull->set_sensitive (true); }

	clear (*_plugins);
	std::vector<PluginEntry> l = plugin_list ();
	for (size_t i = 0; i < l.size (); ++i) {
		Gtk::Box* row = box (Gtk::ORIENTATION_HORIZONTAL, 10);
		cls (*row, "spook-row");
		char idx[8];
		std::snprintf (idx, sizeof (idx), "%03zu", i + 1);
		Gtk::Label* n = label (idx, "spook-row-idx");
		n->set_size_request (28, -1);
		row->pack_start (*n, false, false, 0);
		Gtk::Label* ty = label (l[i].type, "spook-badge");
		cls (*ty, plugin_badge_class (l[i].type));
		row->pack_start (*ty, false, false, 0);
		Gtk::Label* nm = strong (l[i].name);
		nm->set_ellipsize (Pango::ELLIPSIZE_END);
		row->pack_start (*nm, true, true, 0);
		row->pack_start (*label (l[i].io, "spook-strip-unit"), false, false, 0);
		row->pack_start (*label (l[i].creator, "spook-dim"), false, false, 0);
		_plugins->pack_start (*row, false, false, 0);
	}
	if (l.empty ()) {
		_plugins->pack_start (*label ("(nenhum plugin — use LISTAR SÓ CACHE ou a varredura)",
		                              "spook-empty"), false, false, 0);
	}
	_plugins->show_all ();
	_plug_state->set_text (std::to_string ((long) l.size ()) + " plugins.");
	set_status ("plugins listados: " + std::to_string ((long) l.size ()));
}

/* --------------------------------------------------------------------- LOG */

Gtk::Widget*
MainWindow::build_log_tab ()
{
	Gtk::Box* b = box (Gtk::ORIENTATION_VERTICAL, 8, 10);
	b->pack_start (*dim ("Avisos/erros do PBD (o Ardour fala por Receiver/Transmitter) e a nossa "
	                     "trilha de eventos."), false, false, 0);

	_log_buf = Gtk::TextBuffer::create ();
	_log_buf->set_text (
	    "SPECTER — FASE 4 (binário único)\n"
	    "motor: libardour (dinâmico, SEM a GUI do Ardour)   ·   UI: gtkmm-3.0\n"
	    "backend padrão: None (Dummy) — sobe o motor processando silêncio, sem som\n"
	    "tema: escuro de estúdio   ·   abas: ENGINE · SESSÃO · SCARLETT · PLUGINS · LOG\n"
	    "\n"
	    "aguardando comandos…\n");
	_log_view = Gtk::manage (new Gtk::TextView (_log_buf));
	_log_view->set_editable (false);
	_log_view->set_monospace (true);
	cls (*_log_view, "spook-console");
	_log_scroll = scrolled (*_log_view);
	Gtk::Box* c = card ("CONSOLE");
	c->pack_start (*_log_scroll, true, true, 0);
	b->pack_start (*c, true, true, 0);
	return b;
}

void
MainWindow::drain_log ()
{
	std::vector<std::string> lines = log_drain ();
	if (lines.empty ()) { return; }
	for (size_t i = 0; i < lines.size (); ++i) {
		Gtk::TextBuffer::iterator it = _log_buf->end ();
		_log_buf->insert (it, lines[i] + "\n");
	}
	if (_log_scroll) {
		Glib::RefPtr<Gtk::Adjustment> adj = _log_scroll->get_vadjustment ();
		adj->set_value (adj->get_upper () - adj->get_page_size ());
	}
}

/* --------------------------------------------------------------- RODAPÉ */

Gtk::Widget*
MainWindow::build_statusbar ()
{
	Gtk::Box* s = box (Gtk::ORIENTATION_HORIZONTAL, 10);
	cls (*s, "spook-statusbar");
	_status = label ("pronto.", "spook-status");
	s->pack_start (*_status, true, true, 0);
	_status_version = label ("SPECTER v0.1 · FASE 4 · libardour + ALSA", "spook-ver");
	s->pack_start (*_status_version, false, false, 0);
	return s;
}

/* -------------------------------------------------------------------- tick */

bool
MainWindow::on_tick ()
{
	if (_tick_busy) { return true; }
	_tick_busy = true;

	drain_log ();

	if (_engine->engine_running ()) {
		std::string s = "no ar: " + _engine->backend_name ()
		              + "  @ " + std::to_string ((long) _engine->sample_rate ()) + " Hz";
		if (_engine->session_open ()) { s += "   ·   sessão \"" + _engine->session_name () + "\""; }
		_engine_state->set_text (s);
	} else {
		_engine_state->set_text ("parado.");
	}

	/* medidores reais (CPU / memória do Arch) */
	double pct = cpu_load (_cpu_prev_total, _cpu_prev_idle);
	char buf[32];
	std::snprintf (buf, sizeof (buf), "%3.0f%%", pct);
	if (_cpu_bar) { _cpu_bar->set_value (pct); }
	if (_cpu_label) { _cpu_label->set_text (buf); }
	if (_eng_cpu_bar) { _eng_cpu_bar->set_value (pct); }
	if (_eng_cpu_val) { _eng_cpu_val->set_text (buf); }

	std::string mtxt;
	double mp = mem_load (mtxt);
	if (_eng_mem_bar) { _eng_mem_bar->set_value (mp); }
	if (_eng_mem_val) { _eng_mem_val->set_text (mtxt); }

	/* DAW: LCD do transporte e medidores do mixer (o mixer só na aba visível) */
	if (_daw->attached ()) {
		refresh_transport ();
		if (_nb && _nb->get_current_page () == 0) { refresh_mixer (); }
		if (_nb && _nb->get_current_page () == 1) { refresh_arrange (); }
	}

	update_engine_visuals ();

	_tick_busy = false;
	return true;
}

/* ------------------------------------------------ diagnóstico headless (env) */

void
MainWindow::show_tab (int n)
{
	if (!_nb) { return; }
	if (n < 0 || n >= (int) _nb->get_n_pages ()) { return; }
	_nb->set_current_page (n);
}

void
MainWindow::debug_bootstrap_session ()
{
	if (!_sess_dir || !_sess_name) { return; }
	_sess_dir->set_text ("/tmp/opencode/spook-ui-dump");
	_sess_name->set_text ("uidump");
	if (!_engine->engine_running ()) { on_start (); }
	on_session_open ();
	if (_track_name) { _track_name->set_text ("Bateria"); }
	on_track_new ();
	if (_track_name) { _track_name->set_text ("Teclado"); }
	on_track_new_midi ();
	std::string section_err;
	_daw->add_arranger_section ("Intro", 8, section_err);
	_daw->locate ((long long) (_engine->session_sample_rate () * (60.0 / 120.0) * 4.0 * 8.0));
	_daw->add_arranger_section ("Verso", 8, section_err);
	_daw->locate (0);
	const std::vector<RouteStrip> demo_strips = _daw->strips ();
	for (size_t i = 0; i < demo_strips.size (); ++i) {
		if (demo_strips[i].is_midi && !demo_strips[i].is_master) {
			_daw->add_midi_clip ((int) i, "Beat 01", 4, section_err);
			break;
		}
	}
	on_marker_add ();
	refresh_transport ();
	refresh_mixer ();
	refresh_arrange ();
	refresh_markers ();
}

/* Teste funcional dos atalhos: monta GdkEventKey sintéticos e chama o MESMO
 * handler ligado a signal_key_press_event. Exercita a lógica de atalho (espaço,
 * R, L, M, Del, Home/End, Ctrl+Z/Y/S) sem depender de foco do X. Ligado por
 * SPOOK_KEYTEST=1. */
void
MainWindow::debug_key_test ()
{
	struct Caso { guint key; guint state; const char* nome; };
	const Caso casos[] = {
		{ GDK_KEY_space,    0,                "espaço  (play)" },
		{ GDK_KEY_space,    0,                "espaço  (pausar)" },
		{ GDK_KEY_r,        0,                "R  (arm rec)" },
		{ GDK_KEY_r,        0,                "R  (desarmar)" },
		{ GDK_KEY_l,        0,                "L  (loop)" },
		{ GDK_KEY_l,        0,                "L  (loop off)" },
		{ GDK_KEY_m,        0,                "M  (marcador)" },
		{ GDK_KEY_Delete,   0,                "Del (remove marcador)" },
		{ GDK_KEY_End,      0,                "End (fim)" },
		{ GDK_KEY_Home,     0,                "Home (início)" },
		{ GDK_KEY_z,        GDK_CONTROL_MASK, "Ctrl+Z (desfazer)" },
		{ GDK_KEY_z,        GDK_CONTROL_MASK, "Ctrl+Z (desfazer 2)" },
		{ GDK_KEY_y,        GDK_CONTROL_MASK, "Ctrl+Y (refazer)" },
		{ GDK_KEY_s,        GDK_CONTROL_MASK, "Ctrl+S (salvar)" },
	};

	for (size_t i = 0; i < sizeof (casos) / sizeof (casos[0]); ++i) {
		GdkEventKey ev;
		std::memset (&ev, 0, sizeof (ev));
		ev.type   = GDK_KEY_PRESS;
		ev.keyval = casos[i].key;
		ev.state  = casos[i].state;

		const bool pego = on_key_press (&ev);

		std::string l = std::string ("[TECLA] ") + casos[i].nome
		              + " -> " + (pego ? "tratada" : "IGNORADA")
		              + " | rolling=" + (_daw->rolling () ? "sim" : "nao")
		              + " arm="     + (_daw->record_enabled () ? "sim" : "nao")
		              + " loop="    + (_daw->loop_active () ? "sim" : "nao")
		              + " locais="  + std::to_string ((long) _daw->markers ().size ())
		              + " amostras=" + _daw->pos_samples ();
		log_line (l);
	}

	/* com o foco num Entry, o atalho NÃO deve roubar a tecla */
	if (_track_name) {
		_track_name->grab_focus ();
		GdkEventKey ev;
		std::memset (&ev, 0, sizeof (ev));
		ev.type = GDK_KEY_PRESS;
		ev.keyval = GDK_KEY_space;
		const bool pego = on_key_press (&ev);
		log_line (std::string ("[TECLA] espaço com foco num Entry -> ")
		          + (pego ? "TRATADA (ERRADO: roubou a tecla)" : "ignorada (correto)"));
	}
}

} // namespace spook
