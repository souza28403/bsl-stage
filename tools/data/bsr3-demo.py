"""Gera o projeto do screenshot docs/screenshot-bsl-nativos-b.png: uma foto com o BSL Slice Refiner (BSR3) aplicado a 3 slices.

Uso: py -3 tools/data/bsr3-demo.py <saida.bslstage>
Depois: BSLStage.exe <saida.bslstage> --trigger 1 1 --tab efeitos --window-size 1920 1080 --screenshot docs/screenshot-bsl-nativos-b.png 8
"""
import json
import os
import sys

out = sys.argv[1] if len(sys.argv) > 1 else "bsr3-demo.bslstage"
PHOTO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "testmedia", "photo.jpg")).replace("\\", "/")
screen = "Painel LED"
slices = [("Coluna 1", 160, 200, 560, 680), ("Coluna 2", 720, 200, 480, 680), ("Torre", 1300, 300, 460, 480)]
ids = [f"{screen}/{name}#{i}" for i, (name, *_rest) in enumerate(slices)]


def group(name, color, color_b, effect, merge, smooth, size):
    return {
        "name": name, "enabled": True, "fill": "video", "masterSize": size, "corners": [1, 1, 1, 1], "equalCorners": True,
        "offset": 0, "merge": merge, "mergeSmooth": smooth, "softEdge": 0,
        "stroke": {"width": 10, "mode": "effect", "color": color, "colorB": color_b, "effect": effect, "speed": 0.8,
                   "variation": 0.5, "smooth": True, "symX": False, "symY": False, "glow": 0.55,
                   "dual": {"on": False, "width": 3, "gap": 8}},
        "blinker": {"mode": "off", "rate": 2},
        "bounce": {"mode": "off", "speed": 1, "beats": 1, "min": 0.85, "multi": False, "smooth": True},
        "colorRotate": {"on": False, "speed": 0.2, "bars": 0},
    }


state = {
    "v": 1,
    "assign": {ids[0]: 0, ids[1]: 0, ids[2]: 1},
    "groups": [group("Parede", "#ff3b3b", "#ffd23f", "rainbow", True, 40, 0.35),
               group("Torre", "#27d3e6", "#a66bff", "chaser", False, 0, 0.2)]
    + [group(f"Grupo {i + 1}", "#ffffff", "#000000", "noise", True, 0, 0.25) for i in range(2, 8)],
}

project = {
    "format": "bslstage",
    "version": 1,
    "composition": {"width": 1920, "height": 1080, "fps": 60, "bpm": 124, "master": {"opacity": 1, "bypass": False}, "currentDeck": 0},
    "layers": [{"name": "Fundo", "opacity": 1, "blend": "normal", "bypass": False, "solo": False}],
    "decks": [{
        "name": "Show", "columns": 4,
        "clips": [{
            "layer": 0, "column": 0, "path": PHOTO,
            "speed": 1, "loopMode": "loop", "fitMode": "fit",
            "effects": [{"type": "bsl:slice-refiner", "bypass": False, "params": {"state": state, "blackBg": True}}],
        }],
    }],
    "output": {"screens": [{
        "name": screen, "kind": "disabled", "width": 1920, "height": 1080,
        "slices": [{"name": name, "input": {"x": x, "y": y, "w": w, "h": h}} for name, x, y, w, h in slices],
    }]},
}
with open(out, "w", encoding="utf-8") as f:
    json.dump(project, f, ensure_ascii=False, indent=1)
print("ok", out)
