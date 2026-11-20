"""Roteiro de teste de interface do BSL Stage com input real (mouse/teclado) - ver README.md.

Uso:  py -3 tools/uiqa/run.py --config 1.0|1.5|window [--only nome,...] [--shots docs/qa]
"""
import argparse
import os
import sys
import time
import traceback

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import driver
from driver import QaFailure

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
EXE = os.path.join(ROOT, "build", "Release", "BSLStage.exe")
MEDIA = os.path.join(ROOT, "testmedia")

MODE_COMP = "Composição###modo_composicao"
MODE_MAP = "Mapeamento###modo_mapeamento"
MODE_PIX = "Pixel Map###modo_pixelmap"
MODE_SEQ = "Sequências###modo_sequencias"


class Tester:
    def __init__(self, app, tag, shots):
        self.app = app
        self.tag = tag
        self.shots = shots
        self.results = []
        self.layout_notes = []
        self.closed = False
        self._asserts0 = 0
        self._conf0 = 0

    def shot(self, name):
        path = os.path.join(self.shots, "%s_%s.png" % (self.tag, name))
        self.app.screenshot(path)
        return path

    def step(self, name, fn):
        t0 = time.time()
        retries = 0
        while True:
            lost0 = self.app.focus_losses
            try:
                if self.app.alive():
                    self.app.ensure_focus()
                    self.app.wait_frames(3)
                    close_pickers(self.app)
                lost0 = self.app.focus_losses
                if name in ("grade",):
                    self.fresh_composition()
                self.grid_to_bottom()
                fn()
                self.layout_check(name)
                d = self.app.read()
                if d["asserts"] > self._asserts0:
                    raise QaFailure("assert do ImGui durante o passo (%d novos)" % (d["asserts"] - self._asserts0))
                if d["conflicts"] > self._conf0:
                    raise QaFailure("conflito de ID durante o passo (%d novos)" % (d["conflicts"] - self._conf0))
                self.results.append((name, True, "%.1fs" % (time.time() - t0)))
                print("  ok    %s (%.1fs)" % (name, time.time() - t0))
                break
            except Exception as e:
                if self.app.focus_losses > lost0 and retries < 2 and self.app.alive():
                    retries += 1
                    print("  repetindo %s: o foco saiu do app durante o passo (%s)" % (name, str(e).splitlines()[0] if str(e) else type(e).__name__))
                    self.recover()
                    continue
                detail = "%s: %s" % (type(e).__name__, e)
                if not isinstance(e, QaFailure):
                    detail += "\n" + traceback.format_exc()
                self.results.append((name, False, detail))
                print("  FALHA %s -> %s" % (name, detail.splitlines()[0]))
                try:
                    if self.app.alive():
                        self.shot("falha_" + name.replace(" ", "_").replace("/", "-"))
                        self.app.reset_input()
                        close_pickers(self.app)
                except Exception:
                    pass
                break
        try:
            if self.app.alive():
                d = self.app.read()
                self._asserts0 = d["asserts"]
                self._conf0 = d["conflicts"]
        except Exception:
            pass

    def recover(self):
        app = self.app
        try:
            app.reset_input()
            close_pickers(app)
            app.wait_frames(4)
            ensure_mode(app, "composition")
            app.wait_frames(4)
        except Exception:
            pass

    def fresh_composition(self):
        """Um cenario anterior pode ter criado camadas (Mapeamento: "Criar camada para este slice"; Pixel Map; Transform) e a
        grade passa a rolar, escondendo o cabecalho das colunas ou o botao de nova camada. Volta a composicao ao inicio
        (Ctrl+N, "Não salvar") antes do proximo cenario."""
        app = self.app
        try:
            state = app.read()["state"]
            if state.get("mode") != "composition" or len(state["layers"]) <= 3:
                return
            app.key("n", "ctrl")
            app.wait_frames(4)
            if app.has("Não salvar"):
                app.click("Não salvar", "Salvar")
            app.wait_state(lambda s: len(s["layers"]) <= 3, "composicao nova", timeout=5)
        except Exception:
            pass

    def grid_to_bottom(self):
        """Rola a grade de clips ate o fim: as camadas 1..3 (embaixo) ficam visiveis mesmo depois de um cenario ter criado
        camadas (o Pixel Map cria uma por painel e a grade passa a rolar)."""
        try:
            if self.app.read()["state"].get("mode") == "composition":
                win = self.app.window_rect("cmp_grade")
                if win and not self.app.find_all("celula L1 C1"):
                    self.app.wheel(win.cx, win.cy, -12)
        except Exception:
            pass

    def layout_check(self, where):
        """Itens cortados pela borda da janela que os contem e itens que se sobrepoem (sem rolagem)."""
        app = self.app
        app.read()
        wins = {it.window: it.rect for it in app.items if it.is_window}
        no_scroll = ("##statusbar", "##MainMenuBar", "cmp_grade", "cmp_previa", "cmp_saida", "cmp_clip", "Entrada",
                     "Lista", "Propriedades", "cmp_aba_saida", "pm_config")
        issues = []
        for it in app.items:
            if it.is_window or not it.visible or not it.label or it.window not in wins:
                continue
            if "/" in it.window and "##celulas" in it.window:
                continue
            w = wins[it.window]
            if any(k in it.window for k in no_scroll) and "/" not in it.window:
                tol = 2.5
                if it.rect.x1 > w.x1 + tol or it.rect.x0 < w.x0 - tol:
                    issues.append("cortado na horizontal: %r em %s (%s, janela %s)" % (it.label, it.window.split("#")[0], it.rect, w))
        by_win = {}
        for it in app.items:
            if it.is_window or not it.visible or not it.label or it.label.startswith(("tab:", "celula", "entrada:", "saida:")):
                continue
            if it.window.startswith("dock-tabs") or "DockSpace" in it.window:
                continue
            by_win.setdefault(it.window, []).append(it)
        for wname, items in by_win.items():
            for i in range(len(items)):
                for j in range(i + 1, len(items)):
                    a, b = items[i].rect, items[j].rect
                    ix = min(a.x1, b.x1) - max(a.x0, b.x0)
                    iy = min(a.y1, b.y1) - max(a.y0, b.y0)
                    if ix > 3 and iy > 3:
                        area = ix * iy
                        small = min(a.w * a.h, b.w * b.h)
                        if small <= 0 or area / small > 0.8:
                            continue
                        if area / small > 0.15:
                            issues.append("sobreposto: %r x %r em %s" % (items[i].label, items[j].label, wname.split("#")[0]))
        for s in dict.fromkeys(issues):
            self.layout_notes.append((where, s))
            print("      layout[%s]: %s" % (where, s))

def wait_items_stable(app, label, window=None, quiet=0.5, timeout=10.0):
    last = None
    t_change = time.time()
    t0 = time.time()
    while time.time() - t0 < timeout:
        app.wait_frames(3)
        sig = [(round(i.rect.x0), round(i.rect.y0), round(i.rect.x1), round(i.rect.y1)) for i in app.find_all(label, window)]
        if sig and sig == last:
            if time.time() - t_change >= quiet:
                return True
        else:
            last = sig
            t_change = time.time()
    return False


def close_pickers(app):
    for _ in range(3):
        if app.has("Atualizar lista") and app.has("Cancelar"):
            try:
                app.click("Cancelar")
                app.wait_frames(4)
            except QaFailure:
                break
        else:
            return


def menu(app, top, *path, nth=0):
    app.click(top, "MainMenuBar", nth=nth)
    app.wait_frames(3)
    for sub in path[:-1]:
        it = app.find(sub, "Menu_")
        app.hover(it.rect.cx, it.rect.cy)
        app.wait_frames(5)
    app.click(path[-1], "Menu_")
    app.wait_frames(3)


def open_menu_hover_all(app, top, nth=0, hover=True):
    """Abre o menu e passa o mouse por todos os itens (sem clicar)."""
    app.click(top, "MainMenuBar", nth=nth)
    app.wait_frames(3)
    items = [i for i in app.items if "Menu_" in i.window and not i.is_window and i.label and i.visible]
    for it in (items if hover else items[:1]):
        app.hover(it.rect.cx, it.rect.cy)
        app.wait_frames(2)
    app.key("esc")
    app.key("esc")
    app.wait_frames(2)


def ensure_mode(app, mode):
    if app.state["mode"] == mode:
        return
    for attempt in range(3):
        app.click({"mapping": MODE_MAP, "pixelmap": MODE_PIX, "sequences": MODE_SEQ}.get(mode, MODE_COMP), "MainMenuBar")
        try:
            app.wait_state(lambda s: s["mode"] == mode, "modo " + mode, timeout=2.0)
            break
        except QaFailure:
            if attempt == 2:
                raise
            app.key("esc")
    app.wait_frames(4)


def pick_tab(app, name):
    app.click("tab:" + name, "dock-tabs")
    app.wait_frames(3)


def clear_field_and_type(app, text):
    app.key("a", "ctrl")
    app.type_text(text)
    app.key("enter")


def cell(app, layer, col):
    """Item da celula (layer e col 1-based como na tela)."""
    return app.find("celula L%d C%d" % (layer, col))


def scroll_to_visible(app, label, over):
    """Rola a grade na horizontal (Shift + roda) ate o item aparecer."""
    for _ in range(12):
        if app.find_all(label):
            return
        app.wheel(over.rect.cx, over.rect.cy, -3, shift=True)
    raise QaFailure("nao consegui rolar ate %r" % label)


def expect(cond, what):
    if not cond:
        raise QaFailure("esperado: " + what)

def sc_modos(t):
    app = t.app
    for i in range(5):
        for mode in ("mapping", "pixelmap", "sequences", "composition"):
            ensure_mode(app, mode)
            tabs = [x.label for x in app.items if x.window == "dock-tabs" and x.visible and x.label.startswith("tab:")]
            for label in tabs:
                app.click(label, "dock-tabs", nth=0 if tabs.count(label) == 1 else 0)
            for top in ("Arquivo", "Composição", "Saída", "Exibir", "Ajuda"):
                open_menu_hover_all(app, top, hover=(i == 0))
            if i == 0:
                t.shot("modo_" + mode)
        expect(app.state["mode"] == "composition", "volta ao modo Composição na rodada %d" % (i + 1))
        expect(app.has("celula L1 C1"), "grade visível após voltar ao modo Composição")


def _slice(app, i, scr=None):
    s = app.read()["state"]
    return s["screens"][s["mapping"]["screen"] if scr is None else scr]["slices"][i]


def _bbox(c):
    xs, ys = c[0::2], c[1::2]
    return min(xs), min(ys), max(xs), max(ys)


def _set_snap(app, want):
    if app.read()["state"]["mapping"]["snap"] != want:
        app.click("##map_ima", "Saída")
        app.wait_state(lambda s: s["mapping"]["snap"] == want, "ímã %s" % ("ligado" if want else "desligado"))


def _type_field(app, label, value):
    app.click(label, "Propriedades")
    app.key("a", "ctrl")
    app.type_text(str(value))
    app.key("enter")
    app.wait_frames(3)


def _map_ops(app):
    return app.read()["state"]["mapping"]["undo"]


def map_editor_checks(t):
    """Editor de mapeamento com mouse e teclado reais: Transformar (mover, redimensionar, girar), ímã e linhas-guia,
    Encostar, alinhar, modo Pontos, seleção múltipla, zoom/pan, teclado e desfazer/refazer, nas duas vistas."""
    import math
    app = t.app
    n_screens = len(app.state["screens"])
    app.click("##map_add_tela", "Lista")
    app.wait_state(lambda s: len(s["screens"]) == n_screens + 1, "tela nova para o editor")
    app.click("##map_add_slice", "Lista")
    s = app.wait_state(lambda s: len(s["screens"][s["mapping"]["screen"]]["slices"]) == 2, "slice 2 da tela nova")
    scr = s["mapping"]["screen"]
    _set_snap(app, False)

    def P(x, y):
        r = app.find("saida:imagem").rect
        return r.x0 + x * r.w / 1920.0, r.y0 + y * r.w / 1920.0

    def corners(i):
        return _slice(app, i, scr)["corners"]

    for lbl, v in (("##ox", 900), ("##oy", 100), ("##ow", 400), ("##oh", 300)):
        _type_field(app, lbl, v)
    c = corners(1)
    expect(c[:2] == [900, 100] and c[4:6] == [1300, 400], "campos X/Y/L/A da saída montam o retângulo (%s)" % c)
    app.click_at(*P(200, 700))
    app.wait_state(lambda s: s["mapping"]["slice"] == 0 and s["mapping"]["multi"] == [0], "clique na vista seleciona o slice A")
    for lbl, v in (("##ox", 100), ("##oy", 100), ("##ow", 400), ("##oh", 300)):
        _type_field(app, lbl, v)
    app.click_at(*P(1100, 250))
    app.wait_state(lambda s: s["mapping"]["multi"] == [1], "clique seleciona o slice B")

    u0 = _map_ops(app)
    app.drag(*P(1100, 250), *P(1160, 290))
    c = corners(1)
    expect(abs(c[0] - 960) <= 8 and abs(c[1] - 140) <= 8, "arrastar o miolo move o slice (%s)" % c[:2])
    expect(abs((c[2] - c[0]) - 400) < 0.01 and abs((c[5] - c[1]) - 300) < 0.01, "mover não muda o tamanho")
    expect(_map_ops(app) == u0 + 1, "o arraste inteiro é 1 operação de desfazer (%d -> %d)" % (u0, _map_ops(app)))

    c = corners(1)
    ym = (c[1] + c[5]) / 2
    u0 = _map_ops(app)
    app.drag(*P(c[2], ym), *P(c[2] + 80, ym))
    c2 = corners(1)
    expect(abs((c2[2] - c2[0]) - 480) <= 8 and abs(c2[0] - c[0]) < 0.01 and abs((c2[5] - c2[1]) - 300) < 0.01,
           "alça da borda direita muda só a largura (%s -> %s)" % (c, c2))
    expect(_map_ops(app) == u0 + 1, "redimensionar = 1 operação")
    c = corners(1)
    ratio0 = (c[2] - c[0]) / (c[5] - c[1])
    app.drag(*P(c[4], c[5]), *P(c[4] + 96, c[5] + 5), mods=("shift",))
    c3 = corners(1)
    w3, h3 = c3[2] - c3[0], c3[5] - c3[1]
    expect(abs(w3 / h3 - ratio0) < 0.02 and w3 > 520, "Shift mantém a proporção (%.1f x %.1f)" % (w3, h3))
    app.key("z", "ctrl")
    c = corners(1)
    app.drag(*P(c[2], (c[1] + c[5]) / 2), *P(c[2] + 40, (c[1] + c[5]) / 2), mods=("alt",))
    c4 = corners(1)
    expect(abs((c4[2] - c4[0]) - ((c[2] - c[0]) + 80)) <= 10 and abs((c4[0] + c4[2]) / 2 - (c[0] + c[2]) / 2) <= 6,
           "Alt redimensiona a partir do centro (%s -> %s)" % (c, c4))
    app.key("z", "ctrl")
    c = corners(1)
    expect(abs((c[2] - c[0]) - 480) <= 8, "Ctrl+Z desfez o Alt e o Shift (largura %.1f)" % (c[2] - c[0]))

    c0 = corners(1)
    wb, hb = c0[2] - c0[0], c0[5] - c0[1]
    bx0, by0, bx1, by1 = _bbox(corners(1))
    ctr = P((bx0 + bx1) / 2, (by0 + by1) / 2)
    top = P((bx0 + bx1) / 2, by0)
    u0 = _map_ops(app)
    app.drag(top[0], top[1] - 26 * app.scale, ctr[0] + 150, ctr[1], mods=("shift",))
    c = corners(1)
    ang = math.degrees(math.atan2(c[3] - c[1], c[2] - c[0]))
    expect(abs(ang - 90) < 0.5, "girar com Shift dá 90 graus exatos (%.2f)" % ang)
    expect(abs(math.hypot(c[2] - c[0], c[3] - c[1]) - wb) < 1 and abs(math.hypot(c[6] - c[0], c[7] - c[1]) - hb) < 1,
           "girar não muda o tamanho")
    expect(abs(sum(c[0::2]) / 4 - (bx0 + bx1) / 2) < 1 and abs(sum(c[1::2]) / 4 - (by0 + by1) / 2) < 1, "gira em volta do centro")
    expect(_map_ops(app) == u0 + 1, "girar = 1 operação")
    t.shot("mapeamento_girado")
    app.key("z", "ctrl")
    c = corners(1)
    expect(abs(c[1] - c[3]) < 0.01 and abs((c[2] - c[0]) - wb) < 1, "Ctrl+Z desfaz a rotação (%s)" % c)
    expect(app.state["mapping"]["redo"] >= 1, "o refazer ficou disponível")
    app.key("y", "ctrl")
    c = corners(1)
    expect(abs(math.degrees(math.atan2(c[3] - c[1], c[2] - c[0])) - 90) < 0.5, "Ctrl+Y refaz a rotação")
    app.key("z", "ctrl")

    c = corners(1)
    a = corners(0)
    app.click("##map_encostar_esq", "Saída")
    c = corners(1)
    expect(c[0] == a[2] and c[6] == a[2], "Encostar: a borda esquerda de B ficou igual à direita de A (%s vs %s)" % (c[0], a[2]))
    app.key("z", "ctrl")
    expect(corners(1)[0] > a[2] + 100, "Ctrl+Z desfaz o Encostar")

    _set_snap(app, True)
    c = corners(1)
    a = corners(0)
    gap = c[0] - a[2]
    dx = -(gap - 8)

    def probe():
        m = app.read()["state"]["mapping"]
        t.shot("mapeamento_ima")
        return m["guides"], m["dragging"]

    cx, cy = (c[0] + c[2]) / 2, (c[1] + c[5]) / 2 + 40
    guides, dragging = app.drag(*P(cx, cy), *P(cx + dx, cy), probe=probe)
    c = corners(1)
    expect(dragging and guides >= 1, "linhas-guia aparecem durante o encaixe (guias=%s)" % guides)
    expect(c[0] == a[2] and c[6] == a[2], "ímã: a borda de B encostou na de A, igual ao pixel (%s vs %s)" % (c[0], a[2]))
    expect(app.state["mapping"]["guides"] == 0, "as guias somem ao soltar")
    app.key("z", "ctrl")
    c = corners(1)
    cx, cy = (c[0] + c[2]) / 2, (c[1] + c[5]) / 2 + 40
    app.drag(*P(cx, cy), *P(cx + dx, cy), mods=("alt",))
    c = corners(1)
    expect(c[0] != a[2] and abs(c[0] - (a[2] + 8)) <= 10, "Alt desliga o ímã (borda em %.1f, A em %.1f)" % (c[0], a[2]))
    app.key("z", "ctrl")

    app.drag(*P(30, 30), *P(1700, 560))
    app.wait_state(lambda s: s["mapping"]["multi"] == [0, 1], "retângulo envolvendo os dois seleciona os dois")
    app.drag(*P(30, 30), *P(300, 200))
    app.wait_state(lambda s: s["mapping"]["multi"] == [0], "retângulo parcial seleciona só o A")
    app.drag(*P(1100, 300), *P(1100, 300), mods=("ctrl",))
    app.wait_state(lambda s: s["mapping"]["multi"] == [0, 1], "Ctrl+clique soma à seleção")
    app.click("##map_alinhar_topo", "Saída")
    expect(abs(corners(0)[1] - corners(1)[1]) < 0.01 or abs(min(corners(1)[1::2]) - corners(0)[1]) < 0.01,
           "Alinhar ao topo com 2 slices")
    a0, b0 = corners(0), corners(1)
    app.drag(*P(1100, 250), *P(1140, 280))
    a1, b1 = corners(0), corners(1)
    expect(abs((a1[0] - a0[0]) - (b1[0] - b0[0])) < 0.01 and a1[0] != a0[0], "mover move a seleção múltipla junta")
    app.key("z", "ctrl")
    app.key("z", "ctrl")

    app.click("Pontos (warp)", "Saída")
    app.wait_state(lambda s: s["mapping"]["pointsMode"], "modo Pontos")
    expect(app.has("##cx", "Propriedades") and not app.has("##ox", "Propriedades"), "Propriedades mostram os 4 cantos no modo Pontos")
    app.click_at(*P(1100, 280))
    app.wait_state(lambda s: s["mapping"]["multi"] == [1], "B selecionado")
    a = corners(0)
    c = corners(1)
    tx, ty = a[4] + 6, a[5] - 5
    app.drag(*P(c[6], c[7]), *P(tx, ty))
    c = corners(1)
    expect(c[6] == a[4] and c[7] == a[5], "Pontos: o canto BL de B encaixou no canto BR de A (%s,%s vs %s,%s)" % (c[6], c[7], a[4], a[5]))
    expect(app.state["mapping"]["corner"] == 3, "o canto arrastado ficou selecionado")
    t.shot("mapeamento_pontos")
    app.click("Transformar", "Saída")
    app.wait_state(lambda s: not s["mapping"]["pointsMode"], "volta ao modo Transformar")
    app.wait_frames(3)
    app.key("z", "ctrl")

    z0 = app.state["mapping"]["outputZoom"]
    u_zoom = _map_ops(app)
    r = app.find("saida:imagem").rect
    app.wheel(r.cx, r.cy, 3)
    z1 = app.state["mapping"]["outputZoom"]
    expect(z1 > z0 * 1.3, "a roda dá zoom (%.3f -> %.3f)" % (z0, z1))
    r1 = app.find("saida:imagem").rect
    app.drag(r1.cx, r1.cy, r1.cx + 60, r1.cy + 30, mods=("space",))
    r2 = app.find("saida:imagem").rect
    expect(abs((r2.x0 - r1.x0) - 60) <= 3 and abs((r2.y0 - r1.y0) - 30) <= 3, "espaço+arrastar faz pan (%s -> %s)" % (r1, r2))
    app.drag(r2.cx, r2.cy, r2.cx - 40, r2.cy - 20, button="middle")
    r3 = app.find("saida:imagem").rect
    expect(abs((r3.x0 - r2.x0) + 40) <= 3 and abs((r3.y0 - r2.y0) + 20) <= 3, "botão do meio faz pan")
    expect(_map_ops(app) == u_zoom, "zoom e pan não entram no histórico")
    app.click("##map_100", "Saída")
    expect(abs(app.state["mapping"]["outputZoom"] - 1.0) < 1e-3, "botão 100%")
    app.click("##map_ajustar", "Saída")
    expect(abs(app.state["mapping"]["outputZoom"] - z0) < 0.01, "Ajustar à vista volta ao zoom inicial")

    n = len(app.state["screens"][scr]["slices"])
    app.click_at(*P(300, 250))
    app.key("a", "ctrl")
    app.wait_state(lambda s: s["mapping"]["multi"] == [0, 1], "Ctrl+A seleciona todos")
    app.key("delete")
    app.wait_frames(3)
    expect(app.has("##map_remover_ok"), "Delete com mais de 1 slice pede confirmação")
    app.click("Cancelar", "Remover slices")
    expect(len(app.read()["state"]["screens"][scr]["slices"]) == n, "Cancelar não remove")
    app.key("esc")
    app.wait_state(lambda s: s["mapping"]["multi"] == [], "Esc limpa a seleção")
    app.click_at(*P(1100, 250))
    app.key("d", "ctrl")
    app.wait_state(lambda s: len(s["screens"][scr]["slices"]) == n + 1, "Ctrl+D duplica")
    app.key("delete")
    app.wait_state(lambda s: len(s["screens"][scr]["slices"]) == n, "Delete com 1 slice remove sem perguntar")
    app.key("a", "ctrl")
    app.key("delete")
    app.wait_frames(3)
    app.click("##map_remover_ok")
    app.wait_state(lambda s: len(s["screens"][scr]["slices"]) == 0, "confirmar remove todos")
    app.key("z", "ctrl")
    app.wait_state(lambda s: len(s["screens"][scr]["slices"]) == n, "Ctrl+Z traz os slices de volta")
    app.click_at(*P(1100, 250))
    c0 = corners(1)
    app.key("right")
    app.key("down", "shift")
    c = corners(1)
    expect(abs(c[0] - (c0[0] + 1)) < 0.01 and abs(c[1] - (c0[1] + 10)) < 0.01, "setas movem o slice (1 px / Shift 10 px)")

    app.read()
    it1 = app.find_all("Slice 1", "Lista")[-1]
    app.click_at(it1.rect.cx, it1.rect.cy)
    app.wait_state(lambda s: s["mapping"]["multi"] == [0], "Lista: clique seleciona um slice")
    app.read()
    it2 = app.find_all("Slice 2", "Lista")[-1]
    app.drag(it2.rect.cx, it2.rect.cy, it2.rect.cx, it2.rect.cy, mods=("ctrl",))
    app.wait_state(lambda s: s["mapping"]["multi"] == [0, 1], "Lista: Ctrl+clique faz seleção múltipla")

    def Pi(x, y):
        r = app.find("entrada:imagem").rect
        cw = app.state["canvas"][0]
        return r.x0 + x * r.w / cw, r.y0 + y * r.w / cw

    def inp(i):
        return _slice(app, i, scr)["input"]

    for lbl, v in (("##w", 400), ("##h", 300), ("##x", 700), ("##y", 100)):
        _type_field(app, lbl, v)
    app.click_at(*Pi(200, 800))
    app.wait_state(lambda s: s["mapping"]["multi"] == [0], "clique na Entrada seleciona o A")
    for lbl, v in (("##w", 400), ("##h", 300), ("##x", 100), ("##y", 100)):
        _type_field(app, lbl, v)
    expect(inp(0) == [100, 100, 400, 300] and inp(1) == [700, 100, 400, 300], "entradas montadas pelas propriedades (%s, %s)" % (inp(0), inp(1)))
    app.click_at(*Pi(900, 250))
    app.wait_state(lambda s: s["mapping"]["multi"] == [1], "B selecionado na Entrada")
    a = inp(0)
    ib = inp(1)
    u0 = _map_ops(app)
    app.drag(*Pi(ib[0] + 200, ib[1] + 150), *Pi(ib[0] + 200 - (ib[0] - (a[0] + a[2]) - 8), ib[1] + 150))
    ib = inp(1)
    expect(ib[0] == a[0] + a[2] and ib[1] == 100, "Entrada: o ímã encostou o retângulo B no A (%s vs %s)" % (ib, a))
    expect(_map_ops(app) == u0 + 1, "arrasto na Entrada = 1 operação")
    app.key("z", "ctrl")
    expect(inp(1)[0] == 700, "Ctrl+Z na Entrada")
    app.drag(*Pi(30, 30), *Pi(1700, 560))
    app.wait_state(lambda s: s["mapping"]["multi"] == [0, 1], "retângulo de seleção na Entrada")
    app.key("esc")
    app.click("##map_ima", "Entrada")
    app.wait_state(lambda s: not s["mapping"]["snap"], "ímã desligado pela vista Entrada")
    _set_snap(app, True)
    t.shot("mapeamento_editor")


