"""Screenshots de documentacao do editor de mapeamento (1920x1080):

  docs/screenshot-mapeamento-transformar.png  slice girado, com as alcas de redimensionar e a de rotacao
  docs/screenshot-mapeamento-ima.png          arraste com o imã encaixando, com as linhas-guia vermelhas visiveis

Uso:  py -3 tools/uiqa/mapshots.py

Abre o app com o demo, monta os slices pelos campos das Propriedades e opera o mouse de verdade. O screenshot do imã sai
com o botao do mouse AINDA apertado (parametro 'probe' do driver.drag): as guias so existem durante o arraste.
"""
import math
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import driver

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
EXE = os.path.join(ROOT, "build", "Release", "BSLStage.exe")
MEDIA = os.path.join(ROOT, "testmedia")
DOCS = os.path.join(ROOT, "docs")


def type_field(app, label, value):
    app.click(label, "Propriedades")
    app.key("a", "ctrl")
    app.type_text(str(value))
    app.key("enter")
    app.wait_frames(3)


def main():
    qa_dir = os.path.join(ROOT, "build", "qa", "mapshots")
    app = driver.App(EXE, qa_dir, ["--demo", MEDIA, "--trigger", "1", "1", "--mode", "mapping", "--output-window",
                                   "--ui-mode", "window"], size=(1920, 1080))
    try:
        time.sleep(6.0)
        app.wait_frames(10)
        st = app.read()["state"]
        sw = st["screens"][0]["w"]
        sh = st["screens"][0]["h"]

        def P(x, y):
            r = app.find("saida:imagem").rect
            return r.x0 + x * r.w / sw, r.y0 + y * r.w / sw

        def corners(i):
            s = app.read()["state"]
            return s["screens"][s["mapping"]["screen"]]["slices"][i]["corners"]

        app.click("Resetar warp", "Propriedades")
        for lbl, v in (("##ox", 760), ("##oy", 88), ("##ow", 480), ("##oh", 300)):
            type_field(app, lbl, v)
        app.click_at(*P(200, 640))
        app.wait_state(lambda s: s["mapping"]["multi"] == [0], "A selecionado")
        for lbl, v in (("##ox", 80), ("##oy", 80), ("##ow", 520), ("##oh", 300)):
            type_field(app, lbl, v)
        app.click_at(*P(1000, 240))
        app.wait_state(lambda s: s["mapping"]["multi"] == [1], "B selecionado")

        c = corners(1)
        gap = c[0] - 600.0
        cx, cy = (c[0] + c[2]) / 2, (c[1] + c[5]) / 2

        def probe():
            m = app.read()["state"]["mapping"]
            path = os.path.join(DOCS, "screenshot-mapeamento-ima.png")
            app.screenshot(path)
            print("ok", path, "guias:", m["guides"])
            return m["guides"]

        guides = app.drag(*P(cx, cy), *P(cx - (gap - 6.0), cy), probe=probe)
        if not guides:
            raise driver.QaFailure("nenhuma linha-guia durante o arraste (o screenshot do imã ficaria sem elas)")
        c = corners(1)
        print("B depois do encaixe: x=%.1f y=%.1f (A termina em x=600, topo y=80)" % (c[0], c[1]))
        app.key("z", "ctrl")

        c = corners(1)
        bx0, bx1 = min(c[0::2]), max(c[0::2])
        by0, by1 = min(c[1::2]), max(c[1::2])
        ctr = P((bx0 + bx1) / 2, (by0 + by1) / 2)
        top = P((bx0 + bx1) / 2, by0)
        hx, hy = top[0], top[1] - 26 * app.scale
        r = math.hypot(hx - ctr[0], hy - ctr[1])
        ang = math.radians(-12.0)
        app.drag(hx, hy, ctr[0] + r * math.sin(ang), ctr[1] - r * math.cos(ang))
        app.hover(960, 1075)
        app.wait_frames(6)
        path = os.path.join(DOCS, "screenshot-mapeamento-transformar.png")
        app.screenshot(path)
        print("ok", path)
    finally:
        app.quit()


if __name__ == "__main__":
    main()
