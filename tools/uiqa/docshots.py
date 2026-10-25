"""Screenshots de documentacao do visual "Adobe" (docs/screenshot-adobe-*.png), 1920x1080.

Uso:  py -3 tools/uiqa/docshots.py

Abre o app com layout limpo (--qa), o demo em testmedia/ e clica na aba do painel que deve aparecer em foco (o contorno
vermelho BSL so existe no painel com foco), depois pede o screenshot pelo comando "shot" do app.
"""
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import driver  # noqa: E402

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
        time.sleep(settle)  # fontes ao vivo, PDF, Office e Web precisam de alguns segundos
        app.wait_frames(10)
        focus_tab(app, tab, top)
        app.hover(960, 1075)  # mouse fora dos controles
        app.wait_frames(6)
        path = os.path.join(DOCS, name)
        app.screenshot(path)
        print("ok", path)
    finally:
        app.quit()


def main():
    shot("screenshot-adobe-composicao.png", [], "Composição", top=True)
    shot("screenshot-adobe-clip-texto.png", ["--trigger", "3", "1"], "Clip")
    shot("screenshot-adobe-mapeamento.png", ["--mode", "mapping", "--output-window"], "Propriedades")


if __name__ == "__main__":
    main()