def sc_mapeamento(t):
    app = t.app
    ensure_mode(app, "mapping")
    n_screens = len(app.state["screens"])
    app.click("##map_add_tela", "Lista")
    app.wait_state(lambda s: len(s["screens"]) == n_screens + 1, "+ Tela")
    app.click("##map_add_slice", "Lista")
    app.wait_state(lambda s: len(s["screens"][s["mapping"]["screen"]]["slices"]) >= 2, "+ Slice")
    m = app.state["mapping"]
    n_sl = len(app.state["screens"][m["screen"]]["slices"])
    app.click("##map_duplicar", "Lista")
    app.wait_state(lambda s: len(s["screens"][s["mapping"]["screen"]]["slices"]) == n_sl + 1, "Duplicar")
    app.click("##map_remover", "Lista")
    app.wait_state(lambda s: len(s["screens"][s["mapping"]["screen"]]["slices"]) == n_sl, "Remover")

    def cur():
        s = app.read()["state"]
        mp = s["mapping"]
        scr = s["screens"][mp["screen"]]
        return s, scr, scr["slices"][mp["slice"]]

    expect(app.read()["state"]["mapping"]["snap"], "o ímã nasce ligado")
    _set_snap(app, False)

    s, scr, sl = cur()
    img = app.find("entrada:imagem").rect
    k = img.w / s["canvas"][0]
    x, y, w, h = sl["input"]
    app.drag(img.x0 + (x + w) * k, img.y0 + (y + h) * k, img.x0 + (x + w - 300) * k, img.y0 + (y + h - 200) * k)
    s, scr, sl = cur()
    expect(sl["input"][2] < w and sl["input"][3] < h, "alca de entrada redimensionou o retangulo (%s -> %s)" % ([x, y, w, h], sl["input"]))
    x, y, w, h = sl["input"]
    app.drag(img.x0 + (x + w / 2) * k, img.y0 + (y + h / 2) * k, img.x0 + (x + w / 2 + 120) * k, img.y0 + (y + h / 2 + 90) * k)
    s, scr, sl = cur()
    expect(sl["input"][0] != x or sl["input"][1] != y, "arrastar moveu o retangulo de entrada")
    t.shot("mapeamento_entrada")
    x0 = sl["input"][0]
    app.hover(img.cx, img.cy)
    app.key("right")
    s, scr, sl = cur()
    expect(sl["input"][0] == x0 + 1, "seta direita move 1 px (x %d -> %d)" % (x0, sl["input"][0]))
    app.key("right", "shift")
    s, scr, sl = cur()
    expect(sl["input"][0] == x0 + 11, "Shift+seta move 10 px (x -> %d)" % sl["input"][0])
    app.key("down")
    s2, scr, sl2 = cur()
    expect(sl2["input"][1] == sl["input"][1] + 1, "seta abaixo move 1 px")

    out = app.find("saida:imagem").rect
    s, scr, sl = cur()
    ko = out.w / scr["w"]
    tol = max(4.0, 2.0 / ko)
    c = sl["corners"]
    app.drag(out.x0 + c[0] * ko, out.y0 + c[1] * ko, out.x0 + (c[0] + 90) * ko, out.y0 + (c[1] + 60) * ko)
    s, scr, sl = cur()
    expect(abs(sl["corners"][0] - (c[0] + 90)) < tol and abs(sl["corners"][1] - (c[1] + 60)) < tol,
           "arrastar canto TL da saída (%s -> %s)" % (c[:2], sl["corners"][:2]))
    expect(s["mapping"]["corner"] == 0, "canto 0 ficou selecionado")
    cx0, cy0 = sl["corners"][0], sl["corners"][1]
    app.hover(out.cx, out.cy)
    app.key("down", "shift")
    s, scr, sl = cur()
    expect(abs(sl["corners"][1] - (cy0 + 10)) < 0.01, "Shift+seta abaixo move o canto selecionado 10 px")
    app.key("left")
    s, scr, sl = cur()
    expect(abs(sl["corners"][0] - (cx0 - 1)) < 0.01, "seta esquerda move o canto 1 px")
    c = sl["corners"]
    mx = sum(c[0::2]) / 4
    my = sum(c[1::2]) / 4
    dm = max(40.0, 30.0 / ko)
    app.drag(out.x0 + mx * ko, out.y0 + my * ko, out.x0 + (mx + dm) * ko, out.y0 + (my + dm * 0.75) * ko)
    s, scr, sl = cur()
    expect(abs(sl["corners"][4] - (c[4] + dm)) < tol, "arrastar o quad inteiro moveu os cantos")
    t.shot("mapeamento_saida")

    map_editor_checks(t)

    s = app.read()["state"]
    app.click(s["screens"][0]["name"], "Lista")
    app.click(s["screens"][0]["slices"][0]["name"], "Lista", double=True)
    app.wait_frames(3)
    app.key("a", "ctrl")
    app.type_text("Renomeado")
    app.key("enter")
    s = app.wait_state(lambda s: any(sl["name"] == "Renomeado" for sl in s["screens"][0]["slices"]), "renomear slice")
    app.click(s["screens"][0]["name"], "Lista", double=True)
    app.wait_frames(3)
    app.key("a", "ctrl")
    app.type_text("Tela Teste")
    app.key("enter")
    app.wait_state(lambda s: s["screens"][0]["name"] == "Tela Teste", "renomear tela")
    app.click("Renomeado", "Lista")
    app.wait_frames(3)
    app.click("##w", "Propriedades")
    app.key("a", "ctrl")
    app.type_text("800")
    app.key("enter")
    app.click("##x", "Propriedades")
    app.key("a", "ctrl")
    app.type_text("123")
    app.key("enter")
    app.click("##w", "Propriedades")
    app.key("esc")
    s, scr, sl = cur()
    expect(sl["input"][0] == 123 and sl["input"][2] == 800, "campos X e L da entrada aceitaram os valores digitados (%s)" % sl["input"])
    app.click("##w", "Propriedades")
    app.key("a", "ctrl")
    app.type_text("9999")
    app.key("enter")
    app.click("##x", "Propriedades")
    app.key("esc")
    s, scr, sl = cur()
    expect(sl["input"][0] + sl["input"][2] <= s["canvas"][0], "L digitado alem do canvas e limitado (%s)" % sl["input"])
    for lbl in ("Ajustar saída ao retângulo de entrada", "Resetar warp", "Espelhar H", "Espelhar H", "Ativa", "Ativa"):
        scroll_into_view(app, lbl, "Propriedades")
        app.click(lbl, "Propriedades")
    t.shot("mapeamento_propriedades")
    layer_slice_checks(t)
    ensure_mode(app, "composition")


def layer_slice_checks(t):
    """Slice ligado a uma camada: combo Fonte, "Incluir efeitos do master", menu de contexto (Vincular à camada /
    Criar camada para este slice), desfazer do Mapeamento e o ícone de link no cabeçalho da camada."""
    app = t.app
    ensure_mode(app, "mapping")
    s = app.read()["state"]
    ids = [l["id"] for l in s["layers"]]
    expect(len(ids) >= 3 and len(set(ids)) == len(ids) and min(ids) > 0, "cada camada tem um id próprio: %r" % ids)
    expect(not any(l["sliceOnly"] for l in s["layers"]), "nenhuma camada nasce como só-nos-slices")
    expect(s["layerViews"] == 0, "nenhum slice ligado: nenhuma vista de camada (custo zero)")
    layers0 = len(s["layers"])
    app.click("Renomeado", "Lista")
    app.wait_frames(3)

    def cur():
        st = app.read()["state"]
        mp = st["mapping"]
        return st, st["screens"][mp["screen"]]["slices"][mp["slice"]]

    def center_of_slice():
        st, sl = cur()
        app.click("##map_ajustar", "Entrada")
        app.wait_frames(4)
        img = app.find("entrada:imagem").rect
        k = img.w / st["canvas"][0]
        x, y, w, h = sl["input"]
        return img.x0 + (x + w / 2) * k, img.y0 + (y + h / 2) * k

    st, sl = cur()
    expect(sl["source"] == 0 and sl["layerId"] == 0, "slice nasce em Composição: %r" % ((sl["source"], sl["layerId"]),))
    expect(not app.has("##sliceMaster", "Propriedades"), "o checkbox do master só aparece com camada")
    pick_combo(app, "##sliceFonte", "Propriedades", "##fonte_camada_2")
    st, sl = cur()
    expect(sl["source"] == 1 and sl["layerId"] == st["layers"][1]["id"] and not sl["includeMaster"],
           "combo Fonte liga o slice à Camada 2 pelo id: %r" % ((sl["source"], sl["layerId"]),))
    app.wait_state(lambda s: s["layerViews"] == 1, "um slice ligado: uma vista de camada")
    expect(app.has("##sliceMaster", "Propriedades"), "checkbox Incluir efeitos do master aparece")
    t.shot("mapeamento_fonte_camada")
    app.click("##sliceMaster", "Propriedades")
    app.wait_state(lambda s: s["screens"][s["mapping"]["screen"]]["slices"][s["mapping"]["slice"]]["includeMaster"],
                   "incluir master ligado")
    app.wait_frames(4)

    app.click("##map_desfazer", "Entrada")
    app.wait_state(lambda s: not s["screens"][s["mapping"]["screen"]]["slices"][s["mapping"]["slice"]]["includeMaster"],
                   "desfazer o checkbox do master")
    st, sl = cur()
    expect(sl["source"] == 1, "desfazer só tirou o master")
    app.click("##map_desfazer", "Entrada")
    app.wait_state(lambda s: s["screens"][s["mapping"]["screen"]]["slices"][s["mapping"]["slice"]]["source"] == 0,
                   "desfazer a ligação à camada")
    app.wait_state(lambda s: s["layerViews"] == 0, "sem slice ligado a vista some (custo zero)")
    app.click("##map_refazer", "Entrada")
    app.click("##map_refazer", "Entrada")
    app.wait_state(lambda s: (lambda sl: sl["source"] == 1 and sl["includeMaster"])(
        s["screens"][s["mapping"]["screen"]]["slices"][s["mapping"]["slice"]]), "refazer volta à camada com o master")
    pick_combo(app, "##sliceFonte", "Propriedades", "##fonte_comp")
    st, sl = cur()
    expect(sl["source"] == 0 and sl["layerId"] == 0 and not sl["includeMaster"], "combo Composição desliga o vínculo")
    app.wait_state(lambda s: s["layerViews"] == 0, "Composição: nenhuma vista")

    px, py = center_of_slice()
    app.click_at(px, py, button="right")
    app.wait_frames(5)
    expect(app.has("##map_ctx_vincular") and app.has("##map_ctx_criar"), "menu de contexto do slice aberto")
    app.click("##map_ctx_vincular")
    app.wait_frames(4)
    app.click("##map_ctx_camada_1")
    app.wait_frames(4)
    st, sl = cur()
    expect(sl["source"] == 1 and sl["layerId"] == st["layers"][0]["id"],
           "Vincular à camada pelo menu: %r" % ((sl["source"], sl["layerId"]),))
    app.wait_state(lambda s: s["layerViews"] == 1, "vista da camada 1")
    t.shot("mapeamento_menu_vincular")

    name = sl["name"]
    px, py = center_of_slice()
    app.click_at(px, py, button="right")
    app.wait_frames(5)
    app.click("##map_ctx_criar")
    st = app.wait_state(lambda s: len(s["layers"]) == layers0 + 1, "camada nova criada pelo menu")
    new = st["layers"][-1]
    st, sl = cur()
    expect(new["sliceOnly"] and new["name"] == name and sl["source"] == 1 and sl["layerId"] == new["id"],
           "camada nova: nome do slice, só nos slices, slice ligado a ela: %r %r" % (new, (sl["source"], sl["layerId"])))
    expect(new["id"] not in ids, "a camada nova ganhou um id inédito")
    t.shot("mapeamento_camada_criada")
    app.click("##map_desfazer", "Entrada")
    app.wait_state(lambda s: s["screens"][s["mapping"]["screen"]]["slices"][s["mapping"]["slice"]]["layerId"] == s["layers"][0]["id"],
                   "desfazer o Criar camada devolve o vínculo anterior")
    expect(len(app.read()["state"]["layers"]) == layers0 + 1, "a camada criada continua (só o vínculo foi desfeito)")
    app.click("##map_refazer", "Entrada")
    app.wait_state(lambda s: s["screens"][s["mapping"]["screen"]]["slices"][s["mapping"]["slice"]]["layerId"] == new["id"],
                   "refazer liga de novo à camada criada")

    ensure_mode(app, "composition")
    app.wait_frames(4)
    expect(app.has("##camada_1_slice"), "ícone de link no cabeçalho da camada")
    app.click("##camada_1_slice")
    app.wait_state(lambda s: s["layers"][0]["sliceOnly"], "link liga só-nos-slices na camada 1")
    t.shot("composicao_camada_slice")
    app.click("##camada_1_slice")
    app.wait_state(lambda s: not s["layers"][0]["sliceOnly"], "link desliga")
    ensure_mode(app, "mapping")


def _pm(app):
    return app.read()["state"]["pixelmap"]


def _pm_type(app, label, value, window, nth=0):
    """Numero arrastavel (hot text) ou campo: clica, Ctrl+A, digita, Enter."""
    it = app.find(label, window, nth=nth)
    app.click_at(it.rect.cx, it.rect.cy)
    app.key("a", "ctrl")
    app.type_text(str(value))
    app.key("enter")
    app.wait_frames(3)


def _pm_add(app, name, w, h, x, y):
    _pm_text(app, "##pm_nome", name)
    for lbl, v in (("##pm_dw", w), ("##pm_dh", h), ("##pm_dx", x), ("##pm_dy", y)):
        _pm_type(app, lbl, v, "pm_config")
    n = len(_pm(app)["panels"])
    app.click("Adicionar painel", "pm_config")
    app.wait_state(lambda s: len(s["pixelmap"]["panels"]) == n + 1, "painel %r adicionado" % name)


def _pm_text(app, label, value):
    it = app.find(label, "pm_config")
    app.click_at(it.rect.cx, it.rect.cy)
    app.key("a", "ctrl")
    app.type_text(value)
    app.wait_frames(3)


def _pm_clear(app):
    for _ in range(40):
        if not _pm(app)["panels"]:
            return
        app.click("##pm_remover", "pm_tabela")
        app.wait_frames(3)
    raise QaFailure("nao consegui limpar os paineis do Pixel Map")


def sc_pixelmap(t):
    app = t.app
    ensure_mode(app, "pixelmap")
    _pm_clear(app)
    app.wait_frames(4)
    for lbl in ("##pm_cadeado", "##pm_ima", "##pm_grade", "Adicionar painel", "Criar mapeamento", "Importar do mapeamento",
                "Exportar todos os padrões", "Exportar overview (PNG)", "Exportar tabela (CSV)", "Importar CSV…"):
        expect(app.has(lbl), "item do modo Pixel Map: " + lbl)
    pm = _pm(app)
    expect(pm["linked"] and pm["input"] == app.state["canvas"], "input ligado ao canvas da composição")
    expect(pm["phys"] == [2048, 640], "físico total padrão 2048x640 (como no HTML)")

    app.click("Adicionar painel", "pm_config")
    app.wait_state(lambda s: len(s["pixelmap"]["panels"]) == 1, "1o painel adicionado")
    _pm_add(app, "Lateral Esquerda", 512, 512, 0, 128)
    pm = _pm(app)
    p0, p1 = pm["panels"]
    expect(p0["name"] == "Testeira Palco" and p0["phys"] == [0, 0, 2048, 128], "1o painel do rascunho: %r" % p0)
    expect(p1["name"] == "Lateral Esquerda" and p1["phys"] == [0, 128, 512, 512], "2o painel digitado: %r" % p1)
    if pm["input"] == [1920, 1080]:
        expect(p0["comp"] == [0, 0, 1920, 216] and p1["comp"] == [0, 216, 480, 864], "escala igual à do HTML: %r %r" % (p0["comp"], p1["comp"]))
    expect(pm["scaleWarn"], "aviso de escala não inteira (0,9375 x 1,6875)")
    expect(app.has("pixelmap:aviso-escala"), "aviso discreto visível no overview")
    expect(not p0["overlap"] and not p1["overlap"], "encostados não são sobrepostos")
    t.shot("pixelmap_dois_paineis")

    _pm_type(app, "##pm_y", 100, "pm_tabela", nth=1)
    pm = _pm(app)
    expect(pm["panels"][1]["phys"][1] == 100, "Y editado na tabela")
    expect(pm["panels"][1]["comp"][1] == round(100 * app.state["canvas"][1] / 640.0 + 1e-9) or pm["input"] != [1920, 1080],
           "composição recalculada: %r" % pm["panels"][1]["comp"])
    expect(pm["panels"][0]["overlap"] and pm["panels"][1]["overlap"], "sobreposição detectada")
    t.shot("pixelmap_sobreposicao")
    _pm_type(app, "##pm_y", 128, "pm_tabela", nth=1)
    expect(not _pm(app)["panels"][1]["overlap"], "voltou a 128: sem sobreposição")

    _pm_add(app, "Fora", 100, 100, 2000, 600)
    expect(_pm(app)["panels"][2]["outside"], "painel fora da área física marcado")
    app.click("##pm_remover", "pm_tabela", nth=2)
    app.wait_state(lambda s: len(s["pixelmap"]["panels"]) == 2, "painel Fora removido")

    r = app.find("pixelmap:painel:1").rect
    app.click_at(r.cx, r.cy)
    app.wait_state(lambda s: s["pixelmap"]["selected"] == 1, "clique no overview seleciona o painel")
    if not _pm(app)["snap"]:
        app.click("##pm_ima", "pm_overview")
    cv = app.find("pixelmap:canvas").rect
    zoom = cv.w / app.state["canvas"][0]
    app.drag(r.cx, r.cy, r.cx + 2 * app.scale, r.cy + 3 * app.scale)
    app.wait_frames(4)
    p1 = _pm(app)["panels"][1]
    expect(p1["phys"][1] == 128 and p1["phys"][0] == 0, "com o ímã o painel encaixa de volta: %r" % p1["phys"])
    app.click("##pm_ima", "pm_overview")
    app.wait_state(lambda s: not s["pixelmap"]["snap"], "ímã desligado")
    r = app.find("pixelmap:painel:1").rect
    app.drag(r.cx, r.cy, r.cx + 4 * app.scale, r.cy + 6 * app.scale)
    app.wait_frames(4)
    p1 = _pm(app)["panels"][1]
    expect(p1["phys"][0] > 0 or p1["phys"][1] > 128, "sem o ímã o arraste desloca: %r" % p1["phys"])
    app.click("##pm_ima", "pm_overview")
    app.wait_state(lambda s: s["pixelmap"]["snap"], "ímã ligado")
    _pm_type(app, "##pm_x", 0, "pm_tabela", nth=1)
    _pm_type(app, "##pm_y", 128, "pm_tabela", nth=1)

    r = app.find("pixelmap:painel:1").rect
    seen = {"guides": 0}

    def probe():
        seen["guides"] = _pm(app)["guides"]

    app.drag(r.cx, r.cy, r.cx + 2 * app.scale, r.cy + 5 * app.scale, probe=probe)
    app.wait_frames(4)
    expect(_pm(app)["panels"][1]["phys"][1] == 128, "encaixa em y = 128")
    expect(seen["guides"] > 0, "linhas-guia durante o arraste com ímã")

    screens0 = len(app.state["screens"])
    undo0 = app.state["mapping"]["undo"]
    app.click("Criar mapeamento", "pm_config")
    app.wait_frames(4)
    expect(app.has("Adicionar nova tela"), "pergunta substituir ou adicionar")
    t.shot("pixelmap_criar_mapeamento")
    app.click("Adicionar nova tela", "pm_modal_criar")
    app.wait_state(lambda s: len(s["screens"]) == screens0 + 1, "tela nova criada")
    s = app.state["screens"][-1]
    expect(s["w"] == 2048 and s["h"] == 640 and len(s["slices"]) == 2, "tela com o físico total e 1 slice por painel: %r" % s["name"])
    expect([s["slices"][0]["name"], s["slices"][1]["name"]] == ["Testeira Palco", "Lateral Esquerda"], "nomes dos slices")
    pm = _pm(app)
    expect(s["slices"][0]["input"] == pm["panels"][0]["comp"], "entrada = retângulo escalado: %r" % (s["slices"][0]["input"],))
    expect(s["slices"][1]["input"] == pm["panels"][1]["comp"], "entrada do 2º slice")
    expect(s["slices"][1]["corners"] == [0, 128, 512, 128, 512, 640, 0, 640], "saída = posição física: %r" % (s["slices"][1]["corners"],))
    app.wait_state(lambda s: s["mapping"]["undo"] > undo0, "criar mapeamento entrou no histórico de desfazer")
    app.click("Criar mapeamento", "pm_config")
    app.wait_frames(4)
    expect(app.has("##pm_criar_substituir"), "opção de substituir a tela existente")
    app.click("##pm_criar_substituir", "pm_modal_criar")
    app.wait_frames(6)
    expect(len(app.state["screens"]) == screens0 + 1, "substituir não cria tela")

    _pm_clear(app)
    app.click("Importar do mapeamento", "pm_config")
    app.wait_state(lambda s: len(s["pixelmap"]["panels"]) == 2, "importar do mapeamento (sem painéis: direto)")
    pm = _pm(app)
    expect([p["phys"] for p in pm["panels"]] == [[0, 0, 2048, 128], [0, 128, 512, 512]], "ida e volta: %r" % [p["phys"] for p in pm["panels"]])
    expect(pm["phys"] == [2048, 640], "físico total = resolução da tela")
    app.click("Importar do mapeamento", "pm_config")
    app.wait_frames(4)
    expect(app.has("Importar", "pm_modal_importar"), "confirmação ao substituir painéis")
    app.click("Cancelar", "pm_modal_importar")
    app.wait_frames(3)

    ensure_mode(app, "mapping")
    app.wait_frames(4)
    for _ in range(4):
        if len(app.state["screens"]) == screens0:
            break
        app.click("##map_desfazer", "Saída")
        app.wait_frames(4)
    expect(len(app.state["screens"]) == screens0, "Ctrl+Z do mapeamento desfaz o Criar mapeamento")
    ensure_mode(app, "pixelmap")

    layers0 = len(app.state["layers"])
    app.click("Criar mapeamento", "pm_config")
    app.wait_frames(4)
    app.click("Adicionar nova tela", "pm_modal_criar")
    app.wait_state(lambda s: len(s["screens"]) == screens0 + 1, "tela criada sem a opção de camadas")
    expect(len(app.state["layers"]) == layers0, "sem a opção ligada não cria camadas")
    expect(all(sl["source"] == 0 for sl in app.state["screens"][-1]["slices"]), "slices em Composição por padrão")
    app.click("##pm_camada_por_painel", "pm_config")
    app.click("Criar mapeamento", "pm_config")
    app.wait_frames(4)
    app.click("Adicionar nova tela", "pm_modal_criar")
    app.wait_state(lambda s: len(s["layers"]) == layers0 + 2, "uma camada por painel criada")
    s = app.state
    scr = s["screens"][-1]
    for i, sl in enumerate(scr["slices"]):
        lay = s["layers"][layers0 + i]
        expect(lay["name"] == sl["name"] and lay["sliceOnly"] and sl["source"] == 1 and sl["layerId"] == lay["id"],
               "painel %d: camada própria ligada ao slice: %r %r" % (i, lay["name"], sl["name"]))
    app.click("##pm_camada_por_painel", "pm_config")

    sw = app.find("##pm_cor", "pm_tabela")
    app.click_at(sw.rect.cx, sw.rect.cy)
    app.wait_frames(3)
    t.shot("pixelmap_final")
    _pm_clear(app)
    ensure_mode(app, "composition")


