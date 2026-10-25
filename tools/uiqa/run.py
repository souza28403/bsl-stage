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
                     "Lista", "Propriedades", "cmp_aba_saida")
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
        app.click(MODE_MAP if mode == "mapping" else MODE_COMP, "MainMenuBar")
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
        for mode in ("mapping", "composition"):
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
    c = sl["corners"]
    app.drag(out.x0 + c[0] * ko, out.y0 + c[1] * ko, out.x0 + (c[0] + 90) * ko, out.y0 + (c[1] + 60) * ko)
    s, scr, sl = cur()
    expect(abs(sl["corners"][0] - (c[0] + 90)) < 4 and abs(sl["corners"][1] - (c[1] + 60)) < 4,
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
    app.drag(out.x0 + mx * ko, out.y0 + my * ko, out.x0 + (mx + 40) * ko, out.y0 + (my + 30) * ko)
    s, scr, sl = cur()
    expect(abs(sl["corners"][4] - (c[4] + 40)) < 4, "arrastar o quad inteiro moveu os cantos")
    t.shot("mapeamento_saida")

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


def sc_clip(t):
    app = t.app
    ensure_mode(app, "composition")
    # garante um video na L1C2 e dispara
    if not any(x[0] == 0 and x[1] == 1 for x in app.state["cells"]):
        pick_tab(app, "Arquivos")
        app.drag_items(app.find("h264_1080p.mp4", "cmp_arquivos"), cell(app, 1, 2), steps=18)
        app.wait_state(lambda s: any(x[0] == 0 and x[1] == 1 for x in s["cells"]), "video na L1C2")
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


def sc_geradores(t):
    app = t.app
    ensure_mode(app, "composition")
    pick_tab(app, "Geradores")
    app.wait_frames(3)
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
    pick_tab(app, "Geradores")
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
            "##caminho", "##map_remover", "##master_limpar")
    for i in range(160):
        if i % 40 == 0 and app.has("Fechar"):
            app.click("Fechar")
        if i % 40 == 0:
            ensure_mode(app, "mapping" if (i // 40) % 2 else "composition")
        app.read()
        cands = [it for it in app.items if not it.is_window and it.visible and it.label and it.rect.w > 4 and it.rect.h > 4
                 and not it.label.startswith(skip) and "##menu_" not in it.label and "Debug" not in it.window
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
    ("grade", sc_grade),
    ("decks", sc_decks),
    ("transporte", sc_transporte),
    ("hot", sc_hot),
    ("clip", sc_clip),
    ("geradores", sc_geradores),
    ("projeto", sc_projeto),
    ("ver", sc_ver),
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
    args = ap.parse_args()

    w, h = (int(v) for v in args.size.split("x"))
    qa_dir = os.path.join(ROOT, "build", "qa", "cfg_" + args.config)
    extra = ["--ui-mode", "window"] if args.config == "window" else ["--ui-scale", args.config]
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
