import os
import shutil
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import driver
from run import expect, menu, pick_tab

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
EXE = os.path.join(ROOT, "build", "Release", "BSLStage.exe")
DOCS = os.path.join(ROOT, "docs")


def main():
    qa_dir = os.path.join(ROOT, "build", "qa", "pluginshots")
    shutil.rmtree(qa_dir, ignore_errors=True)
    app = driver.App(EXE, qa_dir, ["--ui-mode", "window"], size=(1600, 900))
    try:
        pick_tab(app, "Efeitos")
        menu(app, "Arquivo", "##menu_novo_plugin")
        app.wait_state(lambda s: s["plugins"]["wizardOpen"], "assistente aberto", timeout=4)
        app.click("##plug_nome")
        app.key("a", "ctrl")
        app.type_text("Onda colorida")
        app.click("##plug_modelo_2")
        app.wait_state(lambda s: s["plugins"]["rows"] == 4, "modelo Distorcao", timeout=3)
        app.hover(20, 880)
        app.wait_frames(8)
        path = app.screenshot(os.path.join(DOCS, "screenshot-novo-plugin.png"))
        expect(os.path.isfile(path), "captura gravada")
        print("ok", path)
    finally:
        app.quit()


if __name__ == "__main__":
    main()
