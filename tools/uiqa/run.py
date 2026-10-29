"""Roteiro de teste de interface do BSL Stage com input real (mouse/teclado) - ver README.md.

Uso:  py -3 tools/uiqa/run.py --config 1.0|1.5|window [--only nome,...] [--shots docs/qa]
"""
import argparse
import os
import sys
import time
import traceback

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import driver  # noqa: E402
from driver import QaFailure  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
EXE = os.path.join(ROOT, "build", "Release", "BSLStage.exe")
MEDIA = os.path.join(ROOT, "testmedia")

MODE_COMP = "Composição###modo_composicao"
MODE_MAP = "Mapeamento###modo_mapeamento"
MODE_PIX = "Pixel Map###modo_pixelmap"


class Tester:
    def __init__(self, app, tag, shots):
        self.app = app
        self.tag = tag
        self.shots = shots
        self.results = []  # (nome, ok, detalhe)
        self.layout_notes = []
        self.closed = False  # o roteiro encerrou o app de proposito (sc_fechar)
        self._asserts0 = 0
        self._conf0 = 0

    def shot(self, name):
        path = os.path.join(self.shots, "%s_%s.png" % (self.tag, name))
        self.app.screenshot(path)
        return path

    def step(self, name, fn):
        t0 = time.time()
        try:
            fn()
            self.layout_check(name)
            d = self.app.read()
            if d["asserts"] > self._asserts0:
                raise QaFailure("assert do ImGui durante o passo (%d novos)" % (d["asserts"] - self._asserts0))
            if d["conflicts"] > self._conf0:
                raise QaFailure("conflito de ID durante o passo (%d novos)" % (d["conflicts"] - self._conf0))
            self.results.append((name, True, "%.1fs" % (time.time() - t0)))
            print("  ok    %s (%.1fs)" % (name, time.time() - t0))
        except Exception as e:  # noqa: BLE001
            detail = "%s: %s" % (type(e).__name__, e)
            if not isinstance(e, QaFailure):
                detail += "\n" + traceback.format_exc()
            self.results.append((name, False, detail))
            print("  FALHA %s -> %s" % (name, detail.splitlines()[0]))
            try:
                if self.app.alive():
                    self.shot("falha_" + name.replace(" ", "_").replace("/", "-"))
                    self.app.reset_input()
            except Exception:  # noqa: BLE001
                pass
        try:
            if self.app.alive():
                d = self.app.read()
                self._asserts0 = d["asserts"]
                self._conf0 = d["conflicts"]
        except Exception:  # noqa: BLE001
            pass

    # ---------------------------------------------------------------- verificacao de layout
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
        # sobreposicao entre itens com rotulo na mesma janela (ignora notas identicas aos itens reais)
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
                            continue  # um contem o outro / notas do QA
                        if area / small > 0.15:
                            issues.append("sobreposto: %r x %r em %s" % (items[i].label, items[j].label, wname.split("#")[0]))
        for s in dict.fromkeys(issues):
            self.layout_notes.append((where, s))
            print("      layout[%s]: %s" % (where, s))


# ---------------------------------------------------------------------- helpers
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
    for attempt in range(3):  # um popup aberto engole o primeiro clique
        app.click({"mapping": MODE_MAP, "pixelmap": MODE_PIX}.get(mode, MODE_COMP), "MainMenuBar")
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