def sc_grade(t):
    app = t.app
    ensure_mode(app, "composition")
    pick_tab(app, "Arquivos")
    app.click("##caminho", "cmp_arquivos")
    app.key("a", "ctrl")
    app.type_text(MEDIA)
    app.key("enter")
    app.wait_frames(4)
    expect(app.has("photo.jpg", "cmp_arquivos"), "navegador listou a pasta digitada")

    for name, (l, c) in {"photo.jpg": (1, 1), "h264_1080p.mp4": (1, 2), "alpha.png": (2, 1), "anim.gif": (2, 3)}.items():
        src = app.find(name, "cmp_arquivos")
        app.drag_items(src, cell(app, l, c), steps=18)
        app.wait_state(lambda s, l=l, c=c: any(x[0] == l - 1 and x[1] == c - 1 for x in s["cells"]), "arrastar %s para L%d C%d" % (name, l, c))
    s = app.state
    expect(len(s["cells"]) == 4, "4 clips na grade (%s)" % s["cells"])

    app.click("celula L1 C1")
    s = app.wait_state(lambda s: s["layers"][0]["activeColumn"] == 0, "clique na celula dispara o clip")
    expect(s["selection"]["layer"] == 0 and s["selection"]["column"] == 0, "celula ficou selecionada")
    t.shot("grade_clip_ativo")
    app.click("coluna 2")
    app.wait_state(lambda s: s["layers"][0]["activeColumn"] == 1, "coluna 2 disparada")
    app.click("##camada_1_parar")
    app.wait_state(lambda s: s["layers"][0]["activeColumn"] == -1, "X da camada para o clip")
    cols, lay = app.state["columns"], len(app.state["layers"])
    scroll_to_visible(app, "##coluna_nova", cell(app, 2, 3))
    app.click("##coluna_nova")
    app.wait_state(lambda s: s["columns"] == cols + 1, "+ coluna")
    app.wheel(cell(app, 2, 3).rect.cx, cell(app, 2, 3).rect.cy, 15, shift=True)
    app.click("##camada_nova", "cmp_grade")
    app.wait_state(lambda s: len(s["layers"]) == lay + 1, "+ camada")
    a, b = cell(app, 1, 1), cell(app, 1, 5)
    app.drag_items(a, b, steps=18)
    app.wait_state(lambda s: any(x[0] == 0 and x[1] == 4 for x in s["cells"]) and not any(x[0] == 0 and x[1] == 0 for x in s["cells"]),
                   "arrastar celula L1C1 para L1C5 (troca com vazia)")
    app.click("celula L1 C5", button="right")
    app.wait_frames(3)
    app.click("Limpar")
    app.wait_state(lambda s: not any(x[0] == 0 and x[1] == 4 for x in s["cells"]), "botao direito > Limpar")
    app.click("celula L1 C2")
    app.wait_state(lambda s: s["selection"]["column"] == 1 and s["selection"]["layer"] == 0, "seleciona L1C2")
    app.key("delete", "shift")
    app.wait_state(lambda s: not any(x[0] == 0 and x[1] == 1 for x in s["cells"]), "Shift+Delete limpa a celula selecionada")
    n = len(app.state["cells"])
    app.drop_files([os.path.join(MEDIA, "photo.jpg")], cell(app, 3, 1))
    app.wait_state(lambda s: len(s["cells"]) == n + 1 and any(x[0] == 2 and x[1] == 0 for x in s["cells"]), "drop WM_DROPFILES sobre L3C1")
    app.drop_files([os.path.join(MEDIA, "static.webp"), os.path.join(MEDIA, "vp8.webm")], cell(app, 3, 3))
    app.wait_state(lambda s: any(x[0] == 2 and x[1] == 3 for x in s["cells"]), "drop de 2 arquivos preenche celulas seguintes")
    t.shot("grade_preenchida")
    app.click("celula L1 C1")
    app.wait_frames(10)
    app.click("##camada_1_parar")


def sc_decks(t):
    app = t.app
    ensure_mode(app, "composition")
    n = len(app.state["decks"])
    app.click("##deck_novo")
    app.wait_state(lambda s: len(s["decks"]) == n + 1 and s["deck"] == n, "+ deck cria e troca")
    app.click("deck %d" % (n + 1), double=True)
    app.wait_frames(3)
    app.key("a", "ctrl")
    app.type_text("Show B")
    app.key("enter")
    app.wait_state(lambda s: s["decks"][n]["name"] == "Show B", "renomear deck")
    app.click("deck 1")
    app.wait_state(lambda s: s["deck"] == 0, "trocar de deck")
    app.click("deck 1", button="right")
    app.wait_frames(3)
    app.click("Excluir", "Popup_")
    app.wait_frames(4)
    expect(app.has("Cancelar") and app.has("Excluir", "Excluir deck"), "modal de confirmacao de exclusao apareceu")
    t.shot("deck_confirmar_exclusao")
    app.click("Cancelar", "Excluir deck")
    app.wait_frames(3)
    expect(len(app.state["decks"]) == n + 1, "Cancelar mantem o deck")
    app.click("deck 1", button="right")
    app.wait_frames(3)
    app.click("Excluir", "Popup_")
    app.wait_frames(4)
    app.click("Excluir", "Excluir deck")
    app.wait_state(lambda s: len(s["decks"]) == n, "Excluir remove o deck")
    expect(app.has("celula L1 C1"), "grade desenhada depois de excluir")


def sc_transporte(t):
    app = t.app
    ensure_mode(app, "composition")
    app.click("##transport_pause")
    app.wait_state(lambda s: s["paused"], "pause")
    app.click("##transport_play")
    app.wait_state(lambda s: not s["paused"], "play")
    app.click("##transport_stop")
    app.wait_frames(3)
    tap = app.find("TAP")
    stamps = []
    for i in range(4):
        app.click_at(tap.rect.cx, tap.rect.cy)
        stamps.append(time.time())
        time.sleep(0.12)
    app.wait_frames(2)
    bpm = app.state["bpm"]
    avg = (stamps[-1] - stamps[0]) / 3.0
    expect(abs(bpm - 60.0 / avg) / (60.0 / avg) < 0.2,
           "TAP 4x ajustou o BPM (%.1f, esperado ~%.1f)" % (bpm, 60.0 / avg))
    app.click("##bpm", "cmp_grade")
    app.wait_frames(3)
    app.key("a", "ctrl")
    app.type_text("98")
    app.key("enter")
    app.wait_state(lambda s: abs(s["bpm"] - 98.0) < 0.01, "digitar BPM 98")
    app.click("÷2")
    app.wait_state(lambda s: abs(s["bpm"] - 49.0) < 0.01, "÷2")
    app.click("×2")
    app.wait_state(lambda s: abs(s["bpm"] - 98.0) < 0.01, "×2")
    app.click("##bpm_mais")
    app.click("##bpm_menos")
    app.click("##transport_resync")
    t.shot("transporte")


def sc_hot(t):
    """Numeros arrastaveis (hot text): arrastar, Shift = x10, clicar e digitar, Esc, Tab, Restaurar padrao."""
    app = t.app
    ensure_mode(app, "composition")
    r = app.find("##bpm", "cmp_grade").rect
    app.click("##bpm_menos")
    app.click("##bpm_menos")
    b0 = app.state["bpm"]
    app.drag(r.x0 + 6, r.cy, r.x0 + 46, r.cy, steps=10)
    b1 = app.state["bpm"]
    expect(b1 > b0 + 2.0, "arrastar o BPM para a direita aumenta (%.2f -> %.2f)" % (b0, b1))
    r = app.find("##bpm", "cmp_grade").rect
    app.drag(r.x0 + 6, r.cy, r.x0 + 26, r.cy, steps=10, mods=("shift",))
    b2 = app.state["bpm"]
    expect(b2 > b1 + 10.0, "Shift multiplica o arrasto por 10 (%.2f -> %.2f)" % (b1, b2))
    r = app.find("##bpm", "cmp_grade").rect
    app.drag(r.x0 + 6, r.cy, r.x0 + 46, r.cy, steps=10, mods=("ctrl",))
    b3 = app.state["bpm"]
    expect(0.0 < b3 - b2 < 1.0, "Ctrl reduz o arrasto a 1/10 (%.2f -> %.2f)" % (b2, b3))
    t.shot("hot_arrastando")
    app.click("##bpm", "cmp_grade")
    app.key("a", "ctrl")
    app.type_text("100")
    t.shot("hot_digitando")
    app.key("enter")
    app.wait_state(lambda s: abs(s["bpm"] - 100.0) < 0.01, "digitar 100 no BPM e Enter")
    app.click("##bpm", "cmp_grade")
    app.key("a", "ctrl")
    app.type_text("55")
    app.key("esc")
    app.wait_frames(3)
    expect(abs(app.state["bpm"] - 100.0) < 0.01, "Esc cancela a digitacao (bpm %.2f)" % app.state["bpm"])
    app.click("##bpm", "cmp_grade", button="right")
    app.wait_frames(3)
    app.click("Restaurar padrão", "Popup_")
    app.wait_state(lambda s: abs(s["bpm"] - 120.0) < 0.01, "Restaurar padrão volta o BPM a 120")
    app.click("##bpm", "cmp_grade")
    app.key("a", "ctrl")
    app.type_text("9999")
    app.key("enter")
    app.wait_state(lambda s: abs(s["bpm"] - 300.0) < 0.01, "9999 vira o maximo (300)")
    app.click("##bpm", "cmp_grade", button="right")
    app.wait_frames(3)
    app.click("Restaurar padrão", "Popup_")
    app.wait_state(lambda s: abs(s["bpm"] - 120.0) < 0.01, "BPM de volta a 120")
    n = len(app.state["layers"])
    app.click("##master_opacidade", "cmp_grade")
    app.key("a", "ctrl")
    app.type_text("80")
    app.key("tab")
    app.wait_state(lambda s: abs(s["masterOpacity"] - 0.8) < 0.01, "Tab confirma o master (80%)")
    app.wait_frames(3)
    app.key("a", "ctrl")
    app.type_text("70")
    app.key("enter")
    app.wait_state(lambda s: abs(s["layers"][n - 1]["opacity"] - 0.7) < 0.01, "Tab abriu a opacidade da camada %d" % n)
    for label in ("##master_opacidade", "##camada_%d_opacidade" % n):
        app.click(label, "cmp_grade", button="right")
        app.wait_frames(3)
        app.click("Restaurar padrão", "Popup_")
        app.wait_frames(3)
    expect(abs(app.state["masterOpacity"] - 1.0) < 0.01 and abs(app.state["layers"][n - 1]["opacity"] - 1.0) < 0.01,
           "Restaurar padrão nas opacidades")
    t.shot("hot_opacidades")


def ensure_video_l1c2(app):
    """Deixa um video na celula L1C2 (navega o painel Arquivos ate a pasta de teste se preciso: os cenarios rodam sozinhos)."""
    if any(x[0] == 0 and x[1] == 1 for x in app.state["cells"]):
        return
    pick_tab(app, "Arquivos")
    if not app.has("h264_1080p.mp4", "cmp_arquivos"):
        app.click("##caminho", "cmp_arquivos")
        app.key("a", "ctrl")
        app.type_text(MEDIA)
        app.key("enter")
        app.wait_frames(4)
    app.drag_items(app.find("h264_1080p.mp4", "cmp_arquivos"), cell(app, 1, 2), steps=18)
    app.wait_state(lambda s: any(x[0] == 0 and x[1] == 1 for x in s["cells"]), "video na L1C2")


def sc_clip(t):
    app = t.app
    ensure_mode(app, "composition")
    ensure_video_l1c2(app)
    app.click("celula L1 C2")
    app.wait_state(lambda s: s.get("selClip", {}).get("open"), "clip aberto")
    app.wait_frames(10)
    t.shot("clip_video")
    app.click("##clip_tocar_pausar", "cmp_clip")
    app.wait_state(lambda s: not s["selClip"]["playing"], "Pausar")
    app.click("##clip_tocar_pausar", "cmp_clip")
    app.wait_state(lambda s: s["selClip"]["playing"], "Tocar")
    app.click("##clip_reiniciar", "cmp_clip")
    sp = app.find("##velocidade", "cmp_clip").rect
    app.drag(sp.x0 + 4, sp.cy, sp.x0 + 64, sp.cy, steps=8)
    s = app.wait_state(lambda s: abs(s["selClip"]["speed"] - 1.0) > 0.1, "arrastar a velocidade")
    app.click("1.0x", "cmp_clip")
    app.wait_state(lambda s: abs(s["selClip"]["speed"] - 1.0) < 0.001, "1.0x")
    app.click("##loop_uma_vez", "cmp_clip")
    app.wait_state(lambda s: s["selClip"]["loop"] == 1, "loop = Uma vez")
    app.click("##fit", "cmp_clip")
    app.wait_frames(3)
    app.click("Preencher", "Combo")
    app.wait_state(lambda s: s["selClip"]["fit"] == 1, "encaixe = Preencher")
    pos = app.find("##posicao", "cmp_clip").rect
    app.drag(pos.x0 + 6, pos.cy, pos.x0 + pos.w * 0.5, pos.cy, steps=8)
    app.wait_frames(4)
    app.click("##loop_loop", "cmp_clip")
    app.wait_state(lambda s: s["selClip"]["loop"] == 0, "loop = Loop")
    app.click("##camada_1_parar")


def fully_visible(app, label, window):
    """Primeiro item 'label' da janela que cabe inteiro nela (nao cortado pela rolagem)."""
    win = app.window_rect(window)
    for it in app.find_all(label, window):
        if win and it.rect.y0 >= win.y0 and it.rect.y1 <= win.y1 and it.rect.x0 >= win.x0 and it.rect.x1 <= win.x1:
            return it
    return None


