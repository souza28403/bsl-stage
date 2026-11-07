"""Screenshots de documentacao (docs/screenshot-composicao.png, screenshot-clip-texto.png, screenshot-mapeamento.png), 1920x1080.

Uso:  py -3 tools/uiqa/docshots.py

Abre o app com layout limpo (--qa), o demo em testmedia/ e clica na aba do painel que deve aparecer em foco (o contorno
vermelho BSL so existe no painel com foco), depois pede o screenshot pelo comando "shot" do app.
"""
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import driver
from run import (cell, clear_field_and_type, ensure_mode, expect, menu, pick_combo, pick_tab,
                 scroll_into_view)

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
EXE = os.path.join(ROOT, "build", "Release", "BSLStage.exe")
MEDIA = os.path.join(ROOT, "testmedia")
DOCS = os.path.join(ROOT, "docs")


def focus_tab(app, name, top=True):
    """Clica na aba 'name' (a de cima da janela quando ha duas com o mesmo nome) para dar foco ao painel."""
    app.read()
    tabs = [i for i in app.items if i.window == "dock-tabs" and i.label == "tab:" + name and i.visible]
    if not tabs:
        raise driver.QaFailure("aba nao encontrada: " + name)
    tabs.sort(key=lambda i: i.rect.cy)
    it = tabs[0] if top else tabs[-1]
    app.click_at(it.rect.cx, it.rect.cy)
    app.wait_frames(4)


def shot(name, extra, tab, top=True, settle=8.0):
    qa_dir = os.path.join(ROOT, "build", "qa", "docshots")
    app = driver.App(EXE, qa_dir, ["--demo", MEDIA] + extra, size=(1920, 1080))
    try:
        time.sleep(settle)
        app.wait_frames(10)
        focus_tab(app, tab, top)
        app.hover(960, 1075)
        app.wait_frames(6)
        path = os.path.join(DOCS, name)
        app.screenshot(path)
        print("ok", path)
    finally:
        app.quit()


def previa(name="screenshot-previa.png"):
    """Prévia com o clip selecionado diferente do que está ao vivo: o nome da célula (Gradiente animado, camada 3)
    só vai para a Prévia; a saída segue com o que a coluna 1 do demo dispara."""
    qa_dir = os.path.join(ROOT, "build", "qa", "docshots")
    app = driver.App(EXE, qa_dir, ["--demo", MEDIA], size=(1920, 1080))
    try:
        time.sleep(8.0)
        app.wait_frames(10)
        app.click("celula L3 C3 nome")
        app.wait_state(lambda s: s["preview"]["own"] and not s["preview"]["live"], "Prévia com instância própria", timeout=10)
        time.sleep(1.5)
        app.hover(960, 1075)
        app.wait_frames(6)
        path = os.path.join(DOCS, name)
        app.screenshot(path)
        print("ok", path)
    finally:
        app.quit()


def exibir(name="screenshot-exibir.png"):
    """Menu Exibir aberto: painel Clip oculto (caixa desmarcada) e o submenu "Soltar em janela" aberto."""
    qa_dir = os.path.join(ROOT, "build", "qa", "docshots")
    app = driver.App(EXE, qa_dir, ["--demo", MEDIA], size=(1920, 1080))
    try:
        time.sleep(8.0)
        app.wait_frames(10)
        menu(app, "Exibir", "##exibir_clip")
        app.wait_state(lambda s: s["view"]["hidden"] == ["clip"], "painel Clip oculto")
        app.wait_frames(8)
        app.click("Exibir", "MainMenuBar")
        app.wait_frames(6)
        it = app.find("##exibir_soltar", "Menu_")
        app.hover(it.rect.cx, it.rect.cy)
        app.wait_frames(10)
        path = os.path.join(DOCS, name)
        app.screenshot(path)
        print("ok", path)
    finally:
        app.quit()


def default_ndi_dll():
    for var in ("NDI_RUNTIME_DIR_V6", "NDI_RUNTIME_DIR_V5"):
        folder = os.environ.get(var)
        if folder:
            candidate = os.path.join(folder, "Processing.NDI.Lib.x64.dll")
            if os.path.isfile(candidate):
                return candidate
    return None


NDI_DLL = default_ndi_dll()