# ---------------------------------------------------------------------- roteiro
def sc_modos(t):
    app = t.app
    for i in range(5):
        for mode in ("mapping", "pixelmap", "composition"):
            ensure_mode(app, mode)
            tabs = [x.label for x in app.items if x.window == "dock-tabs" and x.visible]
            for label in tabs:
                app.click(label, "dock-tabs", nth=0 if tabs.count(label) == 1 else 0)
            for top in ("Arquivo", "Composição", "Saída", "Ver", "Ajuda"):
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
    _set_snap(app, False)  # os primeiros passos conferem posições exatas

    def P(x, y):  # ponto do conteúdo da Saída -> cliente
        r = app.find("saida:imagem").rect
        return r.x0 + x * r.w / 1920.0, r.y0 + y * r.w / 1920.0

    def corners(i):
        return _slice(app, i, scr)["corners"]

    # --- Propriedades no modo Transformar: B = (900,100) 400x300; depois A = (100,100) 400x300
    for lbl, v in (("##ox", 900), ("##oy", 100), ("##ow", 400), ("##oh", 300)):
        _type_field(app, lbl, v)
    c = corners(1)
    expect(c[:2] == [900, 100] and c[4:6] == [1300, 400], "campos X/Y/L/A da saída montam o retângulo (%s)" % c)
    app.click_at(*P(200, 700))  # só o slice A (cheio) está aqui
    app.wait_state(lambda s: s["mapping"]["slice"] == 0 and s["mapping"]["multi"] == [0], "clique na vista seleciona o slice A")
    for lbl, v in (("##ox", 100), ("##oy", 100), ("##ow", 400), ("##oh", 300)):
        _type_field(app, lbl, v)
    app.click_at(*P(1100, 250))
    app.wait_state(lambda s: s["mapping"]["multi"] == [1], "clique seleciona o slice B")

    # --- Mover o miolo
    u0 = _map_ops(app)
    app.drag(*P(1100, 250), *P(1160, 290))
    c = corners(1)
    expect(abs(c[0] - 960) <= 8 and abs(c[1] - 140) <= 8, "arrastar o miolo move o slice (%s)" % c[:2])
    expect(abs((c[2] - c[0]) - 400) < 0.01 and abs((c[5] - c[1]) - 300) < 0.01, "mover não muda o tamanho")
    expect(_map_ops(app) == u0 + 1, "o arraste inteiro é 1 operação de desfazer (%d -> %d)" % (u0, _map_ops(app)))

    # --- Redimensionar pela borda direita (alça do meio)
    c = corners(1)
    ym = (c[1] + c[5]) / 2
    u0 = _map_ops(app)
    app.drag(*P(c[2], ym), *P(c[2] + 80, ym))
    c2 = corners(1)
    expect(abs((c2[2] - c2[0]) - 480) <= 8 and abs(c2[0] - c[0]) < 0.01 and abs((c2[5] - c2[1]) - 300) < 0.01,
           "alça da borda direita muda só a largura (%s -> %s)" % (c, c2))
    expect(_map_ops(app) == u0 + 1, "redimensionar = 1 operação")
    # Shift mantém a proporção (alça do canto inferior direito) e Alt cresce a partir do centro
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
    app.key("z", "ctrl")  # desfaz o Alt: volta ao B de 480 x 300
    c = corners(1)
    expect(abs((c[2] - c[0]) - 480) <= 8, "Ctrl+Z desfez o Alt e o Shift (largura %.1f)" % (c[2] - c[0]))

    c0 = corners(1)
    wb, hb = c0[2] - c0[0], c0[5] - c0[1]
    # --- Girar pela alça de cima (Shift: passos de 15 graus)
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
    # Ctrl+Z / Ctrl+Y
    app.key("z", "ctrl")
    c = corners(1)
    expect(abs(c[1] - c[3]) < 0.01 and abs((c[2] - c[0]) - wb) < 1, "Ctrl+Z desfaz a rotação (%s)" % c)
    expect(app.state["mapping"]["redo"] >= 1, "o refazer ficou disponível")
    app.key("y", "ctrl")
    c = corners(1)
    expect(abs(math.degrees(math.atan2(c[3] - c[1], c[2] - c[0])) - 90) < 0.5, "Ctrl+Y refaz a rotação")
    app.key("z", "ctrl")

    # --- Encostar (botão) e desfazer
    c = corners(1)
    a = corners(0)
    app.click("##map_encostar_esq", "Saída")
    c = corners(1)
    expect(c[0] == a[2] and c[6] == a[2], "Encostar: a borda esquerda de B ficou igual à direita de A (%s vs %s)" % (c[0], a[2]))
    app.key("z", "ctrl")
    expect(corners(1)[0] > a[2] + 100, "Ctrl+Z desfaz o Encostar")

    # --- Ímã ao arrastar: borda de B encaixa na borda de A, com linhas-guia (e Alt desliga)
    _set_snap(app, True)
    c = corners(1)
    a = corners(0)
    gap = c[0] - a[2]
    dx = -(gap - 8)  # deixa a borda esquerda de B a 8 px de A (conteúdo)

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

    # --- Seleção por retângulo e Ctrl+clique
    app.drag(*P(30, 30), *P(1700, 560))
    app.wait_state(lambda s: s["mapping"]["multi"] == [0, 1], "retângulo envolvendo os dois seleciona os dois")
    app.drag(*P(30, 30), *P(300, 200))
    app.wait_state(lambda s: s["mapping"]["multi"] == [0], "retângulo parcial seleciona só o A")
    app.drag(*P(1100, 300), *P(1100, 300), mods=("ctrl",))  # Ctrl+clique soma o B
    app.wait_state(lambda s: s["mapping"]["multi"] == [0, 1], "Ctrl+clique soma à seleção")
    # alinhar pelo topo (os dois selecionados)
    app.click("##map_alinhar_topo", "Saída")
    expect(abs(corners(0)[1] - corners(1)[1]) < 0.01 or abs(min(corners(1)[1::2]) - corners(0)[1]) < 0.01,
           "Alinhar ao topo com 2 slices")
    # mover os dois juntos (arrasta o B com os dois selecionados)
    a0, b0 = corners(0), corners(1)
    app.drag(*P(1100, 250), *P(1140, 280))
    a1, b1 = corners(0), corners(1)
    expect(abs((a1[0] - a0[0]) - (b1[0] - b0[0])) < 0.01 and a1[0] != a0[0], "mover move a seleção múltipla junta")
    app.key("z", "ctrl")
    app.key("z", "ctrl")

    # --- Modo Pontos: o canto encaixa no canto do outro slice ("conecta" um no outro)
    app.click("Pontos (warp)", "Saída")
    app.wait_state(lambda s: s["mapping"]["pointsMode"], "modo Pontos")
    expect(app.has("##cx", "Propriedades") and not app.has("##ox", "Propriedades"), "Propriedades mostram os 4 cantos no modo Pontos")
    app.click_at(*P(1100, 280))  # seleciona só o B
    app.wait_state(lambda s: s["mapping"]["multi"] == [1], "B selecionado")
    a = corners(0)
    c = corners(1)
    tx, ty = a[4] + 6, a[5] - 5  # perto do canto inferior direito de A
    app.drag(*P(c[6], c[7]), *P(tx, ty))
    c = corners(1)
    expect(c[6] == a[4] and c[7] == a[5], "Pontos: o canto BL de B encaixou no canto BR de A (%s,%s vs %s,%s)" % (c[6], c[7], a[4], a[5]))
    expect(app.state["mapping"]["corner"] == 3, "o canto arrastado ficou selecionado")
    t.shot("mapeamento_pontos")
    # Transformar um quad deformado: aviso e caixa; o campo X ainda mexe
    app.click("Transformar", "Saída")
    app.wait_state(lambda s: not s["mapping"]["pointsMode"], "volta ao modo Transformar")
    app.wait_frames(3)
    app.key("z", "ctrl")  # desfaz o encaixe do canto (B volta a ser retângulo)

    # --- Zoom na roda, pan (espaço+arrasto e botão do meio), Ajustar e 100%
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

    # --- Teclado: Ctrl+A, Delete com confirmação, Esc, Ctrl+D
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
    # setas na Saída (Transformar): 1 px e Shift = 10 px, sem rolar o painel
    app.click_at(*P(1100, 250))
    c0 = corners(1)
    app.key("right")
    app.key("down", "shift")
    c = corners(1)
    expect(abs(c[0] - (c0[0] + 1)) < 0.01 and abs(c[1] - (c0[1] + 10)) < 0.01, "setas movem o slice (1 px / Shift 10 px)")

    # --- Lista: clique seleciona e Ctrl+clique soma (as duas últimas linhas "Slice 1"/"Slice 2" são da tela nova)
    app.read()
    it1 = app.find_all("Slice 1", "Lista")[-1]
    app.click_at(it1.rect.cx, it1.rect.cy)
    app.wait_state(lambda s: s["mapping"]["multi"] == [0], "Lista: clique seleciona um slice")
    app.read()
    it2 = app.find_all("Slice 2", "Lista")[-1]
    app.drag(it2.rect.cx, it2.rect.cy, it2.rect.cx, it2.rect.cy, mods=("ctrl",))
    app.wait_state(lambda s: s["mapping"]["multi"] == [0, 1], "Lista: Ctrl+clique faz seleção múltipla")

    # --- Entrada: o mesmo editor (retângulos de entrada); ímã, seleção por retângulo e desfazer
    def Pi(x, y):
        r = app.find("entrada:imagem").rect
        cw = app.state["canvas"][0]
        return r.x0 + x * r.w / cw, r.y0 + y * r.w / cw

    def inp(i):
        return _slice(app, i, scr)["input"]

    for lbl, v in (("##w", 400), ("##h", 300), ("##x", 700), ("##y", 100)):
        _type_field(app, lbl, v)
    app.click_at(*Pi(200, 800))  # só o slice A (cheio) na Entrada
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
    app.click("##map_ima", "Entrada")  # o toggle também existe na Entrada (estado compartilhado)
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
    _set_snap(app, False)  # os passos abaixo conferem deslocamentos exatos; o ímã é testado em map_editor_checks

    # --- Entrada: redimensionar pelo canto inferior direito e mover
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
    # setas (com e sem Shift) na Entrada
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

    # --- Saida: canto e quad inteiro
    out = app.find("saida:imagem").rect
    s, scr, sl = cur()
    ko = out.w / scr["w"]
    tol = max(4.0, 2.0 / ko)  # 1 px da tela = 1/ko px do conteúdo (vista pequena = passo grosso)
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
    # mover o quad inteiro (arrasta pelo centro com nenhum canto perto)
    c = sl["corners"]
    mx = sum(c[0::2]) / 4
    my = sum(c[1::2]) / 4
    dm = max(40.0, 30.0 / ko)  # ao menos ~30 px de tela, senao nao passa do limiar de arrasto
    app.drag(out.x0 + mx * ko, out.y0 + my * ko, out.x0 + (mx + dm) * ko, out.y0 + (my + dm * 0.75) * ko)
    s, scr, sl = cur()
    expect(abs(sl["corners"][4] - (c[4] + dm)) < tol, "arrastar o quad inteiro moveu os cantos")
    t.shot("mapeamento_saida")

    map_editor_checks(t)  # Transformar, ímã, Encostar, Pontos, seleção múltipla, zoom/pan, teclado e desfazer

    # --- Renomear na lista (duplo clique) e propriedades
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
    # campo numerico das propriedades (precisa de um slice selecionado)
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
    app.click("##w", "Propriedades")  # sai do campo X (o limite e aplicado ao perder o foco)
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
    for lbl in ("Ajustar saída ao retângulo de entrada", "Resetar warp"):
        app.click(lbl, "Propriedades")
    app.click("Espelhar H", "Propriedades")
    app.click("Espelhar H", "Propriedades")
    app.click("Ativa", "Propriedades")
    app.click("Ativa", "Propriedades")
    t.shot("mapeamento_propriedades")
    ensure_mode(app, "composition")


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

    # --- 2 painéis: o do rascunho (Testeira Palco 2048x128 em 0,0) e uma lateral
    app.click("Adicionar painel", "pm_config")
    app.wait_state(lambda s: len(s["pixelmap"]["panels"]) == 1, "1o painel adicionado")
    _pm_add(app, "Lateral Esquerda", 512, 512, 0, 128)
    pm = _pm(app)
    p0, p1 = pm["panels"]
    expect(p0["name"] == "Testeira Palco" and p0["phys"] == [0, 0, 2048, 128], "1o painel do rascunho: %r" % p0)
    expect(p1["name"] == "Lateral Esquerda" and p1["phys"] == [0, 128, 512, 512], "2o painel digitado: %r" % p1)
    if pm["input"] == [1920, 1080]:  # canvas padrão: os números do HTML
        expect(p0["comp"] == [0, 0, 1920, 216] and p1["comp"] == [0, 216, 480, 864], "escala igual à do HTML: %r %r" % (p0["comp"], p1["comp"]))
    expect(pm["scaleWarn"], "aviso de escala não inteira (0,9375 x 1,6875)")
    expect(app.has("pixelmap:aviso-escala"), "aviso discreto visível no overview")
    expect(not p0["overlap"] and not p1["overlap"], "encostados não são sobrepostos")
    t.shot("pixelmap_dois_paineis")

    # --- editar um número na tabela (hot number): Y do 2º painel para 100 -> sobrepõe a Testeira, e em âmbar
    _pm_type(app, "##pm_y", 100, "pm_tabela", nth=1)
    pm = _pm(app)
    expect(pm["panels"][1]["phys"][1] == 100, "Y editado na tabela")
    expect(pm["panels"][1]["comp"][1] == round(100 * app.state["canvas"][1] / 640.0 + 1e-9) or pm["input"] != [1920, 1080],
           "composição recalculada: %r" % pm["panels"][1]["comp"])
    expect(pm["panels"][0]["overlap"] and pm["panels"][1]["overlap"], "sobreposição detectada")
    t.shot("pixelmap_sobreposicao")
    _pm_type(app, "##pm_y", 128, "pm_tabela", nth=1)
    expect(not _pm(app)["panels"][1]["overlap"], "voltou a 128: sem sobreposição")

    # --- painel fora da área física (vermelho)
    _pm_add(app, "Fora", 100, 100, 2000, 600)
    expect(_pm(app)["panels"][2]["outside"], "painel fora da área física marcado")
    app.click("##pm_remover", "pm_tabela", nth=2)
    app.wait_state(lambda s: len(s["pixelmap"]["panels"]) == 2, "painel Fora removido")

    # --- selecionar e arrastar no overview, com o ímã (coordenadas físicas)
    r = app.find("pixelmap:painel:1").rect
    app.click_at(r.cx, r.cy)
    app.wait_state(lambda s: s["pixelmap"]["selected"] == 1, "clique no overview seleciona o painel")
    if not _pm(app)["snap"]:
        app.click("##pm_ima", "pm_overview")
    cv = app.find("pixelmap:canvas").rect
    zoom = cv.w / app.state["canvas"][0]  # px de tela por px da composição
    # 3 px de tela abaixo do encaixe (y = 128, encostado na Testeira): o ímã devolve para 128
    app.drag(r.cx, r.cy, r.cx + 2 * app.scale, r.cy + 3 * app.scale)
    app.wait_frames(4)
    p1 = _pm(app)["panels"][1]
    expect(p1["phys"][1] == 128 and p1["phys"][0] == 0, "com o ímã o painel encaixa de volta: %r" % p1["phys"])
    # sem o ímã o mesmo arraste desloca
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

    # arraste com o ímã encaixando: sobe a lateral até ~6 px de tela da base da Testeira e as guias aparecem
    r = app.find("pixelmap:painel:1").rect
    seen = {"guides": 0}

    def probe():
        seen["guides"] = _pm(app)["guides"]

    app.drag(r.cx, r.cy, r.cx + 2 * app.scale, r.cy + 5 * app.scale, probe=probe)
    app.wait_frames(4)
    expect(_pm(app)["panels"][1]["phys"][1] == 128, "encaixa em y = 128")
    expect(seen["guides"] > 0, "linhas-guia durante o arraste com ímã")

    # --- Criar mapeamento: pergunta se substitui ou adiciona
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
    # Substituir a tela selecionada (a nova): continua com o mesmo número de telas
    app.click("Criar mapeamento", "pm_config")
    app.wait_frames(4)
    expect(app.has("##pm_criar_substituir"), "opção de substituir a tela existente")
    app.click("##pm_criar_substituir", "pm_modal_criar")
    app.wait_frames(6)
    expect(len(app.state["screens"]) == screens0 + 1, "substituir não cria tela")

    # Importar do mapeamento: lê os slices da tela selecionada (o inverso)
    _pm_clear(app)
    app.click("Importar do mapeamento", "pm_config")
    app.wait_state(lambda s: len(s["pixelmap"]["panels"]) == 2, "importar do mapeamento (sem painéis: direto)")
    pm = _pm(app)
    expect([p["phys"] for p in pm["panels"]] == [[0, 0, 2048, 128], [0, 128, 512, 512]], "ida e volta: %r" % [p["phys"] for p in pm["panels"]])
    expect(pm["phys"] == [2048, 640], "físico total = resolução da tela")
    app.click("Importar do mapeamento", "pm_config")  # com painéis: pede confirmação
    app.wait_frames(4)
    expect(app.has("Importar", "pm_modal_importar"), "confirmação ao substituir painéis")
    app.click("Cancelar", "pm_modal_importar")
    app.wait_frames(3)

    # desfazer no Mapeamento: a tela criada pelo Pixel Map some
    ensure_mode(app, "mapping")
    app.wait_frames(4)
    for _ in range(4):
        if len(app.state["screens"]) == screens0:
            break
        app.click("##map_desfazer", "Saída")
        app.wait_frames(4)
    expect(len(app.state["screens"]) == screens0, "Ctrl+Z do mapeamento desfaz o Criar mapeamento")
    ensure_mode(app, "pixelmap")

    # cores: o clique no quadrado troca pela próxima da paleta do HTML
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

    # arrastar arquivo do Navegador para a grade (drag-and-drop interno do ImGui)
    for name, (l, c) in {"photo.jpg": (1, 1), "h264_1080p.mp4": (1, 2), "alpha.png": (2, 1), "anim.gif": (2, 3)}.items():
        src = app.find(name, "cmp_arquivos")
        app.drag_items(src, cell(app, l, c), steps=18)
        app.wait_state(lambda s, l=l, c=c: any(x[0] == l - 1 and x[1] == c - 1 for x in s["cells"]), "arrastar %s para L%d C%d" % (name, l, c))
    s = app.state
    expect(len(s["cells"]) == 4, "4 clips na grade (%s)" % s["cells"])

    # clicar na celula dispara e seleciona
    app.click("celula L1 C1")
    s = app.wait_state(lambda s: s["layers"][0]["activeColumn"] == 0, "clique na celula dispara o clip")
    expect(s["selection"]["layer"] == 0 and s["selection"]["column"] == 0, "celula ficou selecionada")
    t.shot("grade_clip_ativo")
    # disparar coluna inteira
    app.click("coluna 2")
    app.wait_state(lambda s: s["layers"][0]["activeColumn"] == 1, "coluna 2 disparada")
    # X da camada
    app.click("##camada_1_parar")
    app.wait_state(lambda s: s["layers"][0]["activeColumn"] == -1, "X da camada para o clip")
    # + coluna / + camada
    cols, lay = app.state["columns"], len(app.state["layers"])
    scroll_to_visible(app, "##coluna_nova", cell(app, 2, 3))
    app.click("##coluna_nova")
    app.wait_state(lambda s: s["columns"] == cols + 1, "+ coluna")
    app.wheel(cell(app, 2, 3).rect.cx, cell(app, 2, 3).rect.cy, 15, shift=True)  # volta ao inicio da grade
    app.click("##camada_nova", "cmp_grade")
    app.wait_state(lambda s: len(s["layers"]) == lay + 1, "+ camada")
    # com 4 camadas as celulas deslocam-se: L1 continua sendo a camada de baixo
    # arrastar uma celula para outra (troca)
    a, b = cell(app, 1, 1), cell(app, 1, 5)
    app.drag_items(a, b, steps=18)
    app.wait_state(lambda s: any(x[0] == 0 and x[1] == 4 for x in s["cells"]) and not any(x[0] == 0 and x[1] == 0 for x in s["cells"]),
                   "arrastar celula L1C1 para L1C5 (troca com vazia)")
    # botao direito -> Limpar
    app.click("celula L1 C5", button="right")
    app.wait_frames(3)
    app.click("Limpar")
    app.wait_state(lambda s: not any(x[0] == 0 and x[1] == 4 for x in s["cells"]), "botao direito > Limpar")
    # Delete na celula selecionada
    app.click("celula L1 C2")
    app.wait_state(lambda s: s["selection"]["column"] == 1 and s["selection"]["layer"] == 0, "seleciona L1C2")
    app.key("delete")
    app.wait_state(lambda s: not any(x[0] == 0 and x[1] == 1 for x in s["cells"]), "Delete limpa a celula selecionada")
    # arquivo vindo do Explorer (WM_DROPFILES)
    n = len(app.state["cells"])
    app.drop_files([os.path.join(MEDIA, "photo.jpg")], cell(app, 3, 1))
    app.wait_state(lambda s: len(s["cells"]) == n + 1 and any(x[0] == 2 and x[1] == 0 for x in s["cells"]), "drop WM_DROPFILES sobre L3C1")
    app.drop_files([os.path.join(MEDIA, "static.webp"), os.path.join(MEDIA, "vp8.webm")], cell(app, 3, 3))
    app.wait_state(lambda s: any(x[0] == 2 and x[1] == 3 for x in s["cells"]), "drop de 2 arquivos preenche celulas seguintes")
    t.shot("grade_preenchida")
    app.click("celula L1 C1")  # dispara para mostrar o estado ativo
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
    # excluir o deck 1 (tem clips): confirmacao
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
    # o deck restante precisa estar valido e a grade desenhada
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
    for i in range(4):  # TAP 4x; o BPM esperado vem dos intervalos medidos entre os cliques
        app.click_at(tap.rect.cx, tap.rect.cy)
        stamps.append(time.time())
        time.sleep(0.12)
    app.wait_frames(2)
    bpm = app.state["bpm"]
    avg = (stamps[-1] - stamps[0]) / 3.0
    expect(abs(bpm - 60.0 / avg) / (60.0 / avg) < 0.2,
           "TAP 4x ajustou o BPM (%.1f, esperado ~%.1f)" % (bpm, 60.0 / avg))
    # digitar BPM: clique sem arrastar no numero "quente" abre o campo de digitacao
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
    app.click("##bpm_menos")  # tira o BPM do valor padrao
    app.click("##bpm_menos")
    b0 = app.state["bpm"]
    # arrastar 40 px para a direita: 0,1 BPM por pixel
    app.drag(r.x0 + 6, r.cy, r.x0 + 46, r.cy, steps=10)
    b1 = app.state["bpm"]
    expect(b1 > b0 + 2.0, "arrastar o BPM para a direita aumenta (%.2f -> %.2f)" % (b0, b1))
    # Shift = x10
    r = app.find("##bpm", "cmp_grade").rect
    app.drag(r.x0 + 6, r.cy, r.x0 + 26, r.cy, steps=10, mods=("shift",))
    b2 = app.state["bpm"]
    expect(b2 > b1 + 10.0, "Shift multiplica o arrasto por 10 (%.2f -> %.2f)" % (b1, b2))
    # Ctrl = x0,1
    r = app.find("##bpm", "cmp_grade").rect
    app.drag(r.x0 + 6, r.cy, r.x0 + 46, r.cy, steps=10, mods=("ctrl",))
    b3 = app.state["bpm"]
    expect(0.0 < b3 - b2 < 1.0, "Ctrl reduz o arrasto a 1/10 (%.2f -> %.2f)" % (b2, b3))
    t.shot("hot_arrastando")
    # clicar sem arrastar abre o campo; Enter confirma
    app.click("##bpm", "cmp_grade")
    app.key("a", "ctrl")
    app.type_text("100")
    t.shot("hot_digitando")
    app.key("enter")
    app.wait_state(lambda s: abs(s["bpm"] - 100.0) < 0.01, "digitar 100 no BPM e Enter")
    # Esc cancela
    app.click("##bpm", "cmp_grade")
    app.key("a", "ctrl")
    app.type_text("55")
    app.key("esc")
    app.wait_frames(3)
    expect(abs(app.state["bpm"] - 100.0) < 0.01, "Esc cancela a digitacao (bpm %.2f)" % app.state["bpm"])
    # botao direito: Restaurar padrao
    app.click("##bpm", "cmp_grade", button="right")
    app.wait_frames(3)
    app.click("Restaurar padrão", "Popup_")
    app.wait_state(lambda s: abs(s["bpm"] - 120.0) < 0.01, "Restaurar padrão volta o BPM a 120")
    # valores fora do limite sao limitados ao confirmar
    app.click("##bpm", "cmp_grade")
    app.key("a", "ctrl")
    app.type_text("9999")
    app.key("enter")
    app.wait_state(lambda s: abs(s["bpm"] - 300.0) < 0.01, "9999 vira o maximo (300)")
    app.click("##bpm", "cmp_grade", button="right")
    app.wait_frames(3)
    app.click("Restaurar padrão", "Popup_")
    app.wait_state(lambda s: abs(s["bpm"] - 120.0) < 0.01, "BPM de volta a 120")
    # Tab: da opacidade master para a da camada de cima
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
    # volta aos padroes
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
    # garante um video na L1C2 e dispara
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
    app.drag(sp.x0 + 4, sp.cy, sp.x0 + 64, sp.cy, steps=8)  # numero "quente": arrastar na horizontal muda o valor
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
        app.wheel(win.cx, win.cy, -2 if i < tries // 2 else 2)  # primeiro para baixo, depois para cima
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
    app.find("BSL Light", "cmp_efeitos")  # espera o snapshot da aba recem-aberta
    # (a secao BSL Light e longa: Nativos, ISF e FFGL ficam abaixo da dobra, mas existem na lista)
    expect(bool(app.find_all("Nativos", "cmp_efeitos", visible=False)) and bool(app.find_all("ISF", "cmp_efeitos", visible=False)),
           "secoes Nativos e ISF na aba Efeitos")
    expect(bool(app.find_all("FFGL", "cmp_efeitos", visible=False)) and app.has("##plugins_abrir", "cmp_efeitos"),
           "secao FFGL e botao Plugins na aba Efeitos")
    expect(app.has("BSL Light", "cmp_efeitos") and app.has("BSL Alpha Reactor", "cmp_efeitos"),
           "secao BSL Light (plugins nativos) no topo da aba Efeitos")
    t.shot("efeitos_aba")

    def types():
        return [e["type"] for e in app.state["selClip"]["effects"]]

    # 1) arrastar da aba Efeitos para a celula: entra na cadeia do clip
    app.drag_items(search_effect(app, "Inverter"), cell(app, 1, 2), steps=18)
    app.wait_state(lambda s: [e["type"] for e in s["selClip"]["effects"]] == ["native:invert"], "arrastar para a celula")
    expect(app.has("celula L1 C2"), "celula continua na grade")
    # 2) duplo clique no item: entra no clip selecionado
    search_effect(app, "Vinheta")
    app.click("Vinheta", "cmp_efeitos", double=True)
    app.wait_state(lambda s: [e["type"] for e in s["selClip"]["effects"]] == ["native:invert", "native:vignette"],
                   "duplo clique adiciona ao clip selecionado")
    # 3) arrastar para a area de soltar da cadeia (painel Clip)
    search_effect(app, "Strobe")
    drop = scroll_into_view(app, "##clip_efeitos_soltar", "cmp_clip")
    app.drag_items(app.find("Strobe", "cmp_efeitos"), drop, steps=18)
    app.wait_state(lambda s: len(s["selClip"]["effects"]) == 3 and s["selClip"]["effects"][2]["type"] == "native:strobe",
                   "arrastar para a area do painel Clip")
    t.shot("efeitos_cadeia_clip")
    # 4) reordenar: a alca do 3o efeito sobre o cabecalho do 1o
    src = scroll_into_view(app, "##clip_efeito_3_alca", "cmp_clip")
    dst = scroll_into_view(app, "##clip_efeito_1_cabecalho", "cmp_clip")
    if not (fully_visible(app, "##clip_efeito_3_alca", "cmp_clip") and fully_visible(app, "##clip_efeito_1_cabecalho", "cmp_clip")):
        # painel pequeno: recolhe o 1o efeito (clicar no cabecalho) para os dois caberem
        app.click("##clip_efeito_1_cabecalho", "cmp_clip")
        app.wait_frames(3)
        src = scroll_into_view(app, "##clip_efeito_3_alca", "cmp_clip")
        dst = scroll_into_view(app, "##clip_efeito_1_cabecalho", "cmp_clip")
    app.drag_items(src, dst, steps=18)
    app.wait_state(lambda s: [e["type"] for e in s["selClip"]["effects"]] == ["native:strobe", "native:invert", "native:vignette"],
                   "reordenar arrastando a alca")
    # 5) bypass (olho)
    b1 = scroll_into_view(app, "##clip_efeito_1_bypass", "cmp_clip")
    app.click_at(b1.rect.cx, b1.rect.cy)
    app.wait_state(lambda s: s["selClip"]["effects"][0]["bypass"], "bypass do 1o efeito")
    t.shot("efeitos_bypass")
    b1 = scroll_into_view(app, "##clip_efeito_1_bypass", "cmp_clip")
    app.click_at(b1.rect.cx, b1.rect.cy)
    app.wait_state(lambda s: not s["selClip"]["effects"][0]["bypass"], "tirar o bypass")
    # 6) menu: duplicar, remover
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
    # restaurar padroes (nao muda a quantidade; so confere que nao quebra)
    m1 = scroll_into_view(app, "##clip_efeito_1_menu", "cmp_clip")
    app.click_at(m1.rect.cx, m1.rect.cy)
    app.wait_frames(3)
    app.click("##efeito_restaurar", "Popup_")
    app.wait_frames(3)
    # o projeto ficou sujo por causa dos efeitos
    expect(app.state["dirty"], "mexer nos efeitos marca o projeto como modificado")
    # esvazia a cadeia pelo menu Remover
    for _ in range(3):
        m = scroll_into_view(app, "##clip_efeito_1_menu", "cmp_clip")
        app.click_at(m.rect.cx, m.rect.cy)
        app.wait_frames(3)
        app.click("##efeito_remover", "Popup_")
        app.wait_frames(3)
    app.wait_state(lambda s: len(s["selClip"]["effects"]) == 0, "cadeia do clip esvaziada")

    # 7) camada: soltar no cabecalho da camada
    pick_tab(app, "Efeitos")
    app.drag_items(search_effect(app, "Espelho"), app.find("camada 1 nome"), steps=18)
    app.wait_state(lambda s: [e["type"] for e in s["layers"][0]["effects"]] == ["native:mirror"], "soltar no cabecalho da camada")
    pick_tab(app, "Camada")
    app.wait_frames(3)
    expect(app.has("##camada_efeito_1_cabecalho", "cmp_aba_camada"), "cadeia da camada na aba Camada")
    t.shot("efeitos_camada")
    mm = scroll_into_view(app, "##camada_efeito_1_menu", "cmp_aba_camada")
    app.click_at(mm.rect.cx, mm.rect.cy)
    app.wait_frames(3)
    app.click("##efeito_remover", "Popup_")
    app.wait_state(lambda s: len(s["layers"][0]["effects"]) == 0, "remover o efeito da camada")

    # 8) master: aba Composicao; o botao "Efeito" abre o catalogo (a lista de efeitos fica noutra aba)
    # h\u00e1 duas abas "Composi\u00e7\u00e3o" (a grade e a aba do canto): a da aba \u00e9 a mais baixa
    tabs = sorted(app.find_all("tab:Composi\u00e7\u00e3o", "dock-tabs"), key=lambda i: i.rect.y0)
    app.click_at(tabs[-1].rect.cx, tabs[-1].rect.cy)
    app.wait_frames(3)
    add = scroll_into_view(app, "##master_efeito_adicionar", "cmp_aba_composicao")
    app.click_at(add.rect.cx, add.rect.cy)
    app.wait_frames(4)
    app.type_text("Vinheta")  # o campo de busca do catalogo nasce com o foco
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

    # 9) ISF: efeito e gerador
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
    # limpa: tira o efeito do clip L1C2
    app.click("celula L1 C2")
    app.wait_state(lambda s: s["selection"]["layer"] == 0 and s["selection"]["column"] == 1, "volta ao clip L1C2")
    m = scroll_into_view(app, "##clip_efeito_1_menu", "cmp_clip")
    app.click_at(m.rect.cx, m.rect.cy)
    app.wait_frames(3)
    app.click("##efeito_remover", "Popup_")
    app.wait_state(lambda s: len(s["selClip"]["effects"]) == 0, "limpa o efeito ISF")

    # 10) BSL Slice Refiner (BSR3): editor nativo no painel Clip (selecionar o slice, enviar ao grupo 2, ficha do grupo)
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
    win = app.window_rect("cmp_clip")  # o editor e longo: volta ao topo do painel antes de achar o menu do efeito
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
    if win:  # a aba pode estar rolada (o cenario efeitos deixa a lista no fim)
        app.wheel(win.cx, win.cy, 20)
    expect(app.has("BSL Light", "cmp_geradores") and app.has("BSL Background", "cmp_geradores")
           and app.has("LED Mapper Pro", "cmp_geradores") and app.has("Padrão de calibração BSL", "cmp_geradores"),
           "secao BSL Light na aba Fontes")
    t.shot("geradores")
    # arrastar o gerador Texto para L3 C2 e mexer nos parametros
    app.drag_items(app.find("Texto", "cmp_geradores"), cell(app, 3, 2), steps=18)
    app.wait_state(lambda s: any(x[0] == 2 and x[1] == 1 for x in s["cells"]), "gerador Texto arrastado")
    app.click("celula L3 C2")
    app.wait_state(lambda s: s["selection"]["layer"] == 2 and s["selection"]["column"] == 1, "seleciona Texto")
    app.wait_frames(10)
    fields = app.find_all("##v", "cmp_clip")
    expect(len(fields) >= 3, "painel Clip mostra os parametros do Texto (%d)" % len(fields))
    # primeiro parametro: campo de texto multilinha
    f0 = fields[0]
    app.click_at(f0.rect.cx, f0.rect.cy)
    app.key("a", "ctrl")
    app.type_text("Ola BSL")
    app.wait_state(lambda s: s["selClip"]["params"].get("text") == "Ola BSL", "digitar no parametro Texto")
    t.shot("clip_texto")
    # percorre os demais widgets do painel: clique no centro e Esc (sem exigir efeito)
    for i in range(1, min(len(fields), 12)):
        app.read()
        fs = app.find_all("##v", "cmp_clip")
        if i >= len(fs):
            break
        app.click_at(fs[i].rect.cx, fs[i].rect.cy)
        app.wait_frames(2)
        app.key("esc")
    # duplo clique em gerador carrega na celula selecionada
    app.click("celula L3 C4")
    pick_tab(app, "Fontes")
    app.click("Cor sólida", "cmp_geradores", double=True)
    app.wait_state(lambda s: any(x[0] == 2 and x[1] == 3 and "Cor" in x[2] for x in s["cells"]), "duplo clique carrega gerador")
    # Captura de tela: abre o seletor de alvo
    app.drag_items(app.find("Captura de tela", "cmp_geradores"), cell(app, 3, 5), steps=18)
    app.wait_frames(10)
    expect(app.has("Atualizar lista"), "seletor de alvo da captura abriu ao soltar o gerador")
    t.shot("captura_seletor")
    app.click("Atualizar lista")
    app.wait_frames(10)
    app.click("##card", nth=0)
    app.wait_frames(6)
    expect(not app.has("Atualizar lista"), "escolher o alvo fecha o seletor")
    expect("target" in app.state["selClip"]["params"], "alvo gravado nos parametros")
    # reabre pelo botao do painel Clip e cancela
    alvo = [i for i in app.items if "cmp_clip" in i.window and i.label.endswith("##alvo") and i.visible]
    expect(bool(alvo), "botao do alvo no painel Clip")
    app.click_at(alvo[0].rect.cx, alvo[0].rect.cy)
    app.wait_frames(8)
    app.click("Cancelar")
    app.wait_frames(4)
    expect(not app.has("Atualizar lista"), "Cancelar fecha o seletor")
    # PDF: navega ate docs/ e arrasta
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


def sc_projeto(t):
    app = t.app
    ensure_mode(app, "composition")
    target = os.path.join(app.dir, "proj", "teste.bslstage")
    os.makedirs(os.path.dirname(target), exist_ok=True)
    if os.path.exists(target):
        os.remove(target)
    app.focus()
    app.key("s", "ctrl", wait=False)  # sem caminho: abre "Salvar como" (o app fica no dialogo)
    dlg = app.find_dialog()
    expect(dlg != 0, "Ctrl+S abriu o dialogo Salvar como")
    time.sleep(0.5)
    driver_type(app, target)
    time.sleep(1.0)
    app.wait_state(lambda s: s["project"].lower().endswith("teste.bslstage"), "projeto salvo no caminho digitado", timeout=10)
    expect(os.path.exists(target), "arquivo .bslstage gravado")
    expect(not app.state["dirty"], "projeto limpo depois de salvar")
    # alteracao -> sujo
    menu(app, "Composição", "+ Coluna")
    app.wait_state(lambda s: s["dirty"], "alteracao marca o projeto como modificado")
    # Ctrl+O -> modal "Salvar alteracoes?" -> Cancelar
    app.key("o", "ctrl")
    app.wait_frames(4)
    expect(app.has("Não salvar"), "modal Salvar alterações? apareceu")
    t.shot("modal_salvar_alteracoes")
    app.click("Cancelar", "Salvar")
    app.wait_frames(3)
    expect(not app.has("Não salvar") and app.state["dirty"], "Cancelar mantem tudo como estava")
    # "Nao salvar" -> abre o dialogo Abrir -> cancela
    app.key("o", "ctrl")
    app.wait_frames(4)
    clicked_dialog(app, "Não salvar", "Salvar")
    dlg = app.find_dialog()
    expect(dlg != 0, "Nao salvar abriu o dialogo Abrir")
    app.dialog_close(dlg)
    app.wait_frames(6)
    # "Salvar" -> salva e abre o dialogo Abrir -> cancela
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
    # Abrir recente
    menu(app, "Arquivo", "##menu_recentes", "teste.bslstage")
    app.wait_frames(10)
    expect(app.state["project"].lower().endswith("teste.bslstage"), "Abrir recente reabriu o projeto")
    # Ctrl+N com o projeto limpo
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
    app.click_at(it.rect.cx, it.rect.cy)  # sem wait_frames: o app pode abrir um dialogo modal


def sc_ver(t):
    app = t.app
    ensure_mode(app, "composition")
    for label, expected in (("100%", 1.0), ("150%", 1.5), ("90%", 0.9), ("200%", 2.0), ("125%", 1.25)):
        menu(app, "Ver", "Tamanho da interface", label)
        app.wait_frames(6)
        expect(abs(app.scale - expected) < 0.01, "escala %s aplicada (%.2f)" % (label, app.scale))
        expect(app.state["scaleMode"] == "fixed", "modo fixo")
        t.layout_check("escala_" + label)
        t.shot("escala_" + label.replace("%", ""))
    menu(app, "Ver", "Tamanho da interface", "Automático (escala do Windows)")
    app.wait_frames(6)
    expect(app.state["scaleMode"] == "system", "modo Automático")
    t.shot("escala_automatico")
    menu(app, "Ver", "Tamanho da interface", "Acompanhar a janela (recomendado)")
    app.wait_frames(6)
    expect(app.state["scaleMode"] == "window", "modo Acompanhar a janela")
    t.shot("escala_janela")


def sc_ndi(t):
    """NDI e Spout: as fontes na aba Fontes, o painel Clip (lista de fontes, mesmo vazia, ou o aviso de runtime ausente),
    o destino NDI da Screen e a janela Compatibilidade de vídeo. Vale com e sem o runtime do NDI (--ndi-lib): sem ele
    confere o aviso "NDI Runtime não encontrado"; com ele faz o loopback (a fonte NDI recebendo a saída NDI do app)."""
    app = t.app
    ensure_mode(app, "composition")
    menu(app, "Ver", "Layout", "Padrão (media server)")
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

    # --- fonte NDI numa celula
    app.drag_items(scroll_into_view(app, "NDI", "cmp_geradores"), cell(app, 3, 3), steps=18)
    app.wait_state(lambda s: any(x[0] == 2 and x[1] == 2 and x[2].startswith("NDI") for x in s["cells"]), "gerador NDI arrastado")
    app.click("celula L3 C3 nome")
    app.wait_state(lambda s: s["selection"]["layer"] == 2 and s["selection"]["column"] == 2, "seleciona a fonte NDI")
    app.wait_frames(10)
    if have_rt:
        expect(app.has("##ndi_fonte", "cmp_clip"), "seletor de fontes NDI no painel Clip")
        app.click("##ndi_fonte", "cmp_clip")  # a lista pode estar vazia: abre e fecha sem erro
        app.wait_frames(4)
        t.shot("ndi_lista")
        app.key("esc")
    else:
        expect(app.has("ndi:sem_runtime", "cmp_clip"), "aviso \"NDI Runtime não encontrado\" no painel Clip")
        expect(app.has("##ndi_link_copiar", "cmp_clip"), "botao do link do NDI")
    checks = app.find_all("##v", "cmp_clip")
    expect(len(checks) >= 2, "opcoes de baixa latencia e banda baixa no painel Clip (%d)" % len(checks))
    app.click_at(checks[0].rect.cx, checks[0].rect.cy)  # baixa latencia
    app.wait_state(lambda s: s["selClip"]["params"].get("lowLatency") is True, "baixa latencia ligada")
    app.read()
    checks = app.find_all("##v", "cmp_clip")
    app.click_at(checks[1].rect.cx, checks[1].rect.cy)  # banda baixa
    app.wait_state(lambda s: s["selClip"]["params"].get("lowBandwidth") is True, "banda baixa ligada")
    t.shot("ndi_clip")
    # dispara a fonte (sem fonte escolhida / sem runtime): nao pode quebrar, e o status explica
    app.click("celula L3 C3")
    app.wait_state(lambda s: s["layers"][2]["activeColumn"] == 2, "dispara a fonte NDI")
    app.wait_frames(20)
    st = app.read()["state"]["selClip"]["status"]
    if have_rt:
        expect(st != "", "status da fonte NDI (%r)" % st)
    else:
        expect(st == "NDI Runtime não encontrado", "status sem runtime (%r)" % st)
    app.click("##camada_3_parar")

    # --- fonte Spout
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

    # --- janela Compatibilidade de vídeo
    menu(app, "Ver", "##menu_compat")
    app.wait_frames(6)
    expect(app.has("compat:ndi_status", "janela_compat") and app.has("compat:spout_status", "janela_compat"),
           "estado do NDI e do Spout na janela de compatibilidade")
    if not have_rt:
        expect(app.has("ndi:sem_runtime", "janela_compat"), "aviso de runtime ausente na janela de compatibilidade")
    t.shot("compat")
    app.click("##compat_fechar")
    app.wait_frames(4)
    expect(not app.has("compat:ndi_status"), "janela de compatibilidade fechou")

    # --- destino NDI da Screen (modo Mapeamento)
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
        # loopback: a fonte NDI recebe o que o app mesmo envia
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

    # volta o destino da Screen ao que era
    ensure_mode(app, "mapping")
    pick_combo(app, "##destino", "Propriedades", "Desativado")
    app.wait_state(lambda s: s["screens"][s["mapping"]["screen"]]["kind"] == "Desativado", "destino desativado")
    ensure_mode(app, "composition")


def preview_state(app):
    return app.read()["state"]["preview"]


def drop_clip(app, name, layer, col):
    """Solta um arquivo de testmedia na celula (como o Explorer) e espera ela aparecer na grade."""
    app.drop_files([os.path.join(MEDIA, name)], cell(app, layer, col))
    app.wait_state(lambda s: any(x[0] == layer - 1 and x[1] == col - 1 for x in s["cells"]), "%s em L%dC%d" % (name, layer, col))


def pick_combo(app, combo, window, item):
    app.click(combo, window)
    app.wait_frames(3)
    app.click(item, "Combo")
    app.wait_frames(3)


def sc_previa(t):
    """Prévia e grade como no Resolume: o nome da celula so leva para a Previa; a imagem toca na saida."""
    app = t.app
    ensure_mode(app, "composition")
    menu(app, "Ver", "Layout", "Padrão (media server)")
    app.wait_frames(8)
    # L1C1 video (vai ao vivo), L1C2 outro video (so previa), L2C1 foto
    drop_clip(app, "h264_1080p.mp4", 1, 1)
    drop_clip(app, "vp8.webm", 1, 2)
    drop_clip(app, "photo.jpg", 2, 1)
    expect(app.has("celula L1 C1 nome") and app.has("celula L1 C2 nome"), "a celula tem a area do nome")

    # clicar na IMAGEM: dispara na camada e seleciona
    app.click("celula L1 C1")
    s = app.wait_state(lambda s: s["layers"][0]["activeColumn"] == 0, "clicar na imagem dispara o clip na camada")
    expect(s["selection"]["layer"] == 0 and s["selection"]["column"] == 0, "a celula clicada ficou selecionada")
    s = app.wait_state(lambda s: s["preview"]["live"] and not s["preview"]["own"], "Prévia espelha o clip ao vivo (sem 2a instância)")
    expect(s["preview"]["column"] == 0 and s["preview"]["mode"] == "clip", "Prévia mostra o clip selecionado")
    app.wait_frames(8)

    # clicar no NOME: carrega na Previa, sem disparar e sem mudar a coluna ativa
    app.click("celula L1 C2 nome")
    s = app.wait_state(lambda s: s["selection"]["layer"] == 0 and s["selection"]["column"] == 1, "clicar no nome seleciona o clip")
    expect(s["layers"][0]["activeColumn"] == 0, "clicar no nome NÃO muda a coluna ativa da camada (%s)" % s["layers"][0]["activeColumn"])
    s = app.wait_state(lambda s: s["preview"]["own"] and not s["preview"]["live"], "Prévia abre o clip selecionado numa instância própria")
    expect(s["preview"]["column"] == 1 and s["preview"]["hasClip"], "Prévia carregou a coluna 2")
    expect(s["selClip"]["name"] == "vp8.webm" and not s["selClip"]["open"], "o painel Clip mostra o clip selecionado (parado na saída)")
    app.wait_frames(6)
    t.shot("previa_clip_diferente_do_ao_vivo")

    # transporte da Previa: nao mexe na saida
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

    # "Ao vivo": dispara o clip da Previa na camada dele
    expect(not app.find("##previa_ao_vivo", "cmp_previa").disabled, "Ao vivo habilitado para um clip fora da saída")
    app.click("##previa_ao_vivo", "cmp_previa")
    s = app.wait_state(lambda s: s["layers"][0]["activeColumn"] == 1, "Ao vivo dispara o clip da Prévia")
    s = app.wait_state(lambda s: s["preview"]["live"] and not s["preview"]["own"], "depois de Ao vivo a Prévia espelha a saída")
    expect(app.find("##previa_ao_vivo", "cmp_previa").disabled, "Ao vivo desabilitado quando o clip já está na saída")
    # com o clip ao vivo o transporte da Previa e o da saida
    app.click("##previa_tocar_pausar", "cmp_previa")
    app.wait_state(lambda s: not s["layers"][0]["clipPlaying"], "Prévia ao vivo: pausar pausa a saída")
    app.click("##previa_tocar_pausar", "cmp_previa")
    app.wait_state(lambda s: s["layers"][0]["clipPlaying"], "Prévia ao vivo: tocar volta a tocar a saída")

    # nome de outra camada (foto na L2) tambem so seleciona
    app.click("celula L2 C1 nome")
    s = app.wait_state(lambda s: s["selection"]["layer"] == 1 and s["selection"]["column"] == 0, "nome na camada 2 seleciona")
    expect(s["layers"][1]["activeColumn"] == -1, "a camada 2 continua parada")
    app.wait_state(lambda s: s["preview"]["hasClip"] and not s["preview"]["live"], "Prévia mostra a foto")

    # modo Camada selecionada: a camada com o blend dela sobre preto
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

    # quadriculado e encaixe
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
        menu(app, "Ver", "Layout", label)
        app.wait_frames(8)
        expect(app.state["layout"] == key, "layout %s ativo" % key)
        expect(app.has("celula L1 C1"), "grade visível no layout %s" % key)
        t.shot("layout_" + key)
    # Restaurar layout volta ao layout ativo
    menu(app, "Ver", "Layout", "Grade grande")
    app.wait_frames(6)
    menu(app, "Ver", "Restaurar layout")
    app.wait_frames(8)
    expect(app.state["layout"] == "bigGrid", "Restaurar layout mantem o layout escolhido")
    menu(app, "Ver", "Layout", "Padrão (media server)")
    app.wait_frames(6)
    # trocar o layout dentro do Mapeamento vale ao voltar
    ensure_mode(app, "mapping")
    menu(app, "Ver", "Layout", "Saída grande")
    ensure_mode(app, "composition")
    app.wait_frames(8)
    expect(app.state["layout"] == "bigOutput", "layout escolhido no Mapeamento aplicado ao voltar")
    menu(app, "Ver", "Layout", "Padrão (media server)")
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
    menu(app, "Ver", "Layout", "##layout_salvar")
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
    # estado de referencia: layout padrao, Previa no modo Camada, sem quadriculado
    menu(app, "Ver", "Layout", "Padrão (media server)")
    app.wait_frames(8)
    pick_combo(app, "##previa_fonte", "cmp_previa", "Camada selecionada")
    app.click("##previa_quadriculado", "cmp_previa")
    app.wait_state(lambda s: s["preview"]["mode"] == "layer" and not s["preview"]["checker"], "Prévia no modo Camada, sem quadriculado")
    ref = {w: rect_of(app, w) for w in ("cmp_grade", "cmp_saida", "cmp_previa", "cmp_clip")}
    ref_frac = {w: fractions(app, w) for w in ref}
    save_layout_preset(app, "Palco A")
    t.shot("layouts_menu_depois_de_salvar")
    # o menu mostra o preset com o atalho Alt+1
    app.click("Ver", "MainMenuBar")
    app.wait_frames(4)
    it = app.find("Layout", "Menu_")
    app.hover(it.rect.cx, it.rect.cy)
    app.wait_frames(6)
    expect(app.has("Palco A", "Menu_") and app.has("##layout_gerenciar", "Menu_"), "o menu lista o preset e Gerenciar…")
    t.shot("layouts_menu")
    app.click("Ver", "MainMenuBar")  # fecha o menu
    app.wait_frames(6)

    # mudar os paineis e a Previa
    menu(app, "Ver", "Layout", "Grade grande")
    app.wait_frames(8)
    pick_combo(app, "##previa_fonte", "cmp_previa", "Clip selecionado")
    app.click("##previa_quadriculado", "cmp_previa")
    app.wait_state(lambda s: s["preview"]["mode"] == "clip" and s["preview"]["checker"], "Prévia de volta ao padrão")
    moved = rect_of(app, "cmp_grade")
    expect(not same_rect(moved, ref["cmp_grade"], 20), "o layout Grade grande moveu o painel Composição (%s -> %s)" % (ref["cmp_grade"], moved))

    # aplicar o preset: volta o retangulo dos paineis e as opcoes da Previa
    menu(app, "Ver", "Layout", "Palco A")
    app.wait_frames(10)
    for w in ref:
        expect(same_rect(rect_of(app, w), ref[w], 4), "preset aplicado: painel %s voltou a %s (agora %s)" % (w, ref[w], rect_of(app, w)))
    s = app.state
    expect(s["preview"]["mode"] == "layer" and not s["preview"]["checker"], "preset aplicado: modo e opções da Prévia voltaram")
    expect(s["lastLayoutPreset"] == "Palco A", "o último preset usado é o Palco A")
    t.shot("layouts_preset_aplicado")

    # Alt+1 aplica o primeiro da lista
    menu(app, "Ver", "Layout", "Saída grande")
    app.wait_frames(8)
    expect(not same_rect(rect_of(app, "cmp_grade"), ref["cmp_grade"], 20), "layout pronto diferente do preset")
    app.key("1", "alt")
    app.wait_frames(10)
    expect(same_rect(rect_of(app, "cmp_grade"), ref["cmp_grade"], 4), "Alt+1 aplicou o preset (painel Composição %s)" % rect_of(app, "cmp_grade"))
    expect(app.state["lastLayoutPreset"] == "Palco A", "Alt+1: último preset")
    t.shot("layouts_alt1")

    # outra janela: as proporcoes se mantem (os layouts prontos tambem seguem a janela)
    app.resize_client(1280, 720)
    menu(app, "Ver", "Layout", "Grade grande")
    app.wait_frames(8)
    menu(app, "Ver", "Layout", "Palco A")
    app.wait_frames(12)
    for w in ref_frac:
        got = fractions(app, w)
        expect(all(abs(a - b) < 0.02 for a, b in zip(got, ref_frac[w])),
               "janela 1280x720: painel %s manteve as proporções (%s -> %s)" % (w, tuple(round(v, 3) for v in ref_frac[w]), tuple(round(v, 3) for v in got)))
    t.shot("layouts_1280x720")
    app.resize_client(1600, 900)
    app.wait_frames(8)

    # Gerenciar: renomear e excluir
    menu(app, "Ver", "Layout", "##layout_gerenciar")
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
    menu(app, "Ver", "Layout", "Padrão (media server)")
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
        menu(app, "Ver", "Layout", label)
        app.wait_frames(8)
        t.layout_check("max_" + key)
        t.shot("maximizada_layout_" + key)
    for pos in ("window", "fixed", "system"):
        if pos == "window":
            menu(app, "Ver", "Tamanho da interface", "Acompanhar a janela (recomendado)")
        elif pos == "fixed":
            menu(app, "Ver", "Tamanho da interface", "150%")
        else:
            menu(app, "Ver", "Tamanho da interface", "Automático (escala do Windows)")
        app.wait_frames(6)
        t.shot("maximizada_escala_" + pos)
    menu(app, "Ver", "Tamanho da interface", "Acompanhar a janela (recomendado)")
    menu(app, "Ver", "Layout", "Padrão (media server)")
    app.restore()
    t.shot("janela_restaurada")
    # arrastar a borda (resize continuo): a escala deve variar em passos, sem travar
    app.resize_client(1600, 900)
    s0 = app.scale
    app.drag_resize_border(-300, -160)
    app.wait_frames(6)
    print("      apos arrastar a borda: escala %.2f -> %.2f, fb %s" % (s0, app.scale, app.snapshot["fb"]))
    t.shot("janela_arrastada")
    app.resize_client(1600, 900)


def sc_fuzz(t):
    """Cliques aleatorios (semente fixa) em itens visiveis, nos dois modos: procura crash/assert/conflito."""
    import random
    app = t.app
    rnd = random.Random(1234)
    skip = ("Sair", "Abrir…", "Salvar", "Salvar como…", "Novo", "Abrir recente", "Limpar tudo", "Excluir", "Remover",
            "##caminho", "##map_remover", "##master_limpar",
            # Pixel Map: abrem dialogos nativos do Windows (bloqueiam o app)
            "Gerar padrão HD", "Exportar", "Importar CSV", "Escolher imagem")
    for i in range(160):
        if i % 40 == 0 and app.has("Fechar"):
            app.click("Fechar")
        if i % 40 == 0:
            ensure_mode(app, ("composition", "mapping", "pixelmap")[(i // 40) % 3])
        app.read()
        cands = [it for it in app.items if not it.is_window and it.visible and it.label and it.rect.w > 4 and it.rect.h > 4
                 and not it.label.startswith(skip) and "##menu_" not in it.label and "Debug" not in it.window
                 and "isf_pasta_adicionar" not in it.label and "isf_abrir_principal" not in it.label
                 and "ndi_link_abrir" not in it.label
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
    if app.has("Fechar"):  # janela "Sobre" aberta pelo fuzz
        app.click("Fechar")
    ensure_mode(app, "composition")
    t.shot("depois_do_fuzz")


def sc_fechar(t):
    """Fechar a janela com alteracoes: o modal aparece; Cancelar mantem o app; Nao salvar encerra."""
    app = t.app
    ensure_mode(app, "composition")
    menu(app, "Composição", "+ Coluna")
    app.wait_state(lambda s: s["dirty"], "projeto modificado")
    driver.user32.PostMessageW(app.hwnd, 0x0010, 0, 0)  # WM_CLOSE (o mesmo do X da janela)
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


SCENARIOS = [
    ("modos", sc_modos),
    ("mapeamento", sc_mapeamento),
    ("pixelmap", sc_pixelmap),
    ("grade", sc_grade),
    ("decks", sc_decks),
    ("transporte", sc_transporte),
    ("hot", sc_hot),
    ("clip", sc_clip),
    ("efeitos", sc_efeitos),
    ("geradores", sc_geradores),
    ("projeto", sc_projeto),
    ("ver", sc_ver),
    ("ndi", sc_ndi),
    ("previa", sc_previa),
    ("layouts", sc_layouts),
    ("janela", sc_janela),
    ("fuzz", sc_fuzz),
    ("fechar", sc_fechar),
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
    tag = {"1.0": "100", "1.5": "150", "window": "janela"}.get(args.config, args.config)
    print("== BSL Stage UI QA: config=%s tamanho=%dx%d ==" % (args.config, w, h))
    app = driver.App(EXE, qa_dir, extra, size=(w, h))
    t = Tester(app, tag, args.shots)
    only = [s for s in args.only.split(",") if s]
    try:
        t.shot("inicial")
        for name, fn in SCENARIOS:
            if only and name not in only:
                continue
            print("[%s]" % name)
            if not app.alive():
                t.results.append((name, False, "app nao esta mais rodando"))
                continue
            t.step(name, lambda fn=fn: fn(t))
    finally:
        alive = app.alive()
        code = app.quit()
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