def scroll_into_view(app, label, window, tries=14):
    """Rola a janela (roda do mouse) ate o item aparecer inteiro; devolve o item."""
    for i in range(tries):
        app.read()
        it = fully_visible(app, label, window)
        if it:
            return it
        win = app.window_rect(window)
        if not win:
            break
        app.wheel(win.cx, win.cy, -2 if i < tries // 2 else 2)
    raise QaFailure("nao consegui rolar ate %r em %s" % (label, window))


def search_effect(app, text):
    """Filtra a lista da aba Efeitos pelo texto (campo de busca) e devolve o primeiro item com esse nome."""
    app.click("##efeitos_busca", "cmp_efeitos")
    app.key("a", "ctrl")
    app.type_text(text)
    app.wait_frames(3)
    return app.find(text, "cmp_efeitos")


def sc_efeitos(t):
    """Cadeia de efeitos: arrastar da aba para a celula/painel Clip/camada/master, duplo clique, reordenar, bypass,
    menu (duplicar, restaurar, remover) e os geradores/efeitos ISF."""
    app = t.app
    ensure_mode(app, "composition")
    ensure_video_l1c2(app)
    app.click("celula L1 C2")
    app.wait_state(lambda s: s.get("selClip", {}).get("open"), "clip aberto")
    pick_tab(app, "Efeitos")
    app.wait_state(lambda s: s["isf"]["entries"] >= 4 and not s["isf"]["scanning"], "shaders ISF varridos", timeout=10)
    app.find("BSL Light", "cmp_efeitos")
    expect(bool(app.find_all("Nativos", "cmp_efeitos", visible=False)) and bool(app.find_all("ISF", "cmp_efeitos", visible=False)),
           "secoes Nativos e ISF na aba Efeitos")
    expect(bool(app.find_all("FFGL", "cmp_efeitos", visible=False)) and app.has("##plugins_abrir", "cmp_efeitos"),
           "secao FFGL e botao Plugins na aba Efeitos")
    expect(app.has("BSL Light", "cmp_efeitos") and app.has("BSL Slice Refiner", "cmp_efeitos"),
           "secao BSL Light (plugins nativos) no topo da aba Efeitos")
    t.shot("efeitos_aba")

    def types():
        return [e["type"] for e in app.state["selClip"]["effects"]]

    app.drag_items(search_effect(app, "Inverter"), cell(app, 1, 2), steps=18)
    app.wait_state(lambda s: [e["type"] for e in s["selClip"]["effects"]] == ["native:invert"], "arrastar para a celula")
    expect(app.has("celula L1 C2"), "celula continua na grade")
    search_effect(app, "Vinheta")
    app.click("Vinheta", "cmp_efeitos", double=True)
    app.wait_state(lambda s: [e["type"] for e in s["selClip"]["effects"]] == ["native:invert", "native:vignette"],
                   "duplo clique adiciona ao clip selecionado")
    search_effect(app, "Strobe")
    drop = scroll_into_view(app, "##clip_efeitos_soltar", "cmp_clip")
    app.drag_items(app.find("Strobe", "cmp_efeitos"), drop, steps=18)
    app.wait_state(lambda s: len(s["selClip"]["effects"]) == 3 and s["selClip"]["effects"][2]["type"] == "native:strobe",
                   "arrastar para a area do painel Clip")
    t.shot("efeitos_cadeia_clip")
    src = scroll_into_view(app, "##clip_efeito_3_alca", "cmp_clip")
    dst = scroll_into_view(app, "##clip_efeito_1_cabecalho", "cmp_clip")
    if not (fully_visible(app, "##clip_efeito_3_alca", "cmp_clip") and fully_visible(app, "##clip_efeito_1_cabecalho", "cmp_clip")):
        app.click("##clip_efeito_1_cabecalho", "cmp_clip")
        app.wait_frames(3)
        src = scroll_into_view(app, "##clip_efeito_3_alca", "cmp_clip")
        dst = scroll_into_view(app, "##clip_efeito_1_cabecalho", "cmp_clip")
    app.drag_items(src, dst, steps=18)
    app.wait_state(lambda s: [e["type"] for e in s["selClip"]["effects"]] == ["native:strobe", "native:invert", "native:vignette"],
                   "reordenar arrastando a alca")
    b1 = scroll_into_view(app, "##clip_efeito_1_bypass", "cmp_clip")
    app.click_at(b1.rect.cx, b1.rect.cy)
    app.wait_state(lambda s: s["selClip"]["effects"][0]["bypass"], "bypass do 1o efeito")
    t.shot("efeitos_bypass")
    b1 = scroll_into_view(app, "##clip_efeito_1_bypass", "cmp_clip")
    app.click_at(b1.rect.cx, b1.rect.cy)
    app.wait_state(lambda s: not s["selClip"]["effects"][0]["bypass"], "tirar o bypass")
    m2 = scroll_into_view(app, "##clip_efeito_2_menu", "cmp_clip")
    app.click_at(m2.rect.cx, m2.rect.cy)
    app.wait_frames(3)
    app.click("##efeito_duplicar", "Popup_")
    app.wait_state(lambda s: len(s["selClip"]["effects"]) == 4 and s["selClip"]["effects"][2]["type"] == "native:invert",
                   "menu: Duplicar")
    m3 = scroll_into_view(app, "##clip_efeito_3_menu", "cmp_clip")
    app.click_at(m3.rect.cx, m3.rect.cy)
    app.wait_frames(3)
    app.click("##efeito_remover", "Popup_")
    app.wait_state(lambda s: len(s["selClip"]["effects"]) == 3, "menu: Remover")
    m1 = scroll_into_view(app, "##clip_efeito_1_menu", "cmp_clip")
    app.click_at(m1.rect.cx, m1.rect.cy)
    app.wait_frames(3)
    app.click("##efeito_restaurar", "Popup_")
    app.wait_frames(3)
    expect(app.state["dirty"], "mexer nos efeitos marca o projeto como modificado")
    for _ in range(3):
        m = scroll_into_view(app, "##clip_efeito_1_menu", "cmp_clip")
        app.click_at(m.rect.cx, m.rect.cy)
        app.wait_frames(3)
        app.click("##efeito_remover", "Popup_")
        app.wait_frames(3)
    app.wait_state(lambda s: len(s["selClip"]["effects"]) == 0, "cadeia do clip esvaziada")

    pick_tab(app, "Efeitos")
    app.drag_items(search_effect(app, "Espelho"), app.find("camada 1 nome"), steps=18)
    app.wait_state(lambda s: [e["type"] for e in s["layers"][0]["effects"]] == ["native:mirror"], "soltar no cabecalho da camada")
    pick_tab(app, "Camada")
    app.wait_frames(3)
    expect(scroll_into_view(app, "##camada_efeito_1_cabecalho", "cmp_aba_camada"), "cadeia da camada na aba Camada")
    t.shot("efeitos_camada")
    mm = scroll_into_view(app, "##camada_efeito_1_menu", "cmp_aba_camada")
    app.click_at(mm.rect.cx, mm.rect.cy)
    app.wait_frames(3)
    app.click("##efeito_remover", "Popup_")
    app.wait_state(lambda s: len(s["layers"][0]["effects"]) == 0, "remover o efeito da camada")

    tabs = sorted(app.find_all("tab:Composi\u00e7\u00e3o", "dock-tabs"), key=lambda i: i.rect.y0)
    app.click_at(tabs[-1].rect.cx, tabs[-1].rect.cy)
    app.wait_frames(3)
    add = scroll_into_view(app, "##master_efeito_adicionar", "cmp_aba_composicao")
    app.click_at(add.rect.cx, add.rect.cy)
    app.wait_frames(4)
    app.type_text("Vinheta")
    app.wait_frames(3)
    app.click("##add_native:vignette", "Popup_")
    app.wait_state(lambda s: [e["type"] for e in s["masterEffects"]] == ["native:vignette"], "master: adicionar pelo catalogo")
    t.shot("efeitos_master")
    bm = scroll_into_view(app, "##master_efeito_1_bypass", "cmp_aba_composicao")
    app.click_at(bm.rect.cx, bm.rect.cy)
    app.wait_state(lambda s: s["masterEffects"][0]["bypass"], "master: bypass")
    mm = scroll_into_view(app, "##master_efeito_1_menu", "cmp_aba_composicao")
    app.click_at(mm.rect.cx, mm.rect.cy)
    app.wait_frames(3)
    app.click("##efeito_remover", "Popup_")
    app.wait_state(lambda s: len(s["masterEffects"]) == 0, "master: remover")

    pick_tab(app, "Efeitos")
    app.drag_items(search_effect(app, "Ondas"), cell(app, 1, 2), steps=18)
    app.wait_state(lambda s: [e["type"] for e in s["selClip"]["effects"]] == ["isf:Ondas.fs"], "efeito ISF na celula")
    app.wait_frames(15)
    pick_tab(app, "Fontes")
    app.drag_items(scroll_into_view(app, "Gradiente animado", "cmp_geradores"), cell(app, 3, 3), steps=18)
    app.wait_state(lambda s: any(x[0] == 2 and x[1] == 2 and "Gradiente" in x[2] for x in s["cells"]), "gerador ISF na grade")
    app.click("celula L3 C3")
    app.wait_state(lambda s: s.get("selClip", {}).get("open") and s["selection"]["layer"] == 2, "gerador ISF aberto")
    app.wait_frames(20)
    expect(len(app.find_all("##v", "cmp_clip")) >= 3, "parametros do gerador ISF no painel Clip")
    t.shot("efeitos_isf")
    app.click("##camada_3_parar")
    app.click("celula L1 C2")
    app.wait_state(lambda s: s["selection"]["layer"] == 0 and s["selection"]["column"] == 1, "volta ao clip L1C2")
    m = scroll_into_view(app, "##clip_efeito_1_menu", "cmp_clip")
    app.click_at(m.rect.cx, m.rect.cy)
    app.wait_frames(3)
    app.click("##efeito_remover", "Popup_")
    app.wait_state(lambda s: len(s["selClip"]["effects"]) == 0, "limpa o efeito ISF")

    pick_tab(app, "Efeitos")
    app.drag_items(search_effect(app, "BSL Slice Refiner"), cell(app, 1, 2), steps=18)
    app.wait_state(lambda s: [e["type"] for e in s["selClip"]["effects"]] == ["bsl:slice-refiner"], "Slice Refiner na celula")
    todos = scroll_into_view(app, "Todos##sl_todos", "cmp_clip")
    app.click_at(todos.rect.cx, todos.rect.cy)
    app.wait_frames(3)
    send = scroll_into_view(app, "##enviar_grupo_2", "cmp_clip")
    app.click_at(send.rect.cx, send.rect.cy)
    app.wait_frames(4)
    ficha = scroll_into_view(app, "##ficha_grupo_2", "cmp_clip")
    expect(ficha.label.startswith("2·"), "o slice foi para o grupo 2 (ficha mostra a contagem: %r)" % ficha.label)
    t.shot("efeitos_slice_refiner")
    expect(app.has("##g_ativo", "cmp_clip") or bool(app.find_all("##g_ativo", "cmp_clip", visible=False)), "controles do grupo no editor")
    win = app.window_rect("cmp_clip")
    for _ in range(4):
        app.wheel(win.cx, win.y0 + 60, 30)
        app.wait_frames(2)
    m = scroll_into_view(app, "##clip_efeito_1_menu", "cmp_clip", tries=30)
    app.click_at(m.rect.cx, m.rect.cy)
    app.wait_frames(3)
    app.click("##efeito_remover", "Popup_")
    app.wait_state(lambda s: len(s["selClip"]["effects"]) == 0, "remove o Slice Refiner")
    app.click("##camada_1_parar")


def sc_geradores(t):
    app = t.app
    ensure_mode(app, "composition")
    pick_tab(app, "Fontes")
    app.wait_frames(3)
    win = app.window_rect("cmp_geradores")
    if win:
        app.wheel(win.cx, win.cy, 20)
    expect(app.has("BSL Light", "cmp_geradores") and app.has("BSL WebBackgrounds", "cmp_geradores")
           and app.has("Padrão de calibração BSL", "cmp_geradores"),
           "secao BSL Light na aba Fontes")
    expect(not app.has("LED Mapper Pro", "cmp_geradores") and not app.has("BSL Background", "cmp_geradores")
           and not app.has("BSL PPT Manager", "cmp_geradores") and not app.has("BSL WebMotion", "cmp_geradores"),
           "fontes BSL removidas nao aparecem mais na aba Fontes")
    t.shot("geradores")
    app.drag_items(app.find("Texto", "cmp_geradores"), cell(app, 3, 2), steps=18)
    app.wait_state(lambda s: any(x[0] == 2 and x[1] == 1 for x in s["cells"]), "gerador Texto arrastado")
    app.click("celula L3 C2")
    app.wait_state(lambda s: s["selection"]["layer"] == 2 and s["selection"]["column"] == 1, "seleciona Texto")
    app.wait_frames(10)
    fields = app.find_all("##v", "cmp_clip")
    expect(len(fields) >= 3, "painel Clip mostra os parametros do Texto (%d)" % len(fields))
    f0 = fields[0]
    app.click_at(f0.rect.cx, f0.rect.cy)
    app.key("a", "ctrl")
    app.type_text("Ola BSL")
    app.wait_state(lambda s: s["selClip"]["params"].get("text") == "Ola BSL", "digitar no parametro Texto")
    t.shot("clip_texto")
    for i in range(1, min(len(fields), 12)):
        app.read()
        fs = app.find_all("##v", "cmp_clip")
        if i >= len(fs):
            break
        app.click_at(fs[i].rect.cx, fs[i].rect.cy)
        app.wait_frames(2)
        app.key("esc")
    app.click("celula L3 C4")
    pick_tab(app, "Fontes")
    app.click("Cor sólida", "cmp_geradores", double=True)
    app.wait_state(lambda s: any(x[0] == 2 and x[1] == 3 and "Cor" in x[2] for x in s["cells"]), "duplo clique carrega gerador")
    app.click("celula L3 C3")
    pick_tab(app, "Fontes")
    app.click("Padrão de calibração BSL", "cmp_geradores", double=True)
    app.wait_state(lambda s: any(x[0] == 2 and x[1] == 2 for x in s["cells"]), "duplo clique carrega o padrao de calibracao")
    app.wait_frames(10)
    t.shot("clip_padrao_calibracao")
    app.drag_items(app.find("Captura de tela", "cmp_geradores"), cell(app, 3, 5), steps=18)
    app.wait_frames(10)
    expect(app.has("Atualizar lista"), "seletor de alvo da captura abriu ao soltar o gerador")
    t.shot("captura_seletor")
    app.click("Atualizar lista")
    wait_items_stable(app, "##card")
    for _ in range(3):
        app.click("##card", nth=0)
        app.wait_frames(6)
        if not app.has("Atualizar lista"):
            break
        wait_items_stable(app, "##card")
    expect(not app.has("Atualizar lista"), "escolher o alvo fecha o seletor")
    expect("target" in app.state["selClip"]["params"], "alvo gravado nos parametros")
    alvo = [i for i in app.items if "cmp_clip" in i.window and i.label.endswith("##alvo") and i.visible]
    expect(bool(alvo), "botao do alvo no painel Clip")
    app.click_at(alvo[0].rect.cx, alvo[0].rect.cy)
    app.wait_frames(8)
    app.click("Cancelar")
    app.wait_frames(4)
    expect(not app.has("Atualizar lista"), "Cancelar fecha o seletor")
    pick_tab(app, "Arquivos")
    app.click("docs/", "cmp_arquivos")
    app.wait_frames(4)
    app.drag_items(app.find("sample.pdf", "cmp_arquivos"), cell(app, 2, 6), steps=18)
    app.wait_state(lambda s: any(x[0] == 1 and x[1] == 5 for x in s["cells"]), "PDF na grade")
    app.click("celula L2 C6")
    app.wait_frames(40)
    t.shot("clip_pdf")
    if app.has("##paginaSeguinte", "cmp_clip"):
        app.click("##paginaSeguinte", "cmp_clip")
        app.click("##paginaAnterior", "cmp_clip")
    app.click("##acima", "cmp_arquivos")
    app.wait_frames(3)
    for l in (2, 3):
        app.click("##camada_%d_parar" % l)


def close_stray_dialogs(app):
    """Fecha qualquer dialogo nativo (Salvar/Abrir) que ficou aberto: com ele aberto o app trava e nada mais responde."""
    for _ in range(4):
        dlg = app.find_dialog(timeout=0.4)
        if not dlg:
            return
        app.dialog_close(dlg)
        app.wait_frames(4)


def sc_projeto(t):
    """Projeto: salvar, sujo, modal Salvar alteracoes?, abrir recente, novo. O "Salvar como" usa o caminho injetado pelo modo
    --qa (BSLSTAGE_QA_SAVE_PATH, definido em main()): a digitacao no dialogo nativo nao e confiavel. O dialogo real fica no
    cenario opcional projeto_dialogo. Nenhum dialogo pode ficar aberto quando o cenario falha."""
    try:
        _projeto_body(t)
    finally:
        close_stray_dialogs(t.app)


def _projeto_body(t):
    app = t.app
    ensure_mode(app, "composition")
    target = os.path.join(app.dir, "proj", "teste.bslstage")
    os.makedirs(os.path.dirname(target), exist_ok=True)
    if os.path.exists(target):
        os.remove(target)
    expect(os.path.normcase(os.environ.get("BSLSTAGE_QA_SAVE_PATH", "")) == os.path.normcase(target),
           "BSLSTAGE_QA_SAVE_PATH aponta para %s (definido por run.py)" % target)
    app.focus()
    app.key("s", "ctrl")
    app.wait_state(lambda s: s["project"].lower().endswith("teste.bslstage"), "projeto salvo no caminho injetado", timeout=10)
    expect(os.path.exists(target), "arquivo .bslstage gravado")
    expect(not app.state["dirty"], "projeto limpo depois de salvar")
    menu(app, "Composição", "+ Coluna")
    app.wait_state(lambda s: s["dirty"], "alteracao marca o projeto como modificado")
    app.key("o", "ctrl")
    app.wait_frames(4)
    expect(app.has("Não salvar"), "modal Salvar alterações? apareceu")
    t.shot("modal_salvar_alteracoes")
    app.click("Cancelar", "Salvar")
    app.wait_frames(3)
    expect(not app.has("Não salvar") and app.state["dirty"], "Cancelar mantem tudo como estava")
    app.key("o", "ctrl")
    app.wait_frames(4)
    clicked_dialog(app, "Não salvar", "Salvar")
    dlg = app.find_dialog()
    expect(dlg != 0, "Nao salvar abriu o dialogo Abrir")
    app.dialog_close(dlg)
    app.wait_frames(6)
    menu(app, "Composição", "+ Coluna")
    app.wait_state(lambda s: s["dirty"], "sujo de novo")
    app.key("o", "ctrl")
    app.wait_frames(4)
    clicked_dialog(app, "Salvar", "Salvar")
    dlg = app.find_dialog()
    expect(dlg != 0, "Salvar abriu o dialogo Abrir")
    app.dialog_close(dlg)
    app.wait_frames(6)
    expect(not app.state["dirty"], "Salvar gravou o projeto")
    menu(app, "Arquivo", "##menu_recentes", "teste.bslstage")
    app.wait_frames(10)
    expect(app.state["project"].lower().endswith("teste.bslstage"), "Abrir recente reabriu o projeto")
    app.key("n", "ctrl")
    app.wait_frames(10)
    expect(app.state["project"] == "", "Ctrl+N cria projeto novo")
    t.shot("projeto_novo")


def driver_type(app, text):
    """Digita no dialogo nativo (tem o foco do teclado): o app esta bloqueado no dialogo."""
    app.type_text_nowait(text)
    driver._key(driver.VK["enter"])
    driver._key(driver.VK["enter"], up=True)


def clicked_dialog(app, label, window):
    it = app.find(label, window)
    app.click_at(it.rect.cx, it.rect.cy)


def _tr(app, i):
    """Transform da camada i (0-based) no ultimo snapshot."""
    return app.read()["state"]["layers"][i]["transform"]


def _close(a, b, tol):
    return abs(a - b) <= tol


def _type_number(app, label, window, value):
    """Numero arrastavel (hot text): clica, Ctrl+A, digita, Enter."""
    app.click(label, window)
    app.key("a", "ctrl")
    app.type_text(str(value))
    app.key("enter")
    app.wait_frames(3)


def sc_transform(t):
    """Transform da camada (item 6): alcas na Previa, presets (Grade 2x2), Ctrl+Z / Ctrl+Y, clicar numa area, duplo clique,
    setas, Recortar, numeros da aba Camada, o monitor de Saida e a Previa em Camada selecionada."""
    app = t.app
    ensure_mode(app, "composition")
    app.wait_frames(8)
    while len(app.state["layers"]) < 4:
        n = len(app.state["layers"])
        app.click("##camada_nova")
        app.wait_state(lambda s, n=n: len(s["layers"]) == n + 1, "nova camada")
    for i, name in enumerate(("h264_1080p.mp4", "person.jpg", "alpha.png", "vp8.webm")):
        if not app.has("celula L%d C1" % (i + 1)):
            scroll_into_view(app, "celula L%d C1" % (i + 1), "cmp_grade")
        drop_clip(app, name, i + 1, 1)
        app.click("celula L%d C1" % (i + 1))
        app.wait_state(lambda s, i=i: s["layers"][i]["activeColumn"] == 0, "camada %d tocando" % (i + 1))
    cw, ch = app.state["canvas"]
    for i in range(4):
        expect(_tr(app, i)["identity"], "camada %d nasce com o Transform padrao (ocupa o canvas inteiro)" % (i + 1))

    app.click("##transformar_previa", "cmp_previa")
    app.wait_state(lambda s: s["transformEditor"]["preview"], "Transformar ligado na Previa")
    app.click("camada 1 nome")
    app.wait_state(lambda s: s["selection"]["kind"] == 1 and s["selection"]["layer"] == 0, "camada 1 selecionada")
    app.wait_frames(6)
    area = app.find("cmp_previa:imagem").rect
    k = area.w / float(cw)

    app.drag(area.x1, area.y1, area.cx + 2, area.cy + 2)
    tr = _tr(app, 0)
    expect(_close(tr["w"], cw / 2.0, 6.0 / k) and _close(tr["h"], ch / 2.0, 6.0 / k) and not tr["identity"],
           "arrastar a alca do canto reduziu a camada 1 (%s)" % tr)
    s = app.wait_state(lambda s: s["transformEditor"]["undo"] >= 1, "o gesto virou um passo do historico")
    n_undo = s["transformEditor"]["undo"]

    x0 = tr["x"]
    box_cx = area.x0 + (tr["x"] + tr["w"] / 2.0) * k
    box_cy = area.y0 + (tr["y"] + tr["h"] / 2.0) * k
    app.drag(box_cx, box_cy, box_cx + 60, box_cy + 30, mods=("ctrl",))
    tr = _tr(app, 0)
    expect(tr["x"] > x0 + 20, "arrastar o corpo moveu a camada (x %s -> %s)" % (x0, tr["x"]))

    app.click("camada 4 nome")
    app.wait_state(lambda s: s["selection"]["layer"] == 3, "camada 4 selecionada")
    app.drag(area.x1, area.y1, area.cx + 2, area.cy + 2)
    tr4 = _tr(app, 3)
    expect(not tr4["identity"], "a alca encolheu a camada 4 (%s)" % tr4)
    box_cx = area.x0 + (tr4["x"] + tr4["w"] / 2.0) * k
    box_cy = area.y0 + (tr4["y"] + tr4["h"] / 2.0) * k
    app.click_at(box_cx, box_cy, double=True)
    app.wait_state(lambda s: s["layers"][3]["transform"]["identity"], "duplo clique reseta o Transform da camada")
    expect(not _tr(app, 0)["identity"], "a camada 1 (coberta) nao foi mexida")
    app.click("camada 1 nome")
    app.wait_state(lambda s: s["selection"]["layer"] == 0, "camada 1 selecionada de novo")
    app.click("##transf_presets_previa", "cmp_previa")
    app.click("##tr_preset_0")
    app.wait_state(lambda s: s["layers"][0]["transform"]["identity"], "preset Tela cheia reseta a camada 1")

    app.click("##transf_presets_previa", "cmp_previa")
    app.click("##tr_preset_9")
    s = app.wait_state(lambda s: all(abs(s["layers"][i]["transform"]["w"] - cw / 2.0) < 1 for i in range(4)), "grade 2x2")
    quad = [(0, 0), (cw / 2.0, 0), (0, ch / 2.0), (cw / 2.0, ch / 2.0)]
    for i, (qx, qy) in enumerate(quad):
        tr = s["layers"][i]["transform"]
        expect(_close(tr["x"], qx, 1) and _close(tr["y"], qy, 1) and _close(tr["h"], ch / 2.0, 1),
               "camada %d no quadrante %d (%s)" % (i + 1, i + 1, tr))

    area = app.find("cmp_previa:imagem").rect
    app.click_at(area.x0 + area.w * 0.75, area.y0 + area.h * 0.25)
    app.wait_state(lambda s: s["selection"]["layer"] == 1, "clicar no quadrante superior direito seleciona a camada 2")
    app.click_at(area.x0 + area.w * 0.25, area.y0 + area.h * 0.75)
    app.wait_state(lambda s: s["selection"]["layer"] == 2, "clicar no quadrante inferior esquerdo seleciona a camada 3")
    app.click_at(area.x0 + area.w * 0.75, area.y0 + area.h * 0.25)
    app.wait_state(lambda s: s["selection"]["layer"] == 1, "volta para a camada 2")

    app.wait_frames(8)
    app.screenshot(os.path.join(ROOT, "docs", "screenshot-transform.png"))

    app.wait_state(lambda s: s["transformEditor"]["undo"] >= n_undo + 2, "passos do historico")
    app.key("z", "ctrl")
    s = app.wait_state(lambda s: s["layers"][1]["transform"]["identity"], "Ctrl+Z desfaz a grade inteira de uma vez")
    expect(all(s["layers"][i]["transform"]["identity"] for i in range(1, 4)), "Ctrl+Z desfez as 4 camadas")
    app.key("y", "ctrl")
    s = app.wait_state(lambda s: _close(s["layers"][1]["transform"]["w"], cw / 2.0, 1), "Ctrl+Y refaz a grade")
    expect(s["transformEditor"]["redo"] == 0, "nada mais a refazer")

    x1 = _tr(app, 1)["x"]
    app.key("right")
    expect(_close(_tr(app, 1)["x"], x1 + 1, 0.01), "seta direita move 1 px")
    app.key("right", "shift")
    expect(_close(_tr(app, 1)["x"], x1 + 11, 0.01), "Shift+seta move 10 px")
    app.key("left", "shift")
    app.key("left")
    expect(_close(_tr(app, 1)["x"], x1, 0.01), "as setas voltam ao lugar")

    app.click("##transf_recortar_previa", "cmp_previa")
    app.wait_state(lambda s: s["transformEditor"]["crop"], "modo Recortar ligado")
    tr = _tr(app, 1)
    left = area.x0 + tr["x"] * k
    mid_y = area.y0 + (tr["y"] + tr["h"] / 2.0) * k
    app.drag(left, mid_y, left + 40, mid_y)
    tr = _tr(app, 1)
    expect(tr["crop"][0] > 10 and tr["crop"][1] == 0 and _close(tr["w"], cw / 2.0, 1),
           "recorte da esquerda sem mudar o tamanho da camada (%s)" % tr)
    app.click("##transf_recortar_previa", "cmp_previa")
    app.wait_state(lambda s: not s["transformEditor"]["crop"], "modo Recortar desligado")

    pick_tab(app, "Camada")
    for lbl in ("##tr_x", "##tr_rot", "##tr_cl", "##tr_w"):
        scroll_into_view(app, lbl, "cmp_aba_camada")
    _type_number(app, "##tr_x", "cmp_aba_camada", 100)
    expect(_close(_tr(app, 1)["x"], 100, 0.01), "campo X da aba Camada move a camada (%s)" % _tr(app, 1))
    _type_number(app, "##tr_rot", "cmp_aba_camada", 20)
    expect(_close(_tr(app, 1)["rotation"], 20, 0.01), "campo Rotacao")
    _type_number(app, "##tr_cl", "cmp_aba_camada", 0)
    expect(_close(_tr(app, 1)["crop"][0], 0, 0.01), "campo Recorte")
    _type_number(app, "##tr_w", "cmp_aba_camada", 600)
    expect(_close(_tr(app, 1)["w"], 600, 0.01), "campo L")
    scroll_into_view(app, "Resetar", "cmp_aba_camada")
    app.click("Resetar", "cmp_aba_camada")
    app.wait_state(lambda s: s["layers"][1]["transform"]["identity"], "botao Resetar")

    app.click("##transformar_saida", "cmp_saida")
    app.wait_state(lambda s: s["transformEditor"]["output"], "Transformar ligado na Saida")
    out_area = app.find("cmp_saida:imagem").rect
    app.click_at(out_area.x0 + out_area.w * 0.25, out_area.y0 + out_area.h * 0.25)
    app.wait_state(lambda s: s["selection"]["layer"] == 1,
                   "clicar no canto superior esquerdo da Saida seleciona a camada de cima ali (a 2, de tela cheia, cobre a 1)")
    app.click_at(out_area.x0 + out_area.w * 0.75, out_area.y0 + out_area.h * 0.75)
    app.wait_state(lambda s: s["selection"]["layer"] == 3, "clicar no canto inferior direito seleciona a camada 4")
    app.click("##transformar_saida", "cmp_saida")
    app.wait_state(lambda s: not s["transformEditor"]["output"], "Transformar desligado na Saida")
    app.click("##transformar_previa", "cmp_previa")
    app.wait_state(lambda s: not s["transformEditor"]["preview"], "Transformar desligado na Previa")

    app.click("camada 1 nome")
    pick_combo(app, "##previa_fonte", "cmp_previa", "Camada selecionada")
    app.wait_state(lambda s: s["preview"]["mode"] == "layer" and s["layerViews"] >= 1, "a Previa pede a vista da camada")
    app.wait_frames(6)
    t.shot("transform_previa_camada")
    pick_combo(app, "##previa_fonte", "cmp_previa", "Clip selecionado")
    app.wait_state(lambda s: s["preview"]["mode"] == "clip" and s["layerViews"] == 0, "sem vista fora do modo Camada")

    app.click("##transformar_previa", "cmp_previa")
    app.click("##transf_presets_previa", "cmp_previa")
    app.click("##tr_preset_0")
    app.click("camada 1 nome")
    app.click("##transf_presets_previa", "cmp_previa")
    app.click("##tr_preset_0")
    app.click("##transformar_previa", "cmp_previa")
    for i in range(4):
        app.click("##camada_%d_parar" % (i + 1))


def sc_projeto_dialogo(t):
    """Opcional (--only projeto_dialogo): digita o caminho no dialogo NATIVO Salvar como. Depende do foco do teclado do
    Windows (estacao desbloqueada, nenhuma outra janela roubando o foco); por isso nao roda por padrao. Sempre fecha o
    dialogo no fim, mesmo quando falha."""
    app = t.app
    ensure_mode(app, "composition")
    expect("BSLSTAGE_QA_SAVE_PATH" not in os.environ, "o cenario opcional roda sem BSLSTAGE_QA_SAVE_PATH")
    target = os.path.join(app.dir, "proj", "dialogo.bslstage")
    os.makedirs(os.path.dirname(target), exist_ok=True)
    if os.path.exists(target):
        os.remove(target)
    try:
        app.focus()
        app.key("s", "ctrl", wait=False)
        dlg = app.find_dialog()
        expect(dlg != 0, "Ctrl+S abriu o dialogo Salvar como")
        time.sleep(0.5)
        driver_type(app, target)
        time.sleep(1.0)
        app.wait_state(lambda s: s["project"].lower().endswith("dialogo.bslstage"), "projeto salvo no caminho digitado", timeout=10)
        expect(os.path.exists(target), "arquivo .bslstage gravado")
    finally:
        close_stray_dialogs(app)


def sc_ver(t):
    app = t.app
    ensure_mode(app, "composition")
    for label, expected in (("100%", 1.0), ("150%", 1.5), ("90%", 0.9), ("200%", 2.0), ("125%", 1.25)):
        menu(app, "Exibir", "Tamanho da interface", label)
        app.wait_frames(6)
        expect(abs(app.scale - expected) < 0.01, "escala %s aplicada (%.2f)" % (label, app.scale))
        expect(app.state["scaleMode"] == "fixed", "modo fixo")
        t.layout_check("escala_" + label)
        t.shot("escala_" + label.replace("%", ""))
    menu(app, "Exibir", "Tamanho da interface", "Automático (escala do Windows)")
    app.wait_frames(6)
    expect(app.state["scaleMode"] == "system", "modo Automático")
    t.shot("escala_automatico")
    menu(app, "Exibir", "Tamanho da interface", "Acompanhar a janela (recomendado)")
    app.wait_frames(6)
    expect(app.state["scaleMode"] == "window", "modo Acompanhar a janela")
    t.shot("escala_janela")


def sc_ndi(t):
    """NDI e Spout: as fontes na aba Fontes, o painel Clip (lista de fontes, mesmo vazia, ou o aviso de runtime ausente),
    o destino NDI da Screen e a janela Compatibilidade de vídeo. Vale com e sem o runtime do NDI (--ndi-lib): sem ele
    confere o aviso "NDI Runtime não encontrado"; com ele faz o loopback (a fonte NDI recebendo a saída NDI do app)."""
    app = t.app
    ensure_mode(app, "composition")
    menu(app, "Exibir", "Layout", "Padrão (media server)")
    app.wait_frames(8)
    pick_tab(app, "Fontes")
    app.wait_frames(3)
    win = app.window_rect("cmp_geradores")
    if win:
        app.wheel(win.cx, win.cy, 20)
        app.wait_frames(3)
    ndi_item = scroll_into_view(app, "NDI", "cmp_geradores")
    expect(ndi_item is not None and app.has("Spout", "cmp_geradores"), "fontes NDI e Spout na aba Fontes")
    ndi = app.read()["state"]["ndi"]
    have_rt = ndi["available"]
    print("      runtime NDI: %s" % ("sim (%s)" % ndi["version"] if have_rt else "nao"))

    app.drag_items(scroll_into_view(app, "NDI", "cmp_geradores"), cell(app, 3, 3), steps=18)
    app.wait_state(lambda s: any(x[0] == 2 and x[1] == 2 and x[2].startswith("NDI") for x in s["cells"]), "gerador NDI arrastado")
    app.click("celula L3 C3 nome")
    app.wait_state(lambda s: s["selection"]["layer"] == 2 and s["selection"]["column"] == 2, "seleciona a fonte NDI")
    app.wait_frames(10)
    if have_rt:
        expect(app.has("##ndi_fonte", "cmp_clip"), "seletor de fontes NDI no painel Clip")
        app.click("##ndi_fonte", "cmp_clip")
        app.wait_frames(4)
        t.shot("ndi_lista")
        app.key("esc")
    else:
        expect(app.has("ndi:sem_runtime", "cmp_clip"), "aviso \"NDI Runtime não encontrado\" no painel Clip")
        expect(app.has("##ndi_link_copiar", "cmp_clip"), "botao do link do NDI")
    checks = app.find_all("##v", "cmp_clip")
    expect(len(checks) >= 2, "opcoes de baixa latencia e banda baixa no painel Clip (%d)" % len(checks))
    app.click_at(checks[0].rect.cx, checks[0].rect.cy)
    app.wait_state(lambda s: s["selClip"]["params"].get("lowLatency") is True, "baixa latencia ligada")
    app.read()
    checks = app.find_all("##v", "cmp_clip")
    app.click_at(checks[1].rect.cx, checks[1].rect.cy)
    app.wait_state(lambda s: s["selClip"]["params"].get("lowBandwidth") is True, "banda baixa ligada")
    t.shot("ndi_clip")
    app.click("celula L3 C3")
    app.wait_state(lambda s: s["layers"][2]["activeColumn"] == 2, "dispara a fonte NDI")
    app.wait_frames(20)
    st = app.read()["state"]["selClip"]["status"]
    if have_rt:
        expect(st != "", "status da fonte NDI (%r)" % st)
    else:
        expect(st == "NDI Runtime não encontrado", "status sem runtime (%r)" % st)
    app.click("##camada_3_parar")

    pick_tab(app, "Fontes")
    app.drag_items(scroll_into_view(app, "Spout", "cmp_geradores"), cell(app, 3, 4), steps=18)
    app.wait_state(lambda s: any(x[0] == 2 and x[1] == 3 and x[2].startswith("Spout") for x in s["cells"]), "gerador Spout arrastado")
    app.click("celula L3 C4 nome")
    app.wait_state(lambda s: s["selection"]["layer"] == 2 and s["selection"]["column"] == 3, "seleciona a fonte Spout")
    app.wait_frames(10)
    expect(app.has("##spout_sender", "cmp_clip"), "seletor de sender Spout no painel Clip")
    app.click("##spout_sender", "cmp_clip")
    app.wait_frames(4)
    app.key("esc")
    app.click("celula L3 C4")
    app.wait_state(lambda s: s["layers"][2]["activeColumn"] == 3, "dispara a fonte Spout")
    app.wait_frames(20)
    st = app.read()["state"]["selClip"]["status"]
    expect(st.startswith("Spout") or st.startswith("Sem sinal"), "status da fonte Spout (%r)" % st)
    t.shot("spout_clip")
    app.click("##camada_3_parar")

    menu(app, "Exibir", "##menu_compat")
    app.wait_frames(6)
    expect(app.has("compat:ndi_status", "janela_compat") and app.has("compat:spout_status", "janela_compat"),
           "estado do NDI e do Spout na janela de compatibilidade")
    if not have_rt:
        expect(app.has("ndi:sem_runtime", "janela_compat"), "aviso de runtime ausente na janela de compatibilidade")
    t.shot("compat")
    app.click("##compat_fechar")
    app.wait_frames(4)
    expect(not app.has("compat:ndi_status"), "janela de compatibilidade fechou")

    ensure_mode(app, "mapping")
    pick_combo(app, "##destino", "Propriedades", "NDI")
    app.wait_state(lambda s: s["screens"][s["mapping"]["screen"]]["kind"] == "NDI", "destino NDI escolhido")
    app.wait_frames(8)
    if have_rt:
        expect(app.has("##ndiFormato", "Propriedades") and app.has("##ndi", "Propriedades"), "campos do sender NDI (nome e formato)")
        app.click("##ndi", "Propriedades")
        clear_field_and_type(app, "BSL Stage QA")
        app.wait_state(lambda s: any(o["name"] == "BSL Stage QA" for o in s["ndi"]["outputs"]), "nome do sender NDI aplicado")
        pick_combo(app, "##ndiFormato", "Propriedades", "BGRA (cheio, com alpha)")
        app.wait_frames(4)
        pick_combo(app, "##ndiFormato", "Propriedades", "UYVY (leve)")
    else:
        expect(app.has("ndi:sem_runtime", "Propriedades"), "aviso de runtime ausente no destino NDI")
    t.shot("ndi_destino")

    if have_rt:
        app.click("##ndi", "Propriedades")
        clear_field_and_type(app, "BSL Stage QA Loop")
        ensure_mode(app, "composition")
        pick_tab(app, "Fontes")
        app.drag_items(scroll_into_view(app, "Padrão de teste", "cmp_geradores"), cell(app, 1, 1), steps=18)
        app.wait_state(lambda s: any(x[0] == 0 and x[1] == 0 for x in s["cells"]), "padrao de teste na grade")
        app.click("celula L1 C1")
        app.wait_state(lambda s: s["layers"][0]["activeColumn"] == 0, "padrao de teste na saida")
        app.click("celula L3 C3 nome")
        app.wait_state(lambda s: s["selection"]["layer"] == 2 and s["selection"]["column"] == 2, "seleciona a fonte NDI")
        s = app.wait_state(lambda s: any(n.endswith("(BSL Stage QA Loop)") for n in s["ndi"]["sources"]),
                           "a propria saida NDI aparece na lista de fontes", timeout=20)
        full = [n for n in s["ndi"]["sources"] if n.endswith("(BSL Stage QA Loop)")][0]
        app.click("##ndi_fonte", "cmp_clip")
        app.wait_frames(4)
        app.click(full, "Combo")
        app.wait_state(lambda s: s["selClip"]["params"].get("source") == full, "fonte NDI escolhida")
        app.click("celula L3 C3")
        app.wait_state(lambda s: s["layers"][2]["activeColumn"] == 2, "dispara a fonte NDI")
        s = app.wait_state(lambda s: s["selClip"]["status"].startswith("NDI:"), "a fonte NDI recebendo a propria saida", timeout=25)
        app.wait_state(lambda s: any(o["connections"] >= 1 for o in s["ndi"]["outputs"]), "a saida NDI tem um receptor", timeout=10)
        app.wait_frames(30)
        t.shot("ndi_loopback")
        app.click("##camada_1_parar")
        app.click("##camada_3_parar")

    ensure_mode(app, "mapping")
    pick_combo(app, "##destino", "Propriedades", "Desativado")
    app.wait_state(lambda s: s["screens"][s["mapping"]["screen"]]["kind"] == "Desativado", "destino desativado")
    ensure_mode(app, "composition")


def preview_state(app):
    return app.read()["state"]["preview"]


def drop_clip(app, name, layer, col):
    """Solta um arquivo de testmedia na celula (como o Explorer) e espera ela aparecer na grade."""
    for attempt in range(3):
        app.drop_files([os.path.join(MEDIA, name)], cell(app, layer, col))
        try:
            app.wait_state(lambda s: any(x[0] == layer - 1 and x[1] == col - 1 for x in s["cells"]), "%s em L%dC%d" % (name, layer, col))
            return
        except QaFailure:
            if attempt == 2:
                raise


def pick_combo(app, combo, window, item):
    app.click(combo, window)
    app.wait_frames(3)
    app.click(item, "Combo")
    app.wait_frames(3)


def sc_previa(t):
    """Previa e grade: o nome da celula so leva para a Previa; a imagem toca na saida."""
    app = t.app
    ensure_mode(app, "composition")
    menu(app, "Exibir", "Layout", "Padrão (media server)")
    app.wait_frames(8)
    drop_clip(app, "h264_1080p.mp4", 1, 1)
    drop_clip(app, "vp8.webm", 1, 2)
    drop_clip(app, "photo.jpg", 2, 1)
    expect(app.has("celula L1 C1 nome") and app.has("celula L1 C2 nome"), "a celula tem a area do nome")

    app.click("celula L1 C1")
    s = app.wait_state(lambda s: s["layers"][0]["activeColumn"] == 0, "clicar na imagem dispara o clip na camada")
    expect(s["selection"]["layer"] == 0 and s["selection"]["column"] == 0, "a celula clicada ficou selecionada")
    s = app.wait_state(lambda s: s["preview"]["live"] and not s["preview"]["own"], "Prévia espelha o clip ao vivo (sem 2a instância)")
    expect(s["preview"]["column"] == 0 and s["preview"]["mode"] == "clip", "Prévia mostra o clip selecionado")
    app.wait_frames(8)

    app.click("celula L1 C2 nome")
    s = app.wait_state(lambda s: s["selection"]["layer"] == 0 and s["selection"]["column"] == 1, "clicar no nome seleciona o clip")
    expect(s["layers"][0]["activeColumn"] == 0, "clicar no nome NÃO muda a coluna ativa da camada (%s)" % s["layers"][0]["activeColumn"])
    s = app.wait_state(lambda s: s["preview"]["own"] and not s["preview"]["live"], "Prévia abre o clip selecionado numa instância própria")
    expect(s["preview"]["column"] == 1 and s["preview"]["hasClip"], "Prévia carregou a coluna 2")
    expect(s["selClip"]["name"] == "vp8.webm" and not s["selClip"]["open"], "o painel Clip mostra o clip selecionado (parado na saída)")
    app.wait_frames(6)
    t.shot("previa_clip_diferente_do_ao_vivo")

    live_pos0 = app.state["layers"][0]["clipPosition"]
    app.click("##previa_tocar_pausar", "cmp_previa")
    s = app.wait_state(lambda s: not s["preview"]["playing"], "pausar a Prévia")
    expect(s["layers"][0]["clipPlaying"] and s["layers"][0]["activeColumn"] == 0, "pausar a Prévia não pausa a saída")
    time.sleep(0.6)
    app.wait_frames(4)
    s = app.state
    expect(s["layers"][0]["clipPlaying"], "a saída segue tocando depois de pausar a Prévia")
    expect(abs(s["layers"][0]["clipPosition"] - live_pos0) > 0.05, "a posição do clip ao vivo continua andando (%.2f -> %.2f)" % (live_pos0, s["layers"][0]["clipPosition"]))
    dur = s["preview"]["duration"]
    expect(dur > 0.5, "a Prévia conhece a duração do clip (%.2f)" % dur)
    app.click("##previa_posicao", "cmp_previa", dx=0.7)
    s = app.wait_state(lambda s: abs(s["preview"]["position"] - 0.7 * dur) < 0.25 * dur, "a barra de posição leva a Prévia a ~70%")
    expect(s["layers"][0]["activeColumn"] == 0 and s["layers"][0]["clipPlaying"], "mexer na posição da Prévia não mexe na saída")
    expect(not s["selClip"]["open"], "o clip da Prévia continua fora da saída")
    app.click("##previa_tocar_pausar", "cmp_previa")
    app.wait_state(lambda s: s["preview"]["playing"], "tocar a Prévia")

    expect(not app.find("##previa_ao_vivo", "cmp_previa").disabled, "Ao vivo habilitado para um clip fora da saída")
    app.click("##previa_ao_vivo", "cmp_previa")
    s = app.wait_state(lambda s: s["layers"][0]["activeColumn"] == 1, "Ao vivo dispara o clip da Prévia")
    s = app.wait_state(lambda s: s["preview"]["live"] and not s["preview"]["own"], "depois de Ao vivo a Prévia espelha a saída")
    expect(app.find("##previa_ao_vivo", "cmp_previa").disabled, "Ao vivo desabilitado quando o clip já está na saída")
    app.click("##previa_tocar_pausar", "cmp_previa")
    app.wait_state(lambda s: not s["layers"][0]["clipPlaying"], "Prévia ao vivo: pausar pausa a saída")
    app.click("##previa_tocar_pausar", "cmp_previa")
    app.wait_state(lambda s: s["layers"][0]["clipPlaying"], "Prévia ao vivo: tocar volta a tocar a saída")

    app.click("celula L2 C1 nome")
    s = app.wait_state(lambda s: s["selection"]["layer"] == 1 and s["selection"]["column"] == 0, "nome na camada 2 seleciona")
    expect(s["layers"][1]["activeColumn"] == -1, "a camada 2 continua parada")
    app.wait_state(lambda s: s["preview"]["hasClip"] and not s["preview"]["live"], "Prévia mostra a foto")

    app.click("camada 1 nome")
    app.wait_state(lambda s: s["selection"]["kind"] == 1 and s["selection"]["layer"] == 0, "selecionar a camada 1")
    pick_combo(app, "##previa_fonte", "cmp_previa", "Camada selecionada")
    s = app.wait_state(lambda s: s["preview"]["mode"] == "layer", "modo Camada selecionada")
    expect(s["preview"]["live"] and s["preview"]["layer"] == 0 and s["preview"]["column"] == 1, "a Prévia mostra o clip ao vivo da camada 1")
    expect(app.find("##previa_ao_vivo", "cmp_previa").disabled, "Ao vivo não faz sentido no modo Camada")
    app.wait_frames(6)
    t.shot("previa_camada")
    pick_combo(app, "##previa_fonte", "cmp_previa", "Clip selecionado")
    app.wait_state(lambda s: s["preview"]["mode"] == "clip", "volta ao modo Clip selecionado")

    app.click("##previa_quadriculado", "cmp_previa")
    app.wait_state(lambda s: not s["preview"]["checker"], "desligar o quadriculado")
    app.click("##previa_quadriculado", "cmp_previa")
    app.wait_state(lambda s: s["preview"]["checker"], "ligar o quadriculado")
    pick_combo(app, "##previaEncaixe", "cmp_previa", "Preencher")
    app.wait_state(lambda s: s["preview"]["fit"] == "fill", "encaixe Preencher")
    pick_combo(app, "##previaEncaixe", "cmp_previa", "Ajustar")
    app.wait_state(lambda s: s["preview"]["fit"] == "fit", "encaixe Ajustar")

    app.click("##camada_1_parar")
    app.wait_state(lambda s: s["layers"][0]["activeColumn"] == -1, "parar a camada 1")


def sc_layouts(t):
    app = t.app
    ensure_mode(app, "composition")
    for label, key in (("Grade grande", "bigGrid"), ("Saída grande", "bigOutput"), ("Padrão (media server)", "default")):
        menu(app, "Exibir", "Layout", label)
        app.wait_frames(8)
        expect(app.state["layout"] == key, "layout %s ativo" % key)
        expect(app.has("celula L1 C1"), "grade visível no layout %s" % key)
        t.shot("layout_" + key)
    menu(app, "Exibir", "Layout", "Grade grande")
    app.wait_frames(6)
    menu(app, "Exibir", "Layout", "##layout_restaurar")
    app.wait_frames(8)
    expect(app.state["layout"] == "bigGrid", "Restaurar layout mantem o layout escolhido")
    menu(app, "Exibir", "Layout", "Padrão (media server)")
    app.wait_frames(6)
    ensure_mode(app, "mapping")
    menu(app, "Exibir", "Layout", "Saída grande")
    ensure_mode(app, "composition")
    app.wait_frames(8)
    expect(app.state["layout"] == "bigOutput", "layout escolhido no Mapeamento aplicado ao voltar")
    menu(app, "Exibir", "Layout", "Padrão (media server)")
    app.wait_frames(6)
    layout_presets(t)


def rect_of(app, win):
    """Retangulo da janela do painel (a propria, nao as filhas "Painel/##filha")."""
    app.read()
    for it in app.items:
        if it.is_window and it.window.endswith(win):
            return it.rect
    raise QaFailure("janela nao encontrada: " + win)


def same_rect(a, b, tol=3.0):
    return all(abs(x - y) <= tol for x, y in ((a.x0, b.x0), (a.y0, b.y0), (a.x1, b.x1), (a.y1, b.y1)))


def fractions(app, win):
    """Retangulo do painel como fracao da janela do app (o que as proporcoes preservam)."""
    r = rect_of(app, win)
    ww, wh = app.snapshot["win"]
    return (r.x0 / ww, r.y0 / wh, r.x1 / ww, r.y1 / wh)


def save_layout_preset(app, name, include_scale=False):
    menu(app, "Exibir", "Layout", "##layout_salvar")
    app.wait_frames(4)
    expect(app.has("##layout_nome", "Salvar layout"), "a janela Salvar layout abriu")
    app.key("a", "ctrl")
    app.type_text(name)
    if include_scale:
        app.click("##layout_incluir_escala", "Salvar layout")
    app.click("##layout_salvar_ok", "Salvar layout")
    app.wait_state(lambda s: name in s["layoutPresets"] and s["lastLayoutPreset"] == name, "preset %r salvo" % name)
    app.wait_frames(4)
    expect(not app.has("##layout_nome"), "a janela Salvar layout fechou")


def layout_presets(t):
    """Presets de layout do usuario: salvar, mudar os paineis, aplicar de volta, Alt+1, proporcoes, gerenciar e excluir."""
    app = t.app
    expect(app.state["layoutPresets"] == [], "nenhum preset salvo no comeco (appdata isolado)")
    menu(app, "Exibir", "Layout", "Padrão (media server)")
    app.wait_frames(8)
    pick_combo(app, "##previa_fonte", "cmp_previa", "Camada selecionada")
    app.click("##previa_quadriculado", "cmp_previa")
    app.wait_state(lambda s: s["preview"]["mode"] == "layer" and not s["preview"]["checker"], "Prévia no modo Camada, sem quadriculado")
    ref = {w: rect_of(app, w) for w in ("cmp_grade", "cmp_saida", "cmp_previa", "cmp_clip")}
    ref_frac = {w: fractions(app, w) for w in ref}
    save_layout_preset(app, "Palco A")
    t.shot("layouts_menu_depois_de_salvar")
    app.click("Exibir", "MainMenuBar")
    app.wait_frames(4)
    it = app.find("Layout", "Menu_")
    app.hover(it.rect.cx, it.rect.cy)
    app.wait_frames(6)
    expect(app.has("Palco A", "Menu_") and app.has("##layout_gerenciar", "Menu_"), "o menu lista o preset e Gerenciar…")
    t.shot("layouts_menu")
    app.click("Exibir", "MainMenuBar")
    app.wait_frames(6)

    menu(app, "Exibir", "Layout", "Grade grande")
    app.wait_frames(8)
    pick_combo(app, "##previa_fonte", "cmp_previa", "Clip selecionado")
    app.click("##previa_quadriculado", "cmp_previa")
    app.wait_state(lambda s: s["preview"]["mode"] == "clip" and s["preview"]["checker"], "Prévia de volta ao padrão")
    moved = rect_of(app, "cmp_grade")
    expect(not same_rect(moved, ref["cmp_grade"], 20), "o layout Grade grande moveu o painel Composição (%s -> %s)" % (ref["cmp_grade"], moved))

    menu(app, "Exibir", "Layout", "Palco A")
    app.wait_frames(10)
    for w in ref:
        expect(same_rect(rect_of(app, w), ref[w], 4), "preset aplicado: painel %s voltou a %s (agora %s)" % (w, ref[w], rect_of(app, w)))
    s = app.state
    expect(s["preview"]["mode"] == "layer" and not s["preview"]["checker"], "preset aplicado: modo e opções da Prévia voltaram")
    expect(s["lastLayoutPreset"] == "Palco A", "o último preset usado é o Palco A")
    t.shot("layouts_preset_aplicado")

    menu(app, "Exibir", "Layout", "Saída grande")
    app.wait_frames(8)
    expect(not same_rect(rect_of(app, "cmp_grade"), ref["cmp_grade"], 20), "layout pronto diferente do preset")
    app.key("1", "alt")
    app.wait_frames(10)
    expect(same_rect(rect_of(app, "cmp_grade"), ref["cmp_grade"], 4), "Alt+1 aplicou o preset (painel Composição %s)" % rect_of(app, "cmp_grade"))
    expect(app.state["lastLayoutPreset"] == "Palco A", "Alt+1: último preset")
    t.shot("layouts_alt1")

    app.resize_client(1280, 720)
    menu(app, "Exibir", "Layout", "Grade grande")
    app.wait_frames(8)
    menu(app, "Exibir", "Layout", "Palco A")
    app.wait_frames(12)
    for w in ref_frac:
        got = fractions(app, w)
        expect(all(abs(a - b) < 0.02 for a, b in zip(got, ref_frac[w])),
               "janela 1280x720: painel %s manteve as proporções (%s -> %s)" % (w, tuple(round(v, 3) for v in ref_frac[w]), tuple(round(v, 3) for v in got)))
    t.shot("layouts_1280x720")
    app.resize_client(1600, 900)
    app.wait_frames(8)

    menu(app, "Exibir", "Layout", "##layout_gerenciar")
    app.wait_frames(5)
    expect(app.has("##layout_1_aplicar", "Gerenciar layouts"), "a janela Gerenciar layouts abriu com o preset")
    t.shot("layouts_gerenciar")
    app.click("##layout_1_renomear", "Gerenciar layouts")
    app.wait_frames(3)
    app.key("a", "ctrl")
    app.type_text("Palco B")
    app.key("enter")
    app.wait_state(lambda s: s["layoutPresets"] == ["Palco B"], "renomear o preset para Palco B")
    app.click("##layout_1_excluir", "Gerenciar layouts")
    app.wait_state(lambda s: s["layoutPresets"] == [], "excluir o preset")
    expect(app.state["lastLayoutPreset"] == "", "excluir o preset ativo limpa o último usado")
    app.click("##layout_fechar", "Gerenciar layouts")
    app.wait_frames(4)
    expect(not app.has("##layout_1_aplicar"), "Gerenciar layouts fechou")
    menu(app, "Exibir", "Layout", "Padrão (media server)")
    app.wait_frames(6)
    pick_combo(app, "##previa_fonte", "cmp_previa", "Clip selecionado")
    if not app.state["preview"]["checker"]:
        app.click("##previa_quadriculado", "cmp_previa")


def sc_janela(t):
    app = t.app
    ensure_mode(app, "composition")
    app.resize_client(1280, 720)
    t.shot("janela_1280x720")
    d = app.snapshot
    print("      1280x720: escala %.2f, fb %s" % (d["scale"], d["fb"]))
    t.layout_check("1280x720")
    app.maximize()
    expect(app.state["maximized"], "janela maximizada")
    t.shot("janela_maximizada")
    print("      maximizada: escala %.2f, fb %s" % (app.snapshot["scale"], app.snapshot["fb"]))
    for key in ("default", "bigGrid", "bigOutput"):
        label = {"default": "Padrão (media server)", "bigGrid": "Grade grande", "bigOutput": "Saída grande"}[key]
        menu(app, "Exibir", "Layout", label)
        app.wait_frames(8)
        t.layout_check("max_" + key)
        t.shot("maximizada_layout_" + key)
    for pos in ("window", "fixed", "system"):
        if pos == "window":
            menu(app, "Exibir", "Tamanho da interface", "Acompanhar a janela (recomendado)")
        elif pos == "fixed":
            menu(app, "Exibir", "Tamanho da interface", "150%")
        else:
            menu(app, "Exibir", "Tamanho da interface", "Automático (escala do Windows)")
        app.wait_frames(6)
        t.shot("maximizada_escala_" + pos)
    menu(app, "Exibir", "Tamanho da interface", "Acompanhar a janela (recomendado)")
    menu(app, "Exibir", "Layout", "Padrão (media server)")
    app.restore()
    t.shot("janela_restaurada")
    app.resize_client(1600, 900)
    s0 = app.scale
    app.drag_resize_border(-300, -160)
    app.wait_frames(6)
    print("      apos arrastar a borda: escala %.2f -> %.2f, fb %s" % (s0, app.scale, app.snapshot["fb"]))
    t.shot("janela_arrastada")
    app.resize_client(1600, 900)

KEY_ARG_DEFAULTS = {"selected": False, "deck": -1, "layer": 0, "column": 0, "index": 0, "value": 1.0, "fxScope": 1, "effect": 0}


def find_binding(s, action, key=None, scope="project", **args):
    """Atalho em state.keys.<scope> com a acao (e a tecla/argumentos pedidos); None se nao existe."""
    for b in s["keys"][scope]:
        a = b.get("args", {})
        if b["action"] != action or (key is not None and b["key"] != key):
            continue
        if all(a.get(k, KEY_ARG_DEFAULTS[k]) == v for k, v in args.items()):
            return b
    return None


def hold_toggles(app, key, getter, seconds):
    """Segura a tecla por alguns segundos e conta quantas vezes o valor lido do estado trocou."""
    values = []
    app.key_down(key)
    try:
        t0 = time.time()
        while time.time() - t0 < seconds:
            d = app.read()
            if d:
                values.append(bool(getter(d["state"])))
            time.sleep(0.01)
    finally:
        app.key_up(key)
    app.wait_frames(6)
    n = sum(1 for a, b in zip(values, values[1:]) if a != b)
    print("      %s segurado %.1f s: %d trocas em %d leituras" % (key.upper(), seconds, n, len(values)))
    return n


def sc_atalhos(t):
    """Atalhos de teclado: atribuir tecla pelo botao direito (celula e camada), disparar, ligar/desligar camada, padroes
    (colunas, Q..P, Espaco, Delete, B segurando), nao disparar digitando, editor (gravar, conflito, modo, remover,
    restaurar). Gera docs/screenshot-atalhos.png."""
    app = t.app
    ensure_mode(app, "composition")
    for name, l, c in (("photo.jpg", 1, 1), ("alpha.png", 1, 2), ("anim.gif", 2, 1), ("static.webp", 2, 2)):
        if not any(x[0] == l - 1 and x[1] == c - 1 for x in app.read()["state"]["cells"]):
            drop_clip(app, name, l, c)
    app.click("##master_limpar", "cmp_grade")
    app.wait_state(lambda s: all(l["activeColumn"] == -1 for l in s["layers"]), "tudo parado")
    s = app.read()["state"]
    expect(len(s["keys"]["project"]) == 23 and len(s["keys"]["app"]) == 15, "padroes: 23 do projeto e 15 do app (F11 = tela cheia)")

    app.click("celula L1 C2", button="right")
    app.wait_frames(3)
    app.click("##key_atribuir")
    app.wait_state(lambda s: s["keys"]["capturing"], "gravacao de tecla aberta")
    expect(app.has("##key_cancelar_gravar", "Gravar atalho"), "aviso 'Aperte a tecla' na tela")
    t.shot("atalhos_gravando")
    app.key("j")
    app.wait_state(lambda s: find_binding(s, "clip.trigger", "J", deck=0, layer=0, column=1) is not None, "J atribuida a L1 C2")
    expect(not app.state["keys"]["capturing"], "gravacao terminou")
    app.key("j")
    app.wait_state(lambda s: s["layers"][0]["activeColumn"] == 1 and s["keys"]["lastAction"] == "clip.trigger"
                   and s["keys"]["lastKey"] == "J", "J dispara o clip da L1 C2")

    app.click("camada 1 nome", button="right")
    app.wait_frames(3)
    expect(app.has("##key_atribuir"), "menu da camada tem 'Atribuir tecla...'")
    app.click("##key_atribuir")
    app.wait_state(lambda s: s["keys"]["capturing"], "gravacao para a camada")
    app.key("k")
    app.wait_state(lambda s: find_binding(s, "layer.power", "K", layer=0) is not None, "K liga/desliga a camada 1")
    expect(not app.state["layers"][0]["bypass"], "camada 1 ligada")
    app.key("k")
    app.wait_state(lambda s: s["layers"][0]["bypass"], "K desliga a camada (a tela de sobrepor some)")
    app.key("k")
    app.wait_state(lambda s: not s["layers"][0]["bypass"], "K liga a camada de novo")

    app.click("coluna 3", button="right")
    app.wait_frames(3)
    app.click("##key_atribuir")
    app.wait_state(lambda s: s["keys"]["capturing"], "gravacao para a coluna")
    app.key("f3")
    app.wait_state(lambda s: find_binding(s, "column.trigger", "F3", column=2) is not None, "F3 dispara a coluna 3")

    app.click("camada 1 nome")
    app.wait_state(lambda s: s["selection"]["kind"] == 1 and s["selection"]["layer"] == 0, "camada 1 selecionada")
    app.key("q")
    app.wait_state(lambda s: s["layers"][0]["activeColumn"] == 0, "Q dispara a celula 1 da camada selecionada")
    app.key("2")
    app.wait_state(lambda s: s["layers"][0]["activeColumn"] == 1 and s["layers"][1]["activeColumn"] == 1, "tecla 2 dispara a coluna 2")
    app.key("1")
    app.wait_state(lambda s: s["layers"][0]["activeColumn"] == 0 and s["layers"][1]["activeColumn"] == 0, "tecla 1 dispara a coluna 1")
    app.key("space")
    app.wait_state(lambda s: s["paused"], "Espaco pausa")
    app.key("space")
    app.wait_state(lambda s: not s["paused"], "Espaco volta a tocar")
    app.key_down("b")
    try:
        app.wait_state(lambda s: s["blackout"] and s["keys"]["held"] == 1, "B segurando liga o blackout")
    finally:
        app.key_up("b")
    app.wait_state(lambda s: not s["blackout"] and s["keys"]["held"] == 0, "soltar o B desliga o blackout")
    app.key("delete")
    app.wait_state(lambda s: s["layers"][0]["activeColumn"] == -1 and s["layers"][1]["activeColumn"] == 0,
                   "Delete limpa so a camada selecionada")
    app.key("j")
    app.wait_state(lambda s: s["layers"][0]["activeColumn"] == 1, "J dispara de novo")

    tabs = sorted(app.find_all("tab:Composição", "dock-tabs"), key=lambda i: i.rect.y0)
    app.click_at(tabs[-1].rect.cx, tabs[-1].rect.cy)
    app.wait_frames(3)
    add = scroll_into_view(app, "##master_efeito_adicionar", "cmp_aba_composicao")
    app.click_at(add.rect.cx, add.rect.cy)
    app.wait_frames(4)
    app.type_text("Vinheta")
    app.wait_frames(3)
    app.click("##add_native:vignette", "Popup_")
    app.wait_state(lambda s: [e["type"] for e in s["masterEffects"]] == ["native:vignette"], "efeito no master")
    eye = scroll_into_view(app, "##master_efeito_1_bypass", "cmp_aba_composicao")
    app.click_at(eye.rect.cx, eye.rect.cy, button="right")
    app.wait_frames(3)
    app.click("##key_atribuir")
    app.wait_state(lambda s: s["keys"]["capturing"], "gravacao para o efeito")
    app.key("v")
    app.wait_state(lambda s: find_binding(s, "effect.bypass", "V", fxScope=2, effect=0) is not None, "V liga/desliga o efeito do master")
    app.key("v")
    app.wait_state(lambda s: s["masterEffects"][0]["bypass"], "V põe o efeito em bypass")
    app.key("v")
    app.wait_state(lambda s: not s["masterEffects"][0]["bypass"], "V liga o efeito de novo")
    t.shot("atalhos_efeito")
    mm = scroll_into_view(app, "##master_efeito_1_menu", "cmp_aba_composicao")
    app.click_at(mm.rect.cx, mm.rect.cy)
    app.wait_frames(3)
    app.click("##efeito_remover", "Popup_")
    app.wait_state(lambda s: len(s["masterEffects"]) == 0, "efeito do master removido")

    menu(app, "Exibir", "##menu_atalhos")
    app.wait_state(lambda s: s["keys"]["editorOpen"], "editor de atalhos aberto")
    app.wait_frames(6)
    expect(app.has("##atalhos_busca", "Atalhos") and app.has("##atalho_1_gravar"), "editor com busca e linhas")
    rows = app.state["keys"]["rows"]
    expect(rows >= 20, "editor lista os atalhos (%d)" % rows)
    for _ in range(25):
        app.read()
        if any(it.visible and "Ligar/desligar camada" in it.label for it in app.items if not it.is_window):
            break
        r = app.find("##atalhos_busca", "Atalhos").rect
        app.wheel(r.cx, r.y1 + 150, -2)
    app.wait_frames(4)
    t.shot("atalhos_editor")
    app.screenshot(os.path.join(ROOT, "docs", "screenshot-atalhos.png"))
    r = app.find("##atalhos_busca", "Atalhos").rect
    app.wheel(r.cx, r.y1 + 150, 60)
    app.wait_frames(3)

    fired = app.state["keys"]["fired"]
    cols = [l["activeColumn"] for l in app.state["layers"]]
    app.click("##atalhos_busca", "Atalhos")
    app.wait_frames(3)
    app.key("q")
    app.key("w")
    app.key("1")
    app.key("space")
    app.key("delete")
    app.wait_frames(4)
    s = app.state
    expect(s["keys"]["fired"] == fired and [l["activeColumn"] for l in s["layers"]] == cols and not s["paused"],
           "digitar no campo de texto nao dispara atalhos de show")
    app.key("a", "ctrl")
    app.key("backspace")
    app.type_text("ligar")
    app.wait_state(lambda s: 1 <= s["keys"]["rows"] <= 3, "busca por 'ligar' filtra a lista")

    app.click("##atalho_1_gravar")
    app.wait_state(lambda s: s["keys"]["capturing"], "Gravar da linha")
    app.key("esc")
    app.wait_state(lambda s: not s["keys"]["capturing"], "Esc cancela a gravacao")
    expect(find_binding(app.state, "layer.power", "K", layer=0) is not None, "Esc nao mudou a tecla")
    app.click("##atalho_1_gravar")
    app.wait_state(lambda s: s["keys"]["capturing"], "Gravar de novo")
    app.key("l")
    app.wait_state(lambda s: find_binding(s, "layer.power", "L", layer=0) is not None, "linha regravada com L")
    app.click("##atalho_1_gravar")
    app.wait_state(lambda s: s["keys"]["capturing"], "Gravar para conflito")
    app.key("q")
    app.wait_state(lambda s: s["keys"]["conflictOpen"], "aviso de conflito")
    t.shot("atalhos_conflito")
    app.click("##key_conflito_cancelar")
    app.wait_state(lambda s: not s["keys"]["conflictOpen"], "cancelar o conflito")
    expect(find_binding(app.state, "layer.power", "L", layer=0) is not None, "cancelar mantem a tecla")
    app.click("##atalho_1_gravar")
    app.wait_state(lambda s: s["keys"]["capturing"], "Gravar para substituir")
    app.key("q")
    app.wait_state(lambda s: s["keys"]["conflictOpen"], "aviso de conflito de novo")
    app.click("##key_conflito_substituir")
    app.wait_state(lambda s: find_binding(s, "layer.power", "Q", layer=0) is not None, "substituir da a tecla ao novo")
    expect(find_binding(app.state, "clip.trigger", "Q", selected=True) is None, "o antigo ficou sem a tecla")
    app.click("##atalho_1_modo")
    app.wait_frames(3)
    app.click("Segurar", "Combo")
    app.wait_state(lambda s: find_binding(s, "layer.power", "Q", layer=0)["mode"] == "hold", "modo Segurar")
    app.click("##atalho_1_global")
    app.wait_state(lambda s: find_binding(s, "layer.power", "Q", layer=0).get("global") is True, "global ligado")
    app.click("##atalho_1_global")
    app.wait_state(lambda s: not find_binding(s, "layer.power", "Q", layer=0).get("global"), "global desligado")
    n = len(app.state["keys"]["project"])
    app.click("##atalho_1_remover")
    app.wait_state(lambda s: len(s["keys"]["project"]) == n - 1 and find_binding(s, "layer.power") is None, "remover o atalho")

    app.click("##atalhos_busca", "Atalhos")
    app.key("a", "ctrl")
    app.key("backspace")
    app.wait_state(lambda s: s["keys"]["rows"] >= 20, "busca limpa")
    app.click("##atalhos_strobo")
    app.wait_frames(4)
    expect(app.has("##strobo_gravar") and app.has("##strobo_tela"), "janela Novo strobo aberta")
    t.shot("atalhos_strobo")
    app.click("##strobo_32")
    app.click("##strobo_gravar")
    app.wait_state(lambda s: s["keys"]["capturing"], "strobo pede a tecla")
    app.key("h")
    app.wait_state(lambda s: (find_binding(s, "master.blackout", "H") or {}).get("mode") == "repeat", "H vira strobo do blackout")
    expect(find_binding(app.state, "master.blackout", "H").get("rate") == 8, "strobo em 1/32")
    expect((find_binding(app.state, "master.blackout", "B") or {}).get("mode") == "hold", "o B (blackout ao segurar) continua igual")
    app.click("##atalhos_busca", "Atalhos")
    app.wait_frames(2)
    app.click("##atalhos_fechar")
    app.wait_state(lambda s: not s["keys"]["editorOpen"], "editor fechado para o strobo")
    expect(hold_toggles(app, "h", lambda s: s["blackout"], 1.5) >= 6, "segurar o H pisca o blackout")
    app.wait_state(lambda s: not s["blackout"] and s["keys"]["held"] == 0, "soltar o H volta sem blackout")

    app.click("camada 1 nome", button="right")
    app.wait_frames(3)
    expect(app.has("##key_strobo"), "menu da camada tem 'Atribuir tecla de strobo'")
    app.click("##key_strobo")
    app.wait_state(lambda s: s["keys"]["capturing"], "gravacao do strobo da camada")
    app.key("g")
    app.wait_state(lambda s: (find_binding(s, "layer.power", "G") or {}).get("mode") == "repeat", "G vira strobo da camada 1")
    expect(hold_toggles(app, "g", lambda s: s["layers"][0]["bypass"], 1.5) >= 4, "segurar o G pisca a camada 1")
    app.wait_state(lambda s: not s["layers"][0]["bypass"] and s["keys"]["held"] == 0, "soltar o G deixa a camada ligada")

    menu(app, "Exibir", "##menu_atalhos")
    app.wait_state(lambda s: s["keys"]["editorOpen"], "editor de atalhos aberto de novo")
    app.wait_frames(4)
    n = len(app.state["keys"]["project"])
    app.click("##atalhos_novo")
    app.wait_state(lambda s: s["keys"]["capturing"], "Novo atalho pede a tecla")
    app.key("n")
    app.wait_state(lambda s: len(s["keys"]["project"]) == n + 1 and find_binding(s, "clip.trigger", "N") is not None,
                   "N gravada no novo atalho")
    app.wait_frames(4)
    expect(app.has("##atalho_acao"), "depois da tecla abre a escolha da ação")
    t.shot("atalhos_novo_acao")
    app.click("##atalhos_busca", "Atalhos")
    app.wait_frames(3)

    app.click("##atalhos_restaurar")
    app.wait_frames(4)
    app.click("##atalhos_restaurar_ok")
    app.wait_state(lambda s: len(s["keys"]["project"]) == 23 and len(s["keys"]["app"]) == 15
                   and find_binding(s, "clip.trigger", "Q", selected=True) is not None
                   and find_binding(s, "layer.power") is None, "restaurar padroes")
    app.click("##atalhos_fechar")
    app.wait_state(lambda s: not s["keys"]["editorOpen"], "editor fechado")

    app.click("##master_limpar", "cmp_grade")
    app.wait_state(lambda s: all(l["activeColumn"] == -1 for l in s["layers"]), "tudo parado no fim")


def _artnet_packet(port_address, values, seq=1):
    """ArtDmx (Art-Net 4) de 512 canais: values = {canal (1-based): valor}."""
    import struct
    data = bytearray(512)
    for ch, v in values.items():
        data[ch - 1] = v
    return (b"Art-Net\x00" + struct.pack("<H", 0x5000) + struct.pack(">H", 14) + bytes([seq & 0xFF, 0, port_address & 0xFF, (port_address >> 8) & 0x7F])
            + struct.pack(">H", 512) + bytes(data))


def sc_dmx(t):
    """DMX / Art-Net: janela (menu Saída), receptor desligado por padrão, ligar só no loopback (nada abre na rede), patch
    automático, dados Art-Net reais por UDP em 127.0.0.1 (opacidade pelo patch), indicador na barra de status e DMX learn
    pelo botão direito (Atribuir DMX... e mexer o fader). Gera docs/qa/<config>_dmx_*.png."""
    import socket
    app = t.app
    ensure_mode(app, "composition")
    menu(app, "Saída", "##menu_dmx")
    app.wait_state(lambda s: s["dmx"]["windowOpen"], "janela DMX / Art-Net aberta", timeout=4)
    expect(app.has("##dmx_aba_monitor") and app.has("##dmx_aba_patch") and app.has("##dmx_aba_rede"), "abas Monitor, Patch e Rede")
    expect(not app.state["dmx"]["enabled"] and not app.state["dmx"]["running"], "receptor desligado por padrão (nenhuma porta aberta)")
    expect(app.has("##dmx_indicador"), "indicador DMX na barra de status")

    app.click("##dmx_aba_rede")
    app.wait_frames(4)
    app.click("##dmx_placa")
    app.wait_frames(3)
    app.click("##dmx_placa_127.0.0.1")
    app.wait_frames(3)
    app.click("##dmx_ligar")
    app.wait_state(lambda s: s["dmx"]["running"], "receptor ligado no loopback (porta 6454 livre?)", timeout=6)

    app.click("##dmx_aba_patch")
    app.wait_frames(4)
    layers = len(app.state["layers"])
    app.click("##dmx_auto")
    app.wait_state(lambda s: s["dmx"]["blocks"] == layers + 1, "patch automático (%d camadas + master)" % layers, timeout=4)
    t.shot("dmx_patch")

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        master_ch = 4 * layers + 1
        sock.sendto(_artnet_packet(0, {1: 255, 5: 128, master_ch: 255}, 1), ("127.0.0.1", 6454))
        app.wait_state(lambda s: s["dmx"]["universes"] and s["dmx"]["universes"][0]["active"], "universo 1 recebendo", timeout=4)
        sock.sendto(_artnet_packet(0, {1: 100, 5: 128, master_ch: 255}, 2), ("127.0.0.1", 6454))
        app.wait_state(lambda s: abs(s["layers"][0]["opacity"] - 100 / 255) < 0.01, "opacidade da camada 1 = 39% pelo canal 1", timeout=4)
        expect(abs(app.state["layers"][1]["opacity"] - 128 / 255) < 0.01, "camada 2 = 50% pelo canal 5")
        app.click("##dmx_aba_monitor")
        app.wait_frames(6)
        t.shot("dmx_monitor")
        expect(app.state["dmx"]["universes"][0]["packets"] >= 2, "contagem de pacotes do universo 1")

        app.key("esc")
        app.wait_state(lambda s: not s["dmx"]["windowOpen"], "janela DMX fechada com Esc", timeout=3)
        last = layers
        app.click("##camada_%d_opacidade" % last, button="right")
        app.wait_frames(4)
        app.click("##dmx_atribuir")
        app.wait_state(lambda s: s["dmx"]["learning"], "DMX learn aceso (Mexa o fader na mesa)", timeout=4)
        sock.sendto(_artnet_packet(0, {1: 100, 5: 128, master_ch: 255, 40: 200}, 3), ("127.0.0.1", 6454))
        app.wait_state(lambda s: not s["dmx"]["learning"] and s["dmx"]["maps"] == 1, "canal 40 aprendido", timeout=4)
        sock.sendto(_artnet_packet(0, {1: 100, 5: 128, master_ch: 255, 40: 64}, 4), ("127.0.0.1", 6454))
        app.wait_state(lambda s: abs(s["layers"][last - 1]["opacity"] - 64 / 255) < 0.01, "o canal aprendido mexe na opacidade", timeout=4)
    finally:
        sock.close()

    menu(app, "Saída", "##menu_dmx")
    app.wait_state(lambda s: s["dmx"]["windowOpen"], "janela DMX reaberta", timeout=4)
    app.click("##dmx_aba_rede")
    app.wait_frames(4)
    app.click("##dmx_ligar")
    app.wait_state(lambda s: not s["dmx"]["running"], "receptor desligado", timeout=4)
    app.key("esc")
    app.wait_state(lambda s: not s["dmx"]["windowOpen"], "janela DMX fechada", timeout=3)


def _beat(app):
    app.read()
    return app.state["beat"]


def sc_minimizado(t):
    app = t.app
    ensure_mode(app, "composition")
    app.wait_frames(10)
    b0 = _beat(app)
    t0 = time.time()
    time.sleep(2.0)
    b1 = _beat(app)
    tb1 = time.time()
    visible_rate = (b1 - b0) / (tb1 - t0)
    expect(visible_rate > 0.1, "o relogio da composicao anda com a janela visivel (%.2f batidas/s)" % visible_rate)
    driver.user32.ShowWindow(app.hwnd, 6)
    try:
        time.sleep(0.5)
        expect(driver.user32.IsIconic(app.hwnd), "a janela principal minimizou")
        time.sleep(3.0)
    finally:
        driver.user32.ShowWindow(app.hwnd, 9)
    app.ensure_focus()
    app.wait_frames(4)
    b2 = _beat(app)
    rate = (b2 - b1) / (time.time() - tb1)
    expect(rate >= visible_rate * 0.7,
           "a composicao continua rodando minimizada (%.2f batidas/s minimizada x %.2f visivel)" % (rate, visible_rate))
    expect(app.alive(), "o app segue vivo depois de restaurar")


def sc_segundo_plano(t):
    app = t.app
    ensure_mode(app, "composition")
    app.wait_frames(10)
    dirty0 = app.state["dirty"]
    frame0 = app.read()["frame"]
    other = driver.ForeignWindow()
    try:
        expect(other.activate(), "a janela de outro programa recebeu o foco")
        expect(driver.user32.GetForegroundWindow() != app.hwnd, "o app ficou em segundo plano")
        worst, hung_run, hung_max, pings, failed = 0.0, 0, 0, 0, 0
        t_end = time.time() + 8.0
        next_ping = 0.0
        while time.time() < t_end:
            for ch in "texto em outro programa 123 ":
                if not other.is_foreground():
                    expect(other.activate(), "a janela de teste recuperou o foco durante a digitação")
                driver._key(0, unicode_char=ch)
                driver._key(0, up=True, unicode_char=ch)
                other.pump()
                time.sleep(0.03)
                if time.time() >= next_ping:
                    ok, ms, stuck = driver.ping_window(app.hwnd)
                    pings += 1
                    worst = max(worst, ms if ok else 1e6)
                    failed += 0 if ok else 1
                    hung_run = hung_run + 1 if stuck else 0
                    hung_max = max(hung_max, hung_run)
                    next_ping = time.time() + 0.25
        typed = len(other.text())
        expect(typed > 50, "o texto foi para a outra janela (%d caracteres)" % typed)
        expect(pings > 10, "o app foi sondado durante a digitação (%d vezes)" % pings)
        expect(failed == 0 and hung_max < 4, "o Windows não marcou o app como travado em segundo plano (%d sem resposta, %d marcas seguidas)" % (failed, hung_max))
        expect(worst < 1000.0, "o app respondeu em menos de 1 s em segundo plano (pior %.0f ms)" % worst)
        frames = app.read()["frame"] - frame0
        expect(frames > 60, "o app continuou desenhando em segundo plano (%d quadros em 8 s)" % frames)
        print("      segundo plano: %d sondagens, pior resposta %.0f ms, %d quadros" % (pings, worst, frames))
    finally:
        other.close()
        app.focus()
    app.wait_frames(5)
    expect(app.state["dirty"] == dirty0, "as teclas digitadas em outro programa não chegaram ao app")


def sc_fuzz(t):
    """Cliques aleatorios (semente fixa) em itens visiveis, nos dois modos: procura crash/assert/conflito."""
    import random
    app = t.app
    rnd = random.Random(1234)
    skip = ("Sair", "Abrir…", "Salvar", "Salvar como…", "Novo", "Abrir recente", "Limpar tudo", "Excluir", "Remover",
            "##caminho", "##map_remover", "##master_limpar",
            "Gerar padrão HD", "Exportar", "Importar CSV", "Escolher imagem")
    for i in range(160):
        if i % 40 == 0 and app.has("Fechar"):
            app.click("Fechar")
        if i % 40 == 0:
            ensure_mode(app, ("composition", "mapping", "pixelmap")[(i // 40) % 3])
        app.wait_frames(3)
        cands = [it for it in app.items if not it.is_window and it.visible and it.label and it.rect.w > 4 and it.rect.h > 4
                 and not it.label.startswith(skip) and "##menu_" not in it.label and "Debug" not in it.window
                 and "isf_pasta_adicionar" not in it.label and "isf_abrir_principal" not in it.label
                 and "ndi_link_abrir" not in it.label
                 and "##atalhos_exportar" not in it.label and "##atalhos_importar" not in it.label
                 and "##exibir_" not in it.label
                 and "DockSpace" not in it.window]
        if not cands:
            app.key("esc")
            continue
        it = rnd.choice(cands)
        button = "right" if rnd.random() < 0.12 else "left"
        app.click_at(it.rect.x0 + it.rect.w * rnd.uniform(0.2, 0.8), it.rect.y0 + it.rect.h * rnd.uniform(0.2, 0.8),
                     button=button, double=rnd.random() < 0.1)
        if rnd.random() < 0.3:
            app.key("esc")
        if i % 10 == 0:
            app.wait_frames(3)
    app.key("esc")
    if app.has("Fechar"):
        app.click("Fechar")
    if app.read()["state"]["legal"]["windowOpen"]:
        app.click("##legal_fechar")
        app.wait_state(lambda s: not s["legal"]["windowOpen"], "janela Licenças e avisos fechada", timeout=3)
    ensure_mode(app, "composition")
    t.shot("depois_do_fuzz")


def sc_fechar(t):
    """Fechar a janela com alteracoes: o modal aparece; Cancelar mantem o app; Nao salvar encerra."""
    app = t.app
    ensure_mode(app, "composition")
    menu(app, "Composição", "+ Coluna")
    app.wait_state(lambda s: s["dirty"], "projeto modificado")
    driver.user32.PostMessageW(app.hwnd, 0x0010, 0, 0)
    app.wait_frames(4)
    expect(app.has("Não salvar"), "fechar com alterações abre o modal")
    app.click("Cancelar", "Salvar")
    app.wait_frames(3)
    expect(app.alive() and not app.has("Não salvar"), "Cancelar mantem o app aberto")
    driver.user32.PostMessageW(app.hwnd, 0x0010, 0, 0)
    app.wait_frames(4)
    it = app.find("Não salvar", "Salvar")
    app.click_at(it.rect.cx, it.rect.cy)
    for _ in range(80):
        if not app.alive():
            break
        time.sleep(0.1)
    expect(not app.alive(), "Não salvar encerra o app")
    t.closed = True


def exibir_view(app):
    return app.read()["state"]["view"]


def exibir_panel(app, key):
    return next(p for p in exibir_view(app)["panels"] if p["key"] == key)


def exibir_abs_rect(app, key):
    """Retangulo do painel em coordenadas da tela (o snapshot traz relativo a janela principal)."""
    v = exibir_view(app)
    p = next(p for p in v["panels"] if p["key"] == key)
    x, y, w, h = p["rect"]
    return (x + v["mainPos"][0], y + v["mainPos"][1], w, h)


def os_window(app, title_prefix):
    """HWND de uma janela do sistema do processo do app cujo titulo comeca com o texto (as janelas soltas do ImGui)."""
    import ctypes
    from ctypes import wintypes
    found = []

    @ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    def cb(hwnd, _):
        pid = wintypes.DWORD()
        driver.user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
        if pid.value == app.proc.pid and driver.user32.IsWindowVisible(hwnd):
            buf = ctypes.create_unicode_buffer(128)
            driver.user32.GetWindowTextW(hwnd, buf, 128)
            if buf.value.startswith(title_prefix):
                found.append(hwnd)
        return True

    driver.user32.EnumWindows(cb, 0)
    return found[0] if found else 0


def sc_exibir(t):
    """Menu Exibir: ocultar/mostrar painel (menu e X da aba), soltar em janela e encaixar de volta, trava do layout (os
    splitters nao andam), Tela cheia (F11), preset de layout com painel oculto (salvar, aplicar, excluir) e Restaurar padrao."""
    app = t.app
    ensure_mode(app, "composition")
    menu(app, "Exibir", "Layout", "Padrão (media server)")
    app.wait_frames(10)
    v = exibir_view(app)
    expect(v["hidden"] == [] and v["detached"] == [] and not v["locked"], "estado inicial do menu Exibir limpo: %s" % v["hidden"])
    ref = {w: rect_of(app, w) for w in ("cmp_saida", "cmp_previa", "cmp_grade", "cmp_clip")}

    menu(app, "Exibir", "##exibir_previa")
    app.wait_state(lambda s: s["view"]["hidden"] == ["previa"], "Prévia oculta")
    app.wait_frames(8)
    expect(not app.has("##previa_fonte"), "o conteúdo da Prévia sumiu")
    expect(not exibir_panel(app, "previa")["visible"], "estado: Prévia não visível")
    expect(rect_of(app, "cmp_saida").h > ref["cmp_saida"].h * 1.5, "a Saída ocupou o lugar da Prévia (%s)" % rect_of(app, "cmp_saida"))
    t.shot("exibir_sem_previa")
    menu(app, "Exibir", "##exibir_previa")
    app.wait_state(lambda s: s["view"]["hidden"] == [], "Prévia visível de novo")
    app.wait_frames(8)
    expect(app.has("##previa_fonte"), "a Prévia voltou")
    for w in ref:
        expect(same_rect(rect_of(app, w), ref[w], 6), "ao mostrar de novo, %s volta ao lugar (%s -> %s)" % (w, ref[w], rect_of(app, w)))

    app.read()
    tab = app.find("tabx:Clip", "dock-tabs")
    app.hover(tab.rect.cx, tab.rect.cy)
    app.wait_frames(3)
    app.click_at(tab.rect.cx, tab.rect.cy)
    app.wait_state(lambda s: "clip" in s["view"]["hidden"], "o X da aba Clip oculta o painel")
    app.wait_frames(6)
    expect(not app.has("##clip_tocar_pausar"), "painel Clip fechado pelo X")
    menu(app, "Exibir", "##exibir_clip")
    app.wait_state(lambda s: "clip" not in s["view"]["hidden"], "Clip mostrado pelo menu")
    app.wait_frames(6)

    menu(app, "Exibir", "##exibir_abas", "##exibir_efeitos")
    app.wait_state(lambda s: "efeitos" in s["view"]["hidden"], "aba Efeitos oculta")
    app.wait_frames(6)
    expect(not any(i.label == "tab:Efeitos" for i in app.items), "a aba Efeitos sumiu da barra de abas")
    t.shot("exibir_sem_efeitos")

    menu(app, "Exibir", "Layout", "##layout_salvar")
    app.wait_frames(4)
    app.key("a", "ctrl")
    app.type_text("Sem Efeitos")
    app.click("##layout_salvar_ok", "Salvar layout")
    app.wait_state(lambda s: s["layoutPresets"] == ["Sem Efeitos"], "preset Sem Efeitos salvo")
    menu(app, "Exibir", "##exibir_mostrar_todos")
    app.wait_state(lambda s: s["view"]["hidden"] == [], "Mostrar todos os painéis")
    app.wait_frames(6)
    expect(any(i.label == "tab:Efeitos" for i in app.items), "a aba Efeitos voltou")
    menu(app, "Exibir", "Layout", "Sem Efeitos")
    app.wait_state(lambda s: s["view"]["hidden"] == ["efeitos"], "o preset restaurou o painel oculto")
    app.wait_frames(6)
    menu(app, "Exibir", "Layout", "##layout_excluir", "##layout_excluir_1")
    app.wait_frames(4)
    expect(app.has("##layout_excluir_ok", "Excluir layout"), "a exclusão pede confirmação")
    app.click("##layout_excluir_cancelar", "Excluir layout")
    app.wait_frames(4)
    expect(app.state["layoutPresets"] == ["Sem Efeitos"], "Cancelar mantém o layout")
    menu(app, "Exibir", "Layout", "##layout_excluir", "##layout_excluir_1")
    app.wait_frames(4)
    app.click("##layout_excluir_ok", "Excluir layout")
    app.wait_state(lambda s: s["layoutPresets"] == [], "layout excluído")
    menu(app, "Exibir", "##exibir_mostrar_todos")
    app.wait_state(lambda s: s["view"]["hidden"] == [], "tudo visível de novo")
    app.wait_frames(6)

    before = exibir_abs_rect(app, "saida")
    menu(app, "Exibir", "##exibir_soltar", "##exibir_soltar_saida")
    app.wait_state(lambda s: s["view"]["detached"] == ["saida"] and s["view"]["viewports"] >= 2, "Saída solta em janela do sistema")
    app.wait_frames(10)
    expect(exibir_panel(app, "saida")["detached"] and not exibir_panel(app, "saida")["docked"], "a Saída não está mais encaixada")
    hwnd = 0
    for _ in range(20):
        hwnd = os_window(app, "Saída")
        if hwnd:
            break
        app.wait_frames(3)
    expect(hwnd != 0, "existe uma janela do sistema chamada Saída")
    expect(rect_of(app, "cmp_previa").h > ref["cmp_previa"].h * 1.5, "a Prévia ocupou o lugar da Saída")
    ox, oy = app.client_origin()
    driver.user32.SetWindowPos(hwnd, 0, ox + 1000, oy + 420, 0, 0, 0x0001 | 0x0004)
    app.wait_frames(12)
    moved = exibir_abs_rect(app, "saida")
    expect(abs(moved[0] - before[0]) > 200, "a posição da janela solta acompanhou o arraste (%s -> %s)" % (before, moved))
    t.shot("exibir_saida_solta")
    expect(exibir_panel(app, "saida")["seen"] and moved[2] > 100 and moved[3] > 100, "a janela solta desenha o painel (%s)" % (moved,))
    menu(app, "Exibir", "##exibir_soltar", "##exibir_soltar_saida")
    app.wait_state(lambda s: s["view"]["detached"] == [], "Saída encaixada de novo")
    app.wait_frames(12)
    expect(exibir_panel(app, "saida")["docked"] and exibir_view(app)["viewports"] == 1, "de volta ao dock, sem janela extra")
    for w in ref:
        expect(same_rect(rect_of(app, w), ref[w], 8), "depois de encaixar, %s no lugar de antes (%s -> %s)" % (w, ref[w], rect_of(app, w)))

    def drag_splitter():
        a, b = rect_of(app, "cmp_saida"), rect_of(app, "cmp_grade")
        x = (a.x1 + b.x0) / 2
        y = (a.y0 + a.y1) / 2
        app.drag(x, y, x + 120, y, steps=12)
        app.wait_frames(8)
        return rect_of(app, "cmp_saida")

    menu(app, "Exibir", "##exibir_bloquear")
    app.wait_state(lambda s: s["view"]["locked"], "layout bloqueado")
    app.wait_frames(6)
    locked_rect = drag_splitter()
    expect(same_rect(locked_rect, ref["cmp_saida"], 4), "com o layout bloqueado o splitter não anda (%s -> %s)" % (ref["cmp_saida"], locked_rect))
    menu(app, "Exibir", "##exibir_bloquear")
    app.wait_state(lambda s: not s["view"]["locked"], "layout desbloqueado")
    app.wait_frames(6)
    free_rect = drag_splitter()
    expect(free_rect.w > ref["cmp_saida"].w + 60, "destravado, o splitter redimensiona (%s -> %s)" % (ref["cmp_saida"], free_rect))
    t.shot("exibir_redimensionado")

    w0, h0 = app.client_size()
    app.key("f11")
    app.wait_state(lambda s: s["view"]["fullscreen"], "F11 liga a tela cheia")
    app.wait_frames(10)
    wf, hf = app.client_size()
    expect(wf >= w0 and hf > h0 + 30, "a janela ocupa o monitor (%dx%d -> %dx%d)" % (w0, h0, wf, hf))
    t.shot("exibir_tela_cheia")
    app.key("f11")
    app.wait_state(lambda s: not s["view"]["fullscreen"], "F11 desliga a tela cheia")
    app.wait_frames(10)
    expect(app.client_size() == (w0, h0), "a janela volta ao tamanho de antes (%s)" % (app.client_size(),))

    menu(app, "Exibir", "##exibir_previa")
    menu(app, "Exibir", "##exibir_soltar", "##exibir_soltar_clip")
    app.wait_state(lambda s: s["view"]["detached"] == ["clip"] and s["view"]["hidden"] == ["previa"], "Prévia oculta e Clip solto")
    menu(app, "Exibir", "Layout", "##layout_restaurar")
    app.wait_state(lambda s: s["view"]["detached"] == [] and s["view"]["hidden"] == [], "Restaurar padrão encaixa e mostra tudo")
    app.wait_frames(12)
    expect(exibir_view(app)["viewports"] == 1, "nenhuma janela solta sobrou")
    for w in ref:
        expect(same_rect(rect_of(app, w), ref[w], 8), "Restaurar padrão: %s no lugar do layout pronto (%s -> %s)" % (w, ref[w], rect_of(app, w)))


def _fit_virtual_screen(x, y, w, h):
    """O retangulo (x, y, w, h) tem a barra de titulo dentro de algum ponto da area virtual da tela?"""
    import ctypes
    u = ctypes.windll.user32
    vx, vy = u.GetSystemMetrics(76), u.GetSystemMetrics(77)
    vw, vh = u.GetSystemMetrics(78), u.GetSystemMetrics(79)
    px, py = x + min(w / 2, 100), y + 10
    return vx <= px < vx + vw and vy <= py < vy + vh


def sc_exibir_reinicio(t):
    """Salva e restaura: painel oculto, painel solto em janela, layout travado e o tamanho dos paineis voltam depois de fechar
    e abrir o app de novo (mesmo appdata). Uma janela solta que ficou fora de todos os monitores e trazida de volta."""
    qa_dir = os.path.join(ROOT, "build", "qa", "exibir_reinicio")
    extra = ["--ui-mode", "window"]
    app = driver.App(EXE, qa_dir, extra, size=(1600, 900))
    try:
        ensure_mode(app, "composition")
        menu(app, "Exibir", "Layout", "Grade grande")
        app.wait_frames(8)
        menu(app, "Exibir", "##exibir_abas", "##exibir_fontes")
        app.wait_state(lambda s: s["view"]["hidden"] == ["fontes"], "aba Fontes oculta")
        menu(app, "Exibir", "##exibir_soltar", "##exibir_soltar_previa")
        app.wait_state(lambda s: s["view"]["detached"] == ["previa"], "Prévia solta")
        app.wait_frames(10)
        hwnd = os_window(app, "Prévia")
        ox, oy = app.client_origin()
        driver.user32.SetWindowPos(hwnd, 0, ox + 900, oy + 400, 0, 0, 0x0001 | 0x0004)
        app.wait_frames(10)
        menu(app, "Exibir", "##exibir_bloquear")
        app.wait_state(lambda s: s["view"]["locked"], "layout bloqueado")
        app.wait_frames(10)
        rects = {w: rect_of(app, w) for w in ("cmp_saida", "cmp_grade", "cmp_clip")}
        prev_abs = exibir_abs_rect(app, "previa")
        app.wait_frames(5)
        time.sleep(1.0)
    finally:
        app.quit()
    ini = open(os.path.join(qa_dir, "appdata", "imgui.ini"), encoding="utf-8").read()
    expect("cmp_previa" in ini and "ViewportPos" in ini, "o imgui.ini guarda a janela solta")
    settings = open(os.path.join(qa_dir, "appdata", "settings.json"), encoding="utf-8").read()
    expect('"fontes"' in settings and '"locked": true' in settings and '"previa"' in settings, "o settings.json guarda ocultos, solto e trava")

    app = driver.App(EXE, qa_dir, extra, size=(1600, 900), keep=True)
    try:
        app.wait_frames(20)
        v = exibir_view(app)
        expect(v["hidden"] == ["fontes"] and v["detached"] == ["previa"] and v["locked"], "tudo restaurado ao abrir: %s" % (v,))
        expect(v["viewports"] >= 2 and exibir_panel(app, "previa")["detached"], "a Prévia reabriu em janela solta")
        expect(not any(i.label == "tab:Fontes" for i in app.items), "a aba Fontes continua oculta")
        for w, r in rects.items():
            expect(same_rect(rect_of(app, w), r, 6), "tamanho restaurado: %s (%s -> %s)" % (w, r, rect_of(app, w)))
        now = exibir_abs_rect(app, "previa")
        expect(abs(now[0] - prev_abs[0]) < 30 and abs(now[1] - prev_abs[1]) < 30, "posição da janela solta restaurada (%s -> %s)" % (prev_abs, now))
    finally:
        app.quit()

    ini_path = os.path.join(qa_dir, "appdata", "imgui.ini")
    ini = open(ini_path, encoding="utf-8").read()
    import re
    ini = re.sub(r"ViewportPos=-?\d+,-?\d+", "ViewportPos=30000,30000", ini)
    open(ini_path, "w", encoding="utf-8").write(ini)
    app = driver.App(EXE, qa_dir, extra, size=(1600, 900), keep=True)
    try:
        app.wait_frames(150)
        x, y, w, h = exibir_abs_rect(app, "previa")
        expect(_fit_virtual_screen(x, y, w, h), "janela solta fora dos monitores foi trazida de volta (%s)" % ((x, y, w, h),))
    finally:
        app.quit()


def sc_atualizacao(t):
    """Atualizações e reporte (item 10): menu Ajuda, janela de atualizações com as preferências (verificar sozinho ligado, beta
    desligado), nenhum aviso na barra sem versão nova, e a janela de reporte (título, descrição, caixas desmarcadas por padrão,
    URL do issue com URL-encoding e rótulo certo, Copiar). No --qa o transporte é falso (nenhuma rede) e o navegador não abre."""
    app = t.app
    ensure_mode(app, "composition")
    u = app.state["update"]
    expect(u["autoCheck"] and not u["beta"] and u["skipped"] == "", "padrões: verificar sozinho ligado, beta desligado, nada pulado")
    expect(not u["notice"] and not u["hasUpdate"], "sem versão nova não há aviso na barra de status")

    menu(app, "Ajuda", "##menu_atualizar")
    app.wait_state(lambda s: s["update"]["windowOpen"], "janela Atualizações aberta", timeout=4)
    app.wait_state(lambda s: not s["update"]["checking"] and s["update"]["lastCheck"] > 0, "verificação manual terminou", timeout=8)
    expect(app.has("##atualizacao_auto") and app.has("##atualizacao_beta") and app.has("##atualizacao_procurar"), "preferências na janela")
    t.shot("atualizacao_janela")
    app.click("##atualizacao_beta")
    app.wait_state(lambda s: s["update"]["beta"], "Receber versões beta ligado")
    app.click("##atualizacao_beta")
    app.wait_state(lambda s: not s["update"]["beta"], "Receber versões beta desligado")
    app.click("##atualizacao_auto")
    app.wait_state(lambda s: not s["update"]["autoCheck"], "verificar sozinho desligado")
    app.click("##atualizacao_auto")
    app.wait_state(lambda s: s["update"]["autoCheck"], "verificar sozinho ligado de novo")
    app.key("esc")
    app.wait_state(lambda s: not s["update"]["windowOpen"], "Esc fecha a janela de atualizações", timeout=3)

    menu(app, "Ajuda", "##menu_reportar")
    app.wait_state(lambda s: s["report"]["open"] and s["report"]["kind"] == 0, "janela Reportar problema aberta", timeout=4)
    expect(not app.state["report"]["system"] and not app.state["report"]["log"], "caixas de sistema e log desmarcadas por padrão")
    app.click("##reportar_titulo")
    app.type_text("Tela preta no show")
    app.click("##reportar_descricao")
    app.type_text("A saida ficou preta depois de 1h.")
    app.click("##reportar_abrir")
    app.wait_state(lambda s: s["report"]["lastUrl"] != "", "URL do issue montada", timeout=4)
    url = app.state["report"]["lastUrl"]
    expect(url.startswith("https://github.com/") and "/issues/new?title=Tela%20preta%20no%20show&body=" in url, "URL com título codificado: " + url[:120])
    expect(url.endswith("&labels=bug"), "rótulo bug")
    expect("Informa" not in url and "GPU" not in url, "sem sistema/log o texto não leva essas seções")
    short = app.state["report"]["bodyLength"]
    app.click("##reportar_sistema")
    app.click("##reportar_log")
    app.wait_state(lambda s: s["report"]["system"] and s["report"]["log"], "caixas marcadas")
    app.wait_frames(4)
    expect(app.state["report"]["bodyLength"] > short + 100, "a prévia cresceu com as seções opcionais")
    t.shot("atualizacao_reportar")
    app.click("##reportar_abrir")
    app.wait_frames(4)
    url = app.state["report"]["lastUrl"]
    expect("Informa%C3%A7%C3%B5es%20do%20sistema" in url and "GPU" in url and "Windows" in url, "com a caixa marcada, o sistema vai no texto")
    expect("%0A" in url and " " not in url and "\n" not in url, "quebras de linha codificadas")
    app.click("##reportar_copiar")
    app.wait_state(lambda s: s["report"]["lastClipboard"].startswith("Tela preta no show"), "Copiar copia o texto", timeout=3)
    app.click("##reportar_fechar")
    app.wait_state(lambda s: not s["report"]["open"], "Fechar fecha a janela", timeout=3)

    menu(app, "Ajuda", "##menu_sugerir")
    app.wait_state(lambda s: s["report"]["open"] and s["report"]["kind"] == 1, "janela Sugerir melhoria aberta", timeout=4)
    app.click("##reportar_titulo")
    app.type_text("Atalho para blackout")
    app.click("##reportar_abrir")
    app.wait_state(lambda s: s["report"]["lastUrl"].endswith("&labels=enhancement"), "rótulo enhancement", timeout=4)
    app.key("esc")
    app.wait_state(lambda s: not s["report"]["open"], "Esc fecha a janela de reporte", timeout=3)


def sc_licencas(t):
    """Avisos legais (item 5): Ajuda > Licenças e avisos… (abas Licença, Terceiros, Privacidade e Termos), o texto completo da GPL, a lista de
    terceiros lida de THIRD-PARTY-NOTICES.md com o texto de uma licença expandido, Ajuda > Sobre com o link do código-fonte. O navegador nunca é aberto no --qa (state.legal.lastUrl)."""
    app = t.app
    ensure_mode(app, "composition")
    lg = app.state["legal"]
    expect(lg["noticeVersion"] >= 1 and not lg["windowOpen"], "estado inicial: janela fechada")

    menu(app, "Ajuda", "##menu_licencas")
    app.wait_state(lambda s: s["legal"]["windowOpen"] and s["legal"]["tab"] == 0, "janela Licenças e avisos aberta na aba Licença", timeout=4)
    for tab in ("licenca", "terceiros", "privacidade", "termos"):
        expect(app.has("##legal_aba_" + tab), "aba " + tab)
    expect(app.has("##legal_codigo") and app.has("##legal_texto_completo") and app.has("##legal_fechar"), "botões da aba Licença")
    app.click("##legal_texto_completo")
    app.wait_state(lambda s: s["legal"]["licenseFull"], "texto completo da GPL aberto", timeout=3)
    t.shot("licencas_gpl")
    app.click("##legal_texto_completo")
    app.wait_state(lambda s: not s["legal"]["licenseFull"], "texto completo da GPL fechado", timeout=3)
    app.click("##legal_codigo")
    repo = app.state["update"]["repo"]
    app.wait_state(lambda s: s["legal"]["lastUrl"] == "https://github.com/" + repo, "link do código-fonte (sem abrir o navegador no --qa)", timeout=3)

    app.click("##legal_aba_terceiros")
    app.wait_state(lambda s: s["legal"]["tab"] == 1, "aba Terceiros", timeout=3)
    st = app.state["legal"]
    expect(st["docsFound"] and st["thirdParty"] >= 25, "lista de terceiros lida do THIRD-PARTY-NOTICES.md: %s" % (st,))
    app.click("##tp_0")
    app.wait_frames(4)
    app.click("##tp_0_0")
    app.wait_state(lambda s: s["legal"]["openedTexts"] == 1, "texto da licença expandido", timeout=3)
    t.shot("licencas_terceiros")
    app.click("##tp_0_0")
    app.wait_state(lambda s: s["legal"]["openedTexts"] == 0, "texto da licença recolhido", timeout=3)

    app.click("##legal_aba_privacidade")
    app.wait_state(lambda s: s["legal"]["tab"] == 2, "aba Privacidade", timeout=3)
    app.wait_frames(3)
    t.shot("licencas_privacidade")
    app.click("##legal_aba_termos")
    app.wait_state(lambda s: s["legal"]["tab"] == 3, "aba Termos", timeout=3)
    app.wait_frames(3)
    t.shot("licencas_termos")
    app.key("esc")
    app.wait_state(lambda s: not s["legal"]["windowOpen"], "Esc fecha a janela", timeout=3)

    menu(app, "Ajuda", "Sobre")
    app.wait_frames(4)
    expect(app.has("##sobre_codigo") and app.has("##sobre_licencas"), "Sobre tem o link do código-fonte e o atalho das licenças")
    t.shot("licencas_sobre")
    app.click("##sobre_codigo")
    app.wait_state(lambda s: s["legal"]["lastUrl"] == "https://github.com/" + repo, "código-fonte pelo Sobre", timeout=3)
    app.click("##sobre_licencas")
    app.wait_state(lambda s: s["legal"]["windowOpen"] and s["legal"]["tab"] == 0, "Sobre abre as licenças", timeout=4)
    app.click("##legal_fechar")
    app.wait_state(lambda s: not s["legal"]["windowOpen"], "janela fechada pelo botão", timeout=3)


def make_qa_package(folder):
    import zipfile
    os.makedirs(folder, exist_ok=True)
    pkg = os.path.join(folder, "pacote-qa.bslplugin")
    manifest = '{"name": "Pacote QA", "author": "Ana", "version": "1.0.0", "license": "CC BY 4.0", "description": "Pacote de teste da interface."}'
    shader = ('/*{ "DESCRIPTION": "Teste", "CATEGORIES": ["Meus plugins"], "INPUTS": [ { "NAME": "inputImage", "TYPE": "image" } ] }*/\n'
              'void main(){ gl_FragColor = IMG_THIS_NORM_PIXEL(inputImage); }\n')
    with zipfile.ZipFile(pkg, "w", zipfile.ZIP_STORED) as z:
        z.writestr("manifest.json", manifest)
        z.writestr("Pacote QA.fs", shader)
    return pkg


def sc_plugins(t):
    app = t.app
    ensure_mode(app, "composition")
    pick_tab(app, "Efeitos")
    app.wait_state(lambda s: s["isf"]["entries"] >= 4 and not s["isf"]["scanning"], "shaders ISF varridos", timeout=10)
    expect(app.has("##plugin_novo", "cmp_efeitos") and app.has("##plugin_importar", "cmp_efeitos"),
           "botoes Novo plugin e Importar na aba Efeitos")

    menu(app, "Arquivo", "##menu_novo_plugin")
    app.wait_state(lambda s: s["plugins"]["wizardOpen"], "assistente Novo plugin aberto", timeout=4)
    for ident in ("##plug_nome", "##plug_tipo_efeito", "##plug_tipo_gerador", "##plug_categoria", "##plug_modelo_0",
                  "##plug_adicionar_param", "##plug_criar", "##plug_cancelar"):
        expect(app.has(ident), "controle " + ident)
    app.click("##plug_nome")
    app.key("a", "ctrl")
    app.type_text("Plugin QA")
    app.click("##plug_modelo_1")
    app.wait_state(lambda s: s["plugins"]["rows"] == 6, "modelo Cor/ajuste traz 6 parametros", timeout=3)
    app.click("##plug_adicionar_param")
    app.wait_state(lambda s: s["plugins"]["rows"] == 7, "parametro novo adicionado", timeout=3)
    t.shot("plugins_assistente")
    app.click("##plug_criar")
    app.wait_state(lambda s: s["plugins"]["created"], "plugin criado", timeout=5)
    st = app.state["plugins"]
    expect(os.path.isfile(st["createdFile"]) and st["createdFile"].endswith("Plugin QA.fs"), "arquivo criado: %s" % st["createdFile"])
    text = open(st["createdFile"], encoding="utf-8").read()
    expect('"NAME": "inputImage"' in text and '"NAME": "brightness"' in text and "\r" not in text, "o .fs tem o cabecalho esperado")
    expect(st["createdId"] == "isf:Plugin QA.fs", "id do plugin: %r" % st["createdId"])
    t.shot("plugins_criado")
    app.click("##plug_abrir_editor")
    app.wait_state(lambda s: s["plugins"]["lastAction"] == "editor" and s["plugins"]["lastPath"].endswith("Plugin QA.fs"), "Abrir no editor", timeout=3)
    app.click("##plug_fechar")
    app.wait_state(lambda s: not s["plugins"]["wizardOpen"], "assistente fechado", timeout=3)

    app.wait_state(lambda s: not s["isf"]["scanning"], "varredura depois de criar", timeout=10)
    item = search_effect(app, "Plugin QA")
    expect(item is not None, "o plugin novo aparece na aba Efeitos")
    app.click("Plugin QA", "cmp_efeitos", button="right")
    app.wait_frames(3)
    app.click("##isf_ctx_pasta", "Popup_")
    app.wait_state(lambda s: s["plugins"]["lastAction"] == "pasta", "Mostrar na pasta", timeout=3)

    app.click("Plugin QA", "cmp_efeitos", button="right")
    app.wait_frames(3)
    app.click("##isf_ctx_duplicar", "Popup_")
    app.wait_state(lambda s: s["plugins"]["lastAction"] == "duplicar" and s["plugins"]["lastPath"].endswith(".fs"), "Duplicar", timeout=3)
    copy = app.state["plugins"]["lastPath"]
    expect(os.path.isfile(copy) and open(copy, encoding="utf-8").read() == text, "a copia e igual ao original: %s" % copy)

    app.click("Plugin QA", "cmp_efeitos", button="right")
    app.wait_frames(3)
    app.click("##isf_ctx_exportar", "Popup_")
    app.wait_state(lambda s: s["plugins"]["exportOpen"], "janela Exportar", timeout=3)
    app.click("##plug_exp_exportar")
    app.wait_state(lambda s: s["plugins"]["exportedFile"] != "", "pacote exportado", timeout=4)
    out = app.state["plugins"]["exportedFile"]
    import zipfile
    with zipfile.ZipFile(out) as z:
        names = sorted(z.namelist())
        expect("manifest.json" in names and "Plugin QA.fs" in names, "pacote com manifest e shader: %s" % names)
        expect('"license"' in z.read("manifest.json").decode("utf-8"), "manifest com licenca")
    t.shot("plugins_exportar")
    app.click("##plug_exp_fechar")
    app.wait_state(lambda s: not s["plugins"]["exportOpen"], "janela Exportar fechada", timeout=3)

    menu(app, "Arquivo", "##menu_importar_plugin")
    app.wait_state(lambda s: s["plugins"]["importOpen"] and s["plugins"]["importValid"], "pacote conferido", timeout=5)
    st = app.state["plugins"]
    expect(st["importName"] == "Pacote QA" and st["importLicense"] == "CC BY 4.0" and st["importAuthor"] == "Ana", "nome, autor e licenca do pacote: %s" % (st,))
    t.shot("plugins_importar")
    app.click("##plug_importar_instalar")
    app.wait_state(lambda s: s["plugins"]["installedFolder"] != "", "pacote instalado", timeout=5)
    folder = app.state["plugins"]["installedFolder"]
    expect(os.path.isfile(os.path.join(folder, "Pacote QA.fs")) and os.path.isfile(os.path.join(folder, "manifest.json")), "arquivos instalados em " + folder)
    app.click("##plug_importar_fechar")
    app.wait_state(lambda s: not s["plugins"]["importOpen"], "janela Importar fechada", timeout=3)
    app.wait_state(lambda s: not s["isf"]["scanning"], "varredura depois de importar", timeout=10)
    expect(search_effect(app, "Pacote QA") is not None, "o plugin importado aparece na aba Efeitos")
    app.click("##efeitos_busca", "cmp_efeitos")
    app.key("a", "ctrl")
    app.key("backspace")


def sc_stream(t):
    """Stream de rede: a fonte na aba Fontes, a janela "Stream de rede" (abas Abrir link e Receber transmissão, campo da URL
    mascarado com botão de mostrar, endereços para copiar), o estado no painel Clip (URL inválida, servidor fora) e o atalho
    da Internet (.url) solto na grade. Nunca dispara o modo Receber (abriria a porta na rede e o Windows pediria o firewall).
    Gera docs/screenshot-stream.png."""
    app = t.app
    ensure_mode(app, "composition")
    menu(app, "Exibir", "Layout", "Padrão (media server)")
    app.wait_frames(8)
    pick_tab(app, "Fontes")
    app.wait_frames(3)

    app.drag_items(scroll_into_view(app, "Stream de rede", "cmp_geradores"), cell(app, 2, 2), steps=18)
    app.wait_state(lambda s: any(x[0] == 1 and x[1] == 1 and x[2].startswith("Stream") for x in s["cells"]), "gerador Stream de rede arrastado")
    app.wait_frames(8)
    expect(app.has("##stream_url", "janela_stream"), "janela Stream de rede aberta ao soltar a fonte")
    expect(app.has("##stream_aba_link", "janela_stream") and app.has("##stream_aba_receber", "janela_stream"), "abas Abrir link e Receber transmissão")
    expect(app.has("##stream_exemplo_1", "janela_stream"), "exemplos de endereço")

    app.click("##stream_url", "janela_stream")
    app.type_text("foo")
    app.wait_state(lambda s: s["selClip"]["params"].get("url") == "foo", "URL digitada chega ao clip")
    expect(app.has("##stream_mostrar", "janela_stream"), "botão mostrar/ocultar a URL")
    app.click("##stream_mostrar", "janela_stream")
    app.wait_frames(4)
    app.click("##stream_conectar", "janela_stream")
    app.wait_state(lambda s: s["selClip"]["open"] and s["selClip"]["status"].startswith("Erro:"), "URL inválida dá erro claro", timeout=5)
    expect("inválido" in app.state["selClip"]["status"], "mensagem em português (%r)" % app.state["selClip"]["status"])
    t.shot("stream_erro")

    app.click("##stream_url", "janela_stream")
    app.key("a", "ctrl")
    app.type_text("rtmp://127.0.0.1:1/live/chave")
    app.wait_state(lambda s: s["selClip"]["params"].get("url") == "rtmp://127.0.0.1:1/live/chave", "URL trocada")
    app.wait_state(lambda s: s["selClip"]["status"].startswith(("Sem sinal", "Conectando")), "servidor fora: sem sinal ou conectando", timeout=15)
    app.wait_frames(6)
    t.shot("stream_sem_sinal")

    app.click("##master_limpar")
    app.wait_state(lambda s: s["layers"][1]["activeColumn"] == -1, "para o clip antes de trocar para Receber (nunca abre a porta na rede)")
    app.click("##stream_aba_receber", "janela_stream")
    app.wait_state(lambda s: s["selClip"]["params"].get("mode") == "listen", "aba Receber troca o modo")
    app.wait_frames(6)
    expect(app.has("##stream_porta", "janela_stream") and app.has("##stream_nome", "janela_stream"), "porta e nome")
    expect(app.has("##stream_copiar_1", "janela_stream") and app.has("##stream_endereco_1", "janela_stream"), "endereço para copiar")
    app.click("##stream_copiar_1", "janela_stream")
    app.wait_frames(4)
    app.hover(5, 5)
    app.wait_frames(4)
    app.screenshot(os.path.join(ROOT, "docs", "screenshot-stream.png"))
    app.click("##stream_fechar", "janela_stream")
    app.wait_frames(4)
    expect(not app.has("##stream_url", "janela_stream"), "janela fechou")

    base = os.path.join(ROOT, "build", "qa", "stream")
    os.makedirs(base, exist_ok=True)
    shortcut = os.path.join(base, "camera.url")
    with open(shortcut, "w", encoding="utf-8") as f:
        f.write("[InternetShortcut]\r\nURL=rtsp://127.0.0.1:9/stream1\r\n")
    app.drop_files([shortcut], cell(app, 3, 2))
    app.wait_state(lambda s: any(x[0] == 2 and x[1] == 1 and x[2] == "Stream: 127.0.0.1:9" for x in s["cells"]), "atalho .url solto cria o clip de stream")


def _sq(app):
    return app.read()["state"]["sequences"]


def _seq_slices(app):
    app.read()
    found = [it for it in app.items if it.label.startswith("seq:slice:") and it.visible]
    return sorted(found, key=lambda i: int(i.label.split(":")[2]))


def _seq_hits(app, x0, y0, x1, y1):
    xa, xb = min(x0, x1), max(x0, x1)
    ya, yb = min(y0, y1), max(y0, y1)
    return sum(1 for it in _seq_slices(app) if it.rect.x0 <= xb and it.rect.x1 >= xa and it.rect.y0 <= yb and it.rect.y1 >= ya)


def _seq_lit(app, index, step=0):
    return _sq(app)["list"][index]["lit"][step]


def _seq_pick(app, label, window, item):
    app.click(label, window)
    app.wait_frames(3)
    app.click(item, "Combo")
    app.wait_frames(3)


def sc_sequencias(t):
    """Modo Sequências: sequência nova, slices ligados no canvas (clique, retângulo, Shift, Ctrl, pincel, desfazer), Visão com
    Alt, gerador automático, envelope novo com âncora, efeito Sequência de slices numa camada e o botão Editar… do envelope.
    Gera docs/screenshot-sequencias.png."""
    app = t.app
    ensure_mode(app, "mapping")
    app.click("##map_add_tela", "Lista")
    app.wait_frames(4)
    rects = [(0, 0), (960, 0), (0, 540), (960, 540)]
    for i, (x, y) in enumerate(rects):
        if i > 0:
            app.click("##map_add_slice", "Lista")
            app.wait_frames(4)
        for lbl, v in (("##w", 960), ("##h", 540), ("##x", x), ("##y", y)):
            _type_field(app, lbl, v)

    ensure_mode(app, "sequences")
    app.wait_frames(6)
    for lbl, win in (("##seq_nova", "seq_lista"), ("##seq_visao_nova", "seq_lista"), ("##seq_ferr_ret", "seq_canvas"),
                     ("##seq_ferr_pincel", "seq_canvas"), ("##seq_ajustar", "seq_canvas"), ("##seq_desfazer", "seq_canvas")):
        expect(app.has(lbl, win), "controle %s em %s" % (lbl, win))
    n0 = len(_sq(app)["list"])
    app.click("##seq_nova", "seq_lista")
    app.wait_state(lambda s: len(s["sequences"]["list"]) == n0 + 1, "sequência criada")
    seq = _sq(app)
    expect(seq["list"][-1]["steps"] == 8 and seq["selected"] == seq["list"][-1]["id"], "sequência nova tem 8 passos e fica selecionada")
    index = len(seq["list"]) - 1
    app.wait_frames(6)
    slices = _seq_slices(app)
    expect(len(slices) >= 4, "o canvas mostra os slices (%d)" % len(slices))
    mine = sorted(slices[-4:], key=lambda i: (round(i.rect.y0), round(i.rect.x0)))
    total = _sq(app)["slices"]

    app.click_at(mine[0].rect.cx, mine[0].rect.cy)
    app.wait_state(lambda s: s["sequences"]["list"][index]["lit"][0] >= 1, "clique liga o slice")
    app.click_at(mine[0].rect.cx, mine[0].rect.cy)
    app.wait_state(lambda s: s["sequences"]["list"][index]["lit"][0] == 0, "clique de novo inverte")

    a = (mine[0].rect.x0 + 4, mine[0].rect.y0 + 4, mine[3].rect.x1 - 4, mine[3].rect.y1 - 4)
    b = (mine[0].rect.x0 + 4, mine[0].rect.y0 + 4, mine[1].rect.x1 - 4, mine[1].rect.y1 - 4)
    n_a = _seq_hits(app, *a)
    n_b = _seq_hits(app, *b)
    app.drag(*a)
    app.wait_state(lambda s: s["sequences"]["list"][index]["lit"][0] == n_a, "retângulo inverte os slices tocados (%d)" % n_a)
    app.drag(*b, mods=("ctrl",))
    app.wait_state(lambda s: s["sequences"]["list"][index]["lit"][0] == n_a - n_b, "Ctrl+retângulo desliga")
    app.drag(*b, mods=("shift",))
    app.wait_state(lambda s: s["sequences"]["list"][index]["lit"][0] == n_a, "Shift+retângulo liga")
    app.key("z", "ctrl")
    app.wait_state(lambda s: s["sequences"]["list"][index]["lit"][0] == n_a - n_b, "Ctrl+Z desfaz um gesto")
    app.key("y", "ctrl")
    app.wait_state(lambda s: s["sequences"]["list"][index]["lit"][0] == n_a, "Ctrl+Y refaz")

    app.click("##seq_limpar_passo", "seq_passos")
    app.wait_state(lambda s: s["sequences"]["list"][index]["lit"][0] == 0, "Limpar passo")
    app.click("##seq_ferr_pincel", "seq_canvas")
    app.wait_state(lambda s: s["sequences"]["tool"] == 1, "ferramenta Pincel")
    app.drag(mine[0].rect.cx, mine[0].rect.cy, mine[1].rect.cx, mine[1].rect.cy, steps=24)
    app.wait_state(lambda s: s["sequences"]["list"][index]["lit"][0] >= 2, "pincel liga os slices por onde passa")
    after_brush = _seq_lit(app, index)
    app.drag(mine[0].rect.cx, mine[0].rect.cy, mine[1].rect.cx, mine[1].rect.cy, steps=24)
    app.wait_state(lambda s: s["sequences"]["list"][index]["lit"][0] == 0, "segundo gesto inverte de volta (%d)" % after_brush)
    app.click("##seq_ferr_ret", "seq_canvas")

    app.key("right")
    app.wait_state(lambda s: s["sequences"]["step"] == 1, "seta direita vai ao passo 2")
    app.click("##seq_visao_nova", "seq_lista")
    app.wait_state(lambda s: len(s["sequences"]["views"]) >= 1, "visão criada")
    lit_before = sum(_sq(app)["list"][index]["lit"])
    app.drag(mine[2].rect.cx - 4, mine[2].rect.cy - 4, mine[2].rect.cx + 4, mine[2].rect.cy + 4, mods=("alt",))
    app.wait_state(lambda s: s["sequences"]["views"][-1]["count"] >= 1, "Alt edita a Visão selecionada")
    expect(sum(_sq(app)["list"][index]["lit"]) == lit_before, "Alt não mexe nos passos")

    app.click("##seq_limpar_tudo", "seq_passos")
    app.wait_state(lambda s: sum(s["sequences"]["list"][index]["lit"]) == 0, "Limpar tudo")
    expect(app.has("##seq_passos_total", "seq_passos") and app.has("##seq_repetir", "seq_passos"), "barra de passos")
    t.shot("sequencias_canvas")

    pick_tab(app, "Gerador")
    app.click("##ger_ligado", "seq_gerador")
    app.wait_state(lambda s: s["sequences"]["list"][index]["generator"], "gerador ligado")
    expect(sum(_sq(app)["list"][index]["lit"]) == total, "o gerador distribui todos os slices (%d)" % total)
    _seq_pick(app, "##ger_direcao", "seq_gerador", "Aleatório")
    app.wait_state(lambda s: s["sequences"]["list"][index]["genDirection"] == 4, "direção Aleatório")
    expect(sum(_sq(app)["list"][index]["lit"]) == total, "aleatório mantém todos os slices")
    app.click("##ger_editar_grade", "seq_gerador")
    app.wait_state(lambda s: s["sequences"]["gridEdit"], "edição da grade no canvas")
    app.click("##ger_editar_grade", "seq_gerador")
    app.wait_state(lambda s: not s["sequences"]["gridEdit"], "edição da grade desligada")

    pick_tab(app, "Envelopes")
    n_env = len(_sq(app)["envelopes"])
    app.click("##env_novo", "seq_envelopes")
    app.wait_state(lambda s: len(s["sequences"]["envelopes"]) == n_env + 1, "envelope criado")
    graph = app.find("env:grafico", "seq_envelopes").rect
    app.click_at(graph.cx, graph.cy, double=True)
    app.wait_state(lambda s: s["sequences"]["envelope"]["anchors"] == 3, "duplo clique na linha cria âncora")
    anchor = app.find("env:ancora:1", "seq_envelopes").rect
    app.drag(anchor.cx, anchor.cy, anchor.cx + 30, anchor.cy - 30, mods=("shift",))
    app.wait_state(lambda s: s["sequences"]["envelope"]["undo"], "mover âncora entra no desfazer próprio")
    app.key("z", "ctrl")
    app.wait_state(lambda s: s["sequences"]["envelope"]["redo"], "Ctrl+Z desfaz a curva")

    ensure_mode(app, "composition")
    pick_tab(app, "Efeitos")
    app.drag_items(search_effect(app, "Sequência de slices"), app.find("camada 1 nome"), steps=18)
    app.wait_state(lambda s: "bsl:sequencia" in [e["type"] for e in s["layers"][0]["effects"]], "efeito Sequência de slices na camada")
    pick_tab(app, "Camada")
    app.wait_frames(4)
    edit = scroll_into_view(app, "##env_editar", "cmp_aba_camada")
    combos = [i for i in app.find_all("##v", "cmp_aba_camada") if i.visible and i.rect.x1 - i.rect.x0 > 300 and i.rect.y0 < edit.rect.y0 - 4]
    expect(combos, "combo Curva do passo na camada")
    curva = max(combos, key=lambda i: i.rect.y0)
    app.click_at(curva.rect.cx, curva.rect.cy)
    app.wait_frames(3)
    app.click("Envelope", "Combo")
    app.wait_frames(6)
    edit = scroll_into_view(app, "##env_editar", "cmp_aba_camada")
    app.click_at(edit.rect.cx, edit.rect.cy)
    app.wait_state(lambda s: s["mode"] == "sequences", "Editar… abre o editor de envelopes")
    app.wait_frames(6)
    app.hover(5, 5)
    app.wait_frames(6)
    app.screenshot(os.path.join(ROOT, "docs", "screenshot-sequencias.png"))


OPTIONAL = {"projeto_dialogo"}
STANDALONE = {"exibir_reinicio"}

SCENARIOS = [
    ("modos", sc_modos),
    ("mapeamento", sc_mapeamento),
    ("grade", sc_grade),
    ("decks", sc_decks),
    ("transporte", sc_transporte),
    ("hot", sc_hot),
    ("clip", sc_clip),
    ("efeitos", sc_efeitos),
    ("geradores", sc_geradores),
    ("pixelmap", sc_pixelmap),
    ("sequencias", sc_sequencias),
    ("projeto", sc_projeto),
    ("projeto_dialogo", sc_projeto_dialogo),
    ("ver", sc_ver),
    ("ndi", sc_ndi),
    ("stream", sc_stream),
    ("previa", sc_previa),
    ("transform", sc_transform),
    ("layouts", sc_layouts),
    ("exibir", sc_exibir),
    ("atalhos", sc_atalhos),
    ("dmx", sc_dmx),
    ("atualizacao", sc_atualizacao),
    ("licencas", sc_licencas),
    ("plugins", sc_plugins),
    ("janela", sc_janela),
    ("minimizado", sc_minimizado),
    ("segundo_plano", sc_segundo_plano),
    ("fuzz", sc_fuzz),
    ("fechar", sc_fechar),
    ("exibir_reinicio", sc_exibir_reinicio),
]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--config", default="1.0", help="1.0 | 1.5 (escala fixa) | window (acompanha a janela)")
    ap.add_argument("--only", default="")
    ap.add_argument("--shots", default=os.path.join(ROOT, "docs", "qa"))
    ap.add_argument("--size", default="1600x900")
    ap.add_argument("--ndi-lib", default="", help="DLL do runtime do NDI (so para testes): liga o loopback do cenario ndi")
    args = ap.parse_args()

    w, h = (int(v) for v in args.size.split("x"))
    qa_dir = os.path.join(ROOT, "build", "qa", "cfg_" + args.config)
    extra = ["--ui-mode", "window"] if args.config == "window" else ["--ui-scale", args.config]
    if args.ndi_lib:
        extra += ["--ndi-lib", args.ndi_lib]
    if "projeto_dialogo" in [x for x in args.only.split(",") if x]:
        os.environ.pop("BSLSTAGE_QA_SAVE_PATH", None)
    else:
        os.environ["BSLSTAGE_QA_SAVE_PATH"] = os.path.join(qa_dir, "proj", "teste.bslstage")
    tag = {"1.0": "100", "1.5": "150", "window": "janela"}.get(args.config, args.config)
    plugin_dir = os.path.join(ROOT, "build", "qa", "plugin_qa_" + args.config)
    os.environ["BSLSTAGE_QA_PLUGIN_FILE"] = make_qa_package(plugin_dir)
    os.environ["BSLSTAGE_QA_PLUGIN_EXPORT"] = os.path.join(plugin_dir, "exportado.bslplugin")
    print("== BSL Stage UI QA: config=%s tamanho=%dx%d ==" % (args.config, w, h))
    stale = driver.kill_stale(os.path.join(ROOT, "build", "qa"))
    if stale:
        print("encerradas %d instancia(s) de teste que ficaram abertas" % stale)
    try:
        app = driver.App(EXE, qa_dir, extra, size=(w, h))
    except Exception:
        driver.stop_all()
        driver.kill_stale(os.path.join(ROOT, "build", "qa"))
        raise
    t = Tester(app, tag, args.shots)
    only = [s for s in args.only.split(",") if s]
    try:
        t.shot("inicial")
        for name, fn in SCENARIOS:
            if (only and name not in only) or (not only and name in OPTIONAL):
                continue
            print("[%s]" % name)
            if not app.alive() and name not in STANDALONE:
                t.results.append((name, False, "app nao esta mais rodando"))
                continue
            t.step(name, lambda fn=fn: fn(t))
    finally:
        alive = app.alive()
        code = app.quit()
        driver.stop_all()
        driver.kill_stale(os.path.join(ROOT, "build", "qa"))
        summary = app.summary()
        log = app.log_text()
    fails = [r for r in t.results if not r[1]]
    print("\n== Resumo (%s) ==" % args.config)
    print("passos: %d ok, %d com falha; app %s (codigo de saida %s)" % (len(t.results) - len(fails), len(fails), "vivo no fim" if alive else ("encerrado pelo roteiro" if t.closed else "MORREU"), code))
    if summary:
        print("asserts do ImGui: %d; conflitos de ID: %s" % (summary["asserts"], summary["conflicts"]))
        for a in summary.get("assertList", []):
            print("   assert:", a)
    for line in log.splitlines():
        if "[imgui]" in line or "[qa]" in line and "conflito" in line:
            print("   log:", line)
    for name, ok, detail in t.results:
        if not ok:
            print("  FALHA", name, "->", detail)
    if t.layout_notes:
        print("avisos de layout:")
        for where, s in dict.fromkeys(t.layout_notes):
            print("  [%s] %s" % (where, s))
    ok = not fails and (alive or t.closed) and code == 0 and summary and summary["asserts"] == 0 and not summary["conflicts"]
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