def ndi(name="screenshot-ndi.png", dll=NDI_DLL):
    """Fonte NDI recebendo a propria saida NDI do app (loopback) + janela Compatibilidade de video.
    O runtime do NDI e so carregado de 'dll' (--ndi-lib): nada e copiado para o projeto. A fonte NDI fica na camada de baixo
    e o padrao de teste na de cima (opaco): sem realimentacao, a Previa mostra o que chegou pela rede."""
    if not dll:
        print("pulado: sem runtime do NDI (defina NDI_RUNTIME_DIR_V6 ou passe o caminho da DLL)")
        return
    qa_dir = os.path.join(ROOT, "build", "qa", "docshots")
    app = driver.App(EXE, qa_dir, ["--ndi-lib", dll], size=(1920, 1080))
    try:
        time.sleep(3.0)
        app.wait_frames(10)
        expect(app.read()["state"]["ndi"]["available"], "runtime do NDI carregado de " + dll)
        ensure_mode(app, "composition")
        pick_tab(app, "Fontes")
        app.drag_items(scroll_into_view(app, "Padrão de teste", "cmp_geradores"), cell(app, 2, 1), steps=18)
        app.wait_state(lambda s: any(x[0] == 1 and x[1] == 0 for x in s["cells"]), "padrao de teste na grade")
        app.click("celula L2 C1")
        app.wait_state(lambda s: s["layers"][1]["activeColumn"] == 0, "padrao de teste na saida")
        ensure_mode(app, "mapping")
        pick_combo(app, "##destino", "Propriedades", "NDI")
        app.wait_state(lambda s: s["screens"][s["mapping"]["screen"]]["kind"] == "NDI", "destino NDI")
        ensure_mode(app, "composition")
        pick_tab(app, "Fontes")
        app.drag_items(scroll_into_view(app, "NDI", "cmp_geradores"), cell(app, 1, 1), steps=18)
        app.wait_state(lambda s: any(x[0] == 0 and x[1] == 0 and x[2].startswith("NDI") for x in s["cells"]), "fonte NDI na grade")
        app.click("celula L1 C1 nome")
        app.wait_state(lambda s: s["selection"]["layer"] == 0 and s["selection"]["column"] == 0, "seleciona a fonte NDI")
        s = app.wait_state(lambda s: any(n.endswith("(BSL Stage - Tela 1)") for n in s["ndi"]["sources"]),
                           "a saida NDI aparece na lista de fontes", timeout=20)
        full = [n for n in s["ndi"]["sources"] if n.endswith("(BSL Stage - Tela 1)")][0]
        app.click("##ndi_fonte", "cmp_clip")
        app.wait_frames(4)
        app.click(full, "Combo")
        app.wait_state(lambda s: s["selClip"]["params"].get("source") == full, "fonte NDI escolhida")
        app.click("celula L1 C1")
        app.wait_state(lambda s: s["layers"][0]["activeColumn"] == 0, "dispara a fonte NDI")
        app.wait_state(lambda s: s["selClip"]["status"].startswith("NDI:"), "fonte NDI recebendo a propria saida", timeout=25)
        app.wait_state(lambda s: any(o["connections"] >= 1 for o in s["ndi"]["outputs"]), "saida NDI com receptor", timeout=10)
        app.click("celula L1 C1 nome")
        app.wait_frames(30)
        menu(app, "Exibir", "##menu_compat")
        app.wait_frames(10)
        expect(app.has("compat:ndi_status", "janela_compat"), "janela de compatibilidade aberta")
        time.sleep(1.0)
        app.hover(960, 1075)
        app.wait_frames(8)
        path = os.path.join(DOCS, name)
        app.screenshot(path)
        print("ok", path)
    finally:
        app.quit()


def main():
    only = sys.argv[1:]
    if only == ["previa"]:
        previa()
        return
    if only == ["exibir"]:
        exibir()
        return
    if only and only[0] == "ndi":
        ndi(dll=only[1] if len(only) > 1 else NDI_DLL)
        return
    shot("screenshot-composicao.png", [], "Composição", top=True)
    shot("screenshot-clip-texto.png", ["--trigger", "3", "1"], "Clip")
    shot("screenshot-mapeamento.png", ["--mode", "mapping", "--output-window"], "Propriedades")
    previa()


if __name__ == "__main__":
    main()
