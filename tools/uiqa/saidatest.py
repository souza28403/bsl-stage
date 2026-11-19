import ctypes
import ctypes.wintypes as w
import json
import os
import shutil
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
import driver  # noqa: E402
from PIL import ImageChops, ImageGrab, ImageStat  # noqa: E402

EXE = os.path.join(ROOT, "build", "Release", "BSLStage.exe")
WORK = os.path.join(tempfile.gettempdir(), "bslstage-saidatest")
u = ctypes.windll.user32
u.SetProcessDPIAware()
results = []


def monitors():
    found = []
    cb_type = ctypes.WINFUNCTYPE(ctypes.c_int, w.HMONITOR, w.HDC, ctypes.POINTER(w.RECT), w.LPARAM)

    def cb(h, d, r, l):
        found.append((r.contents.left, r.contents.top, r.contents.right, r.contents.bottom))
        return 1

    u.EnumDisplayMonitors(0, 0, cb_type(cb), 0)
    return found


def check(ok, what):
    results.append((ok, what))
    print(("OK    " if ok else "FALHA ") + what, flush=True)


def write_project(path, clip, slices):
    sl = []
    for i in range(slices):
        sl.append({"name": "Slice %d" % (i + 1), "input": {"x": 0, "y": 0, "w": 1920, "h": 1080},
                   "corners": [{"x": 0, "y": 0}, {"x": 1920, "y": 0}, {"x": 1920, "y": 1080}, {"x": 0, "y": 1080}]})
    data = {
        "format": "bslstage", "version": 1,
        "composition": {"width": 1920, "height": 1080, "fps": 60, "bpm": 120,
                        "master": {"opacity": 1, "bypass": False}, "currentDeck": 0},
        "layers": [{"name": "Fundo", "opacity": 1, "blend": "normal", "bypass": False, "solo": False}],
        "decks": [{"name": "Show", "columns": 4, "clips": [
            {"layer": 0, "column": 0, "path": clip.replace("\\", "/"), "speed": 1, "loopMode": "loop", "fitMode": "fill"}]}],
        "output": {"screens": [{"name": "Tela 1", "kind": "monitor", "monitor": {"index": 1, "name": ""},
                                "width": 1920, "height": 1080, "slices": sl}]},
    }
    with open(path, "w", encoding="utf-8") as f:
        json.dump(data, f)


class Output:
    def __init__(self, rect):
        self.rect = rect

    def grab(self):
        return ImageGrab.grab(bbox=self.rect, all_screens=True).convert("RGB")

    def mean(self):
        return ImageStat.Stat(self.grab().convert("L")).mean[0]

    def changes(self, label, seconds=3.0):
        shots = []
        t0 = time.time()
        while time.time() - t0 < seconds:
            shots.append(self.grab())
            time.sleep(0.4)
        changed = sum(1 for a, b in zip(shots, shots[1:])
                      if max(ImageChops.difference(a, b).convert("L").getextrema()) > 20)
        check(changed >= len(shots) // 2, "%s: imagem mudou em %d de %d capturas" % (label, changed, len(shots) - 1))


def start(proj, name):
    return driver.App(EXE, os.path.join(WORK, "qa_" + name), args=[proj, "--trigger", "1", "1"],
                      size=(1400, 850), pos=(60, 60))


def app_log(app):
    try:
        with open(os.path.join(app.dir, "appdata", "bslstage.log"), encoding="utf-8", errors="replace") as f:
            return f.read()
    except OSError:
        return ""


def owner_at(x, y):
    root = u.GetAncestor(u.WindowFromPoint(w.POINT(x, y)), 2)
    pid = w.DWORD()
    u.GetWindowThreadProcessId(root, ctypes.byref(pid))
    cls = ctypes.create_unicode_buffer(64)
    u.GetClassNameW(root, cls, 64)
    return pid.value, cls.value


def brightness(out, slices):
    proj = os.path.join(WORK, "foto_%d.bslstage" % slices)
    write_project(proj, os.path.join(ROOT, "testmedia", "person.jpg"), slices)
    app = start(proj, "foto%d" % slices)
    try:
        time.sleep(4)
        check("aberta em" in app_log(app), "saida abriu no segundo monitor (%d slice(s))" % slices)
        return out.mean()
    finally:
        app.quit()
        time.sleep(1.5)


def playing(out):
    proj = os.path.join(WORK, "video.bslstage")
    write_project(proj, os.path.join(ROOT, "testmedia", "h264_1080p.mp4"), 2)
    app = start(proj, "video")
    front = None
    try:
        time.sleep(4)
        out.changes("normal")

        app.focus()
        app.key("o", "ctrl")
        try:
            app.find("Não salvar", "Salvar", timeout=4.0)
            app.click("Não salvar", "Salvar")
        except Exception:
            pass
        dlg = app.find_dialog()
        check(dlg != 0, "janela Abrir projeto apareceu")
        if dlg:
            out.changes("com a janela Abrir aberta", 4.0)
            app.dialog_close(dlg)
            app.wait_frames(6)

        u.PostMessageW(app.hwnd, 0x0112, 0xF010, 0)
        time.sleep(0.5)
        out.changes("arrastando a janela principal", 4.0)
        app.key("esc", wait=False)
        time.sleep(0.5)
        app.wait_frames(6)

        cx = (out.rect[0] + out.rect[2]) // 2
        cy = (out.rect[1] + out.rect[3]) // 2
        front = driver.ForeignWindow("BSL QA - janela na frente", (cx - 400, cy - 300, 800, 600))
        front.activate()
        for _ in range(15):
            front.pump()
            time.sleep(0.1)
        check(front.is_foreground(), "janela de teste ficou na frente")
        pid, cls = owner_at(cx, cy)
        check(pid == app.proc.pid, "saida por cima da janela de teste (no centro: %s)" % cls)
        pid, cls = owner_at(cx, out.rect[3] - 10)
        check(pid == app.proc.pid, "saida por cima da barra de tarefas (embaixo: %s)" % cls)
        out.changes("com outra janela na frente")
        front.close()
        front = None

        warns = [l for l in app_log(app).splitlines() if l.startswith("[aviso]")]
        check(not any("parada" in l for l in warns), "registro sem apresentacao parada")
        for l in warns[-8:]:
            print("  " + l)
    finally:
        if front:
            front.close()
        app.quit()
        time.sleep(1.5)


def main():
    rects = monitors()
    second = [m for m in rects if not (m[0] == 0 and m[1] == 0)]
    if len(rects) < 2 or not second:
        print("PULADO: so ha um monitor")
        return 2
    if not os.path.exists(EXE):
        print("FALHA BSLStage.exe nao encontrado: " + EXE)
        return 1
    shutil.rmtree(WORK, ignore_errors=True)
    os.makedirs(WORK, exist_ok=True)
    out = Output(second[0])
    print("segundo monitor %s" % (out.rect,), flush=True)
    try:
        one = brightness(out, 1)
        two = brightness(out, 2)
        print("brilho medio: 1 slice %.1f, 2 slices %.1f" % (one, two))
        check(one > 5 and abs(two - one) <= max(2.0, one * 0.03), "dois slices iguais sobrepostos com o brilho de um")
        playing(out)
    except Exception as e:
        check(False, "excecao: %r" % (e,))
    finally:
        driver.stop_all()
    fails = [r for r in results if not r[0]]
    print("\n%d verificacoes, %d falha(s)" % (len(results), len(fails)))
    if not fails:
        shutil.rmtree(WORK, ignore_errors=True)
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
