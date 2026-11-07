"""Screenshot de documentação do modo Pixel Map (1920x1080):

  docs/screenshot-pixelmap.png   modo Pixel Map com os 3 painéis do exemplo: "Testeira Palco" 2048x128 em 0,0 e as
                                 laterais 512x512 em 0,128 e 1536,128 (input 1920x1080, físico 2048x640)

Uso:  py -3 tools/uiqa/pmshots.py

Abre o app (--qa) já no modo Pixel Map e monta os painéis pela interface, com mouse e teclado de verdade, como no
cenário "pixelmap" do run.py.
"""
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import driver

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
EXE = os.path.join(ROOT, "build", "Release", "BSLStage.exe")
DOCS = os.path.join(ROOT, "docs")


def pm(app):
    return app.read()["state"]["pixelmap"]


def type_field(app, label, value, window="pm_config"):
    it = app.find(label, window)
    app.click_at(it.rect.cx, it.rect.cy)
    app.key("a", "ctrl")
    app.type_text(str(value))
    app.key("enter")
    app.wait_frames(3)


def add_panel(app, name, w, h, x, y):
    it = app.find("##pm_nome", "pm_config")
    app.click_at(it.rect.cx, it.rect.cy)
    app.key("a", "ctrl")
    app.type_text(name)
    app.wait_frames(3)
    for lbl, v in (("##pm_dw", w), ("##pm_dh", h), ("##pm_dx", x), ("##pm_dy", y)):
        type_field(app, lbl, v)
    n = len(pm(app)["panels"])
    app.click("Adicionar painel", "pm_config")
    app.wait_state(lambda s: len(s["pixelmap"]["panels"]) == n + 1, "painel adicionado")


def main():
    qa_dir = os.path.join(ROOT, "build", "qa", "pmshots")
    app = driver.App(EXE, qa_dir, ["--mode", "pixelmap", "--ui-mode", "window"], size=(1920, 1080))
    try:
        time.sleep(3.0)
        app.wait_frames(10)
        add_panel(app, "Testeira Palco", 2048, 128, 0, 0)
        add_panel(app, "Lateral Esquerda", 512, 512, 0, 128)
        add_panel(app, "Lateral Direita", 512, 512, 1536, 128)
        r = app.find("pixelmap:painel:1").rect
        app.click_at(r.cx, r.cy)
        app.wait_state(lambda s: s["pixelmap"]["selected"] == 1, "painel selecionado")
        app.hover(960, 1075)
        app.wait_frames(8)
        panels = pm(app)["panels"]
        assert [p["phys"] for p in panels] == [[0, 0, 2048, 128], [0, 128, 512, 512], [1536, 128, 512, 512]], panels
        path = os.path.join(DOCS, "screenshot-pixelmap.png")
        app.screenshot(path)
        print("ok", path)
    finally:
        app.quit()


if __name__ == "__main__":
    main()
