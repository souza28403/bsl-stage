"""Driver de teste de interface do BSL Stage (Windows).

Abre o BSLStage.exe com --qa <pasta>, le o snapshot.json que o app grava (itens do ImGui com janela, rotulo e
retangulo, mais o estado da aplicacao) e envia mouse/teclado REAIS com SendInput. Os testes clicam pelo
rotulo do item, nunca por coordenada fixa. Ver run.py para o roteiro e README.md para o uso.
"""
import ctypes
import json
import os
import shutil
import subprocess
import sys
import time
from ctypes import wintypes

user32 = ctypes.WinDLL("user32", use_last_error=True)
kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
shell32 = ctypes.WinDLL("shell32", use_last_error=True)

try:
    ctypes.windll.user32.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))
except Exception:
    pass

INPUT_MOUSE, INPUT_KEYBOARD = 0, 1
MOUSEEVENTF_LEFTDOWN, MOUSEEVENTF_LEFTUP = 0x0002, 0x0004
MOUSEEVENTF_RIGHTDOWN, MOUSEEVENTF_RIGHTUP = 0x0008, 0x0010
MOUSEEVENTF_MIDDLEDOWN, MOUSEEVENTF_MIDDLEUP = 0x0020, 0x0040
MOUSEEVENTF_WHEEL = 0x0800
KEYEVENTF_EXTENDEDKEY, KEYEVENTF_KEYUP, KEYEVENTF_UNICODE = 0x0001, 0x0002, 0x0004
SW_RESTORE, SW_MAXIMIZE = 9, 3

VK = {
    "ctrl": 0x11, "shift": 0x10, "alt": 0x12, "enter": 0x0D, "esc": 0x1B, "tab": 0x09, "delete": 0x2E,
    "left": 0x25, "up": 0x26, "right": 0x27, "down": 0x28, "backspace": 0x08, "home": 0x24, "end": 0x23,
    "space": 0x20, "f4": 0x73,
}
VK.update({"f%d" % n: 0x6F + n for n in range(1, 13)})
EXTENDED = {0x25, 0x26, 0x27, 0x28, 0x2E, 0x24, 0x23}

ULONG_PTR = ctypes.c_size_t


class MOUSEINPUT(ctypes.Structure):
    _fields_ = [("dx", wintypes.LONG), ("dy", wintypes.LONG), ("mouseData", wintypes.DWORD),
                ("dwFlags", wintypes.DWORD), ("time", wintypes.DWORD), ("dwExtraInfo", ULONG_PTR)]


class KEYBDINPUT(ctypes.Structure):
    _fields_ = [("wVk", wintypes.WORD), ("wScan", wintypes.WORD), ("dwFlags", wintypes.DWORD),
                ("time", wintypes.DWORD), ("dwExtraInfo", ULONG_PTR)]


class HARDWAREINPUT(ctypes.Structure):
    _fields_ = [("uMsg", wintypes.DWORD), ("wParamL", wintypes.WORD), ("wParamH", wintypes.WORD)]


class _INPUTUNION(ctypes.Union):
    _fields_ = [("mi", MOUSEINPUT), ("ki", KEYBDINPUT), ("hi", HARDWAREINPUT)]


class INPUT(ctypes.Structure):
    _fields_ = [("type", wintypes.DWORD), ("u", _INPUTUNION)]


def _send(inp):
    n = user32.SendInput(1, ctypes.byref(inp), ctypes.sizeof(INPUT))
    if n != 1:
        raise OSError("SendInput falhou: %d" % ctypes.get_last_error())


def _mouse(flags, data=0):
    i = INPUT(type=INPUT_MOUSE)
    i.u.mi = MOUSEINPUT(0, 0, data, flags, 0, 0)
    _send(i)


def _key(vk, up=False, unicode_char=None):
    i = INPUT(type=INPUT_KEYBOARD)
    if unicode_char is not None:
        i.u.ki = KEYBDINPUT(0, ord(unicode_char), KEYEVENTF_UNICODE | (KEYEVENTF_KEYUP if up else 0), 0, 0)
    else:
        flags = (KEYEVENTF_KEYUP if up else 0) | (KEYEVENTF_EXTENDEDKEY if vk in EXTENDED else 0)
        scan = user32.MapVirtualKeyW(vk, 0)
        i.u.ki = KEYBDINPUT(vk, scan, flags, 0, 0)
    _send(i)


class QaFailure(Exception):
    pass


class ForeignWindow:
    WS_OVERLAPPEDWINDOW, WS_VISIBLE, ES_MULTILINE, ES_AUTOVSCROLL = 0x00CF0000, 0x10000000, 0x0004, 0x0040
    WM_NULL, SMTO_ABORTIFHUNG, SMTO_BLOCK = 0x0000, 0x0002, 0x0001

    def __init__(self, title="BSL Stage QA - segundo plano", rect=(60, 60, 560, 320)):
        user32.CreateWindowExW.restype = wintypes.HWND
        user32.CreateWindowExW.argtypes = [wintypes.DWORD, wintypes.LPCWSTR, wintypes.LPCWSTR, wintypes.DWORD, ctypes.c_int,
                                           ctypes.c_int, ctypes.c_int, ctypes.c_int, wintypes.HWND, wintypes.HMENU,
                                           wintypes.HINSTANCE, ctypes.c_void_p]
        user32.GetForegroundWindow.restype = wintypes.HWND
        user32.SendMessageTimeoutW.restype = ctypes.c_size_t
        user32.SendMessageTimeoutW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM, wintypes.UINT,
                                               wintypes.UINT, ctypes.c_void_p]
        user32.IsHungAppWindow.argtypes = [wintypes.HWND]
        x, y, w, h = rect
        style = self.WS_OVERLAPPEDWINDOW | self.WS_VISIBLE | self.ES_MULTILINE | self.ES_AUTOVSCROLL
        self.hwnd = user32.CreateWindowExW(0, "EDIT", title, style, x, y, w, h, None, None, None, None)
        if not self.hwnd:
            raise QaFailure("nao foi possivel criar a janela de teste do segundo plano")
        self.pump()

    def pump(self):
        msg = wintypes.MSG()
        while user32.PeekMessageW(ctypes.byref(msg), None, 0, 0, 1):
            user32.TranslateMessage(ctypes.byref(msg))
            user32.DispatchMessageW(ctypes.byref(msg))

    def activate(self, timeout=3.0):
        t0 = time.time()
        while time.time() - t0 < timeout:
            fg = user32.GetForegroundWindow()
            cur = kernel32.GetCurrentThreadId()
            fg_thread = user32.GetWindowThreadProcessId(fg, None) if fg else 0
            if fg_thread and fg_thread != cur:
                user32.AttachThreadInput(cur, fg_thread, True)
            user32.SetForegroundWindow(self.hwnd)
            if fg_thread and fg_thread != cur:
                user32.AttachThreadInput(cur, fg_thread, False)
            self.pump()
            time.sleep(0.1)
            if user32.GetForegroundWindow() == self.hwnd:
                return True
        return False

    def is_foreground(self):
        return user32.GetForegroundWindow() == self.hwnd

    def text(self):
        buf = ctypes.create_unicode_buffer(4096)
        user32.GetWindowTextW(self.hwnd, buf, 4096)
        return buf.value

    def close(self):
        if self.hwnd:
            user32.DestroyWindow(self.hwnd)
            self.hwnd = 0


def ping_window(hwnd, timeout_ms=1500):
    t0 = time.time()
    result = ctypes.c_size_t()
    ok = user32.SendMessageTimeoutW(hwnd, ForeignWindow.WM_NULL, 0, 0, ForeignWindow.SMTO_ABORTIFHUNG | ForeignWindow.SMTO_BLOCK,
                                    timeout_ms, ctypes.byref(result))
    return bool(ok), (time.time() - t0) * 1000.0, bool(user32.IsHungAppWindow(hwnd))


class Rect:
    def __init__(self, r):
        self.x0, self.y0, self.x1, self.y1 = r

    @property
    def cx(self):
        return (self.x0 + self.x1) / 2

    @property
    def cy(self):
        return (self.y0 + self.y1) / 2

    @property
    def w(self):
        return self.x1 - self.x0

    @property
    def h(self):
        return self.y1 - self.y0

    def __repr__(self):
        return "Rect(%.0f,%.0f,%.0f,%.0f)" % (self.x0, self.y0, self.x1, self.y1)


class Item:
    def __init__(self, d):
        self.window = d["w"]
        self.label = d["l"]
        self.rect = Rect(d["r"])
        self.visible = bool(d["v"])
        self.is_window = bool(d["win"])
        self.disabled = bool(d["dis"])

    def __repr__(self):
        return "<%s | %s %r>" % (self.window, self.label, self.rect)


def _matches(item_label, query):
    """Rotulo igual; "Texto" casa com "Texto##id"; "##id" casa com qualquer rotulo que termine em "##id"
    (botoes de icone e itens "<icone>  Texto##id" sao clicados pelo ID estavel, nao pelo texto)."""
    if item_label == query:
        return True
    if "#" not in query and "##" in item_label and item_label.split("##")[0] == query:
        return True
    if query.startswith("##") and item_label.endswith(query):
        return True
    return False


class App:
    """Uma instancia do BSLStage controlada por input real."""

    def __init__(self, exe, qa_dir, args=(), size=(1600, 900), pos=(40, 40), keep=False):
        self.exe = exe
        self.dir = qa_dir
        if not keep:
            shutil.rmtree(qa_dir, ignore_errors=True)
        else:
            for f in ("snapshot.json", "summary.json"):
                try:
                    os.remove(os.path.join(qa_dir, f))
                except OSError:
                    pass
        os.makedirs(qa_dir, exist_ok=True)
        cmd = [exe, "--qa", qa_dir, "--window-size", str(size[0]), str(size[1])] + list(args)
        self.proc = subprocess.Popen(cmd, cwd=os.path.dirname(exe))
        self.hwnd = 0
        self.snapshot = None
        self._cmd_serial = 0
        t0 = time.time()
        while time.time() - t0 < 30:
            self.hwnd = self._find_window()
            if self.hwnd and os.path.exists(os.path.join(qa_dir, "snapshot.json")):
                break
            if self.proc.poll() is not None:
                raise QaFailure("o app terminou na inicializacao (codigo %s)" % self.proc.returncode)
            time.sleep(0.1)
        if not self.hwnd:
            raise QaFailure("janela do app nao apareceu")
        self.move_window(pos[0], pos[1])
        self.wait_frames(10)

    def _find_window(self):
        found = []

        @ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
        def cb(hwnd, _):
            pid = wintypes.DWORD()
            user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
            if pid.value == self.proc.pid and user32.IsWindowVisible(hwnd):
                buf = ctypes.create_unicode_buffer(64)
                user32.GetClassNameW(hwnd, buf, 64)
                if buf.value.startswith("GLFW"):
                    title = ctypes.create_unicode_buffer(64)
                    user32.GetWindowTextW(hwnd, title, 64)
                    if title.value.startswith("BSL Stage") and "— Saída" not in title.value:
                        found.append(hwnd)
            return True

        user32.EnumWindows(cb, 0)
        return found[0] if found else 0

    def alive(self):
        return self.proc.poll() is None

    def client_origin(self):
        pt = wintypes.POINT(0, 0)
        user32.ClientToScreen(self.hwnd, ctypes.byref(pt))
        return pt.x, pt.y

    def client_size(self):
        r = wintypes.RECT()
        user32.GetClientRect(self.hwnd, ctypes.byref(r))
        return r.right - r.left, r.bottom - r.top

    def move_window(self, x, y):
        user32.SetWindowPos(self.hwnd, 0, x, y, 0, 0, 0x0001 | 0x0004)
        self.focus()

    def resize_client(self, w, h):
        """Redimensiona a janela (restaurada) para que a area cliente tenha w x h px."""
        user32.ShowWindow(self.hwnd, SW_RESTORE)
        time.sleep(0.3)
        wr, cr = wintypes.RECT(), wintypes.RECT()
        user32.GetWindowRect(self.hwnd, ctypes.byref(wr))
        user32.GetClientRect(self.hwnd, ctypes.byref(cr))
        dw = (wr.right - wr.left) - (cr.right - cr.left)
        dh = (wr.bottom - wr.top) - (cr.bottom - cr.top)
        user32.SetWindowPos(self.hwnd, 0, wr.left, wr.top, w + dw, h + dh, 0x0004)
        self.wait_frames(8)

    def maximize(self):
        user32.ShowWindow(self.hwnd, SW_MAXIMIZE)
        self.wait_frames(10)

    def restore(self):
        user32.ShowWindow(self.hwnd, SW_RESTORE)
        self.wait_frames(10)

    def drag_resize_border(self, dx, dy, steps=12):
        """Arrasta o canto inferior direito da janela com o mouse (resize continuo, como o usuario)."""
        wr = wintypes.RECT()
        user32.GetWindowRect(self.hwnd, ctypes.byref(wr))
        x, y = wr.right - 3, wr.bottom - 3
        self.mouse_move(x, y)
        time.sleep(0.15)
        _mouse(MOUSEEVENTF_LEFTDOWN)
        for i in range(1, steps + 1):
            self.mouse_move(x + dx * i / steps, y + dy * i / steps)
            time.sleep(0.03)
        _mouse(MOUSEEVENTF_LEFTUP)
        self.wait_frames(8)

    def focus(self):
        fg = user32.GetForegroundWindow()
        cur = kernel32.GetCurrentThreadId()
        fg_thread = user32.GetWindowThreadProcessId(fg, None) if fg else 0
        if fg_thread and fg_thread != cur:
            user32.AttachThreadInput(cur, fg_thread, True)
        user32.SetForegroundWindow(self.hwnd)
        if fg_thread and fg_thread != cur:
            user32.AttachThreadInput(cur, fg_thread, False)
        time.sleep(0.1)

    def read(self):
        path = os.path.join(self.dir, "snapshot.json")
        for _ in range(5):
            try:
                with open(path, "rb") as f:
                    data = json.loads(f.read().decode("utf-8"))
                self.snapshot = data
                self.items = [Item(d) for d in data["items"]]
                return data
            except (OSError, ValueError):
                time.sleep(0.02)
        return self.snapshot

    def wait_frames(self, n=6, timeout=8.0):
        d = self.read()
        target = (d["frame"] if d else 0) + n
        t0 = time.time()
        while time.time() - t0 < timeout:
            d = self.read()
            if d and d["frame"] >= target:
                return d
            if not self.alive():
                raise QaFailure("o app encerrou (codigo %s)" % self.proc.returncode)
            time.sleep(0.01)
        raise QaFailure("o app parou de desenhar frames (travado?)")

    @property
    def state(self):
        return self.snapshot["state"]

    @property
    def scale(self):
        return self.snapshot["scale"]

    def find_all(self, label, window=None, visible=True):
        out = []
        for it in self.items:
            if it.is_window or not _matches(it.label, label):
                continue
            if window and window not in it.window:
                continue
            if visible and not it.visible:
                continue
            out.append(it)
        return out

    def find(self, label, window=None, nth=0, timeout=3.0, visible=True):
        t0 = time.time()
        while True:
            self.read()
            found = self.find_all(label, window, visible)
            if len(found) > nth:
                return found[nth]
            if time.time() - t0 > timeout:
                raise QaFailure("item nao encontrado: %r (janela=%r, nth=%d)" % (label, window, nth))
            time.sleep(0.05)

    def has(self, label, window=None):
        self.read()
        return bool(self.find_all(label, window))

    def window_rect(self, name_part):
        self.read()
        for it in self.items:
            if it.is_window and name_part in it.window:
                return it.rect
        return None

    def wait_state(self, pred, what, timeout=5.0):
        t0 = time.time()
        while time.time() - t0 < timeout:
            d = self.read()
            if d and pred(d["state"]):
                return d["state"]
            if not self.alive():
                raise QaFailure("o app encerrou (codigo %s)" % self.proc.returncode)
            time.sleep(0.05)
        raise QaFailure("estado esperado nao ocorreu: " + what)

    def to_screen(self, x, y):
        ox, oy = self.client_origin()
        kx = ky = 1.0
        win = (self.snapshot or {}).get("win")
        if win and win[0] > 0 and win[1] > 0:
            cw, ch = self.client_size()
            kx, ky = cw / win[0], ch / win[1]
        return int(round(ox + x * kx)), int(round(oy + y * ky))

    def mouse_move(self, x, y, client=False):
        if client:
            x, y = self.to_screen(x, y)
        user32.SetCursorPos(int(round(x)), int(round(y)))

    def hover(self, x, y):
        self.mouse_move(x, y, client=True)
        time.sleep(0.06)

    def click_at(self, x, y, button="left", double=False):
        self.ensure_focus()
        self.hover(x, y)
        down, up = (MOUSEEVENTF_LEFTDOWN, MOUSEEVENTF_LEFTUP) if button == "left" else (MOUSEEVENTF_RIGHTDOWN, MOUSEEVENTF_RIGHTUP)
        for _ in range(2 if double else 1):
            _mouse(down)
            time.sleep(0.05)
            _mouse(up)
            time.sleep(0.05)
        time.sleep(0.08)

    def click(self, label, window=None, nth=0, button="left", double=False, dx=0.5, dy=0.5):
        it = self.find(label, window, nth)
        x = it.rect.x0 + it.rect.w * dx
        y = it.rect.y0 + it.rect.h * dy
        self.click_at(x, y, button, double)
        self.wait_frames(3)
        return it

    def drag(self, x0, y0, x1, y1, steps=14, hold=0.0, button="left", mods=(), probe=None):
        """Arrasta em coordenadas do cliente, com passos curtos (o ImGui precisa de varios frames).
        'mods' = teclas seguradas durante o arrasto ("shift", "ctrl", "alt", "space"); 'button' = left | right | middle.
        'probe' = funcao chamada com o botao AINDA apertado, depois do ultimo passo (le o estado ou tira screenshot no
        meio do arrasto, por exemplo as linhas-guia do ima); o resultado dela e devolvido."""
        self.hover(x0, y0)
        down, up = {"left": (MOUSEEVENTF_LEFTDOWN, MOUSEEVENTF_LEFTUP), "right": (MOUSEEVENTF_RIGHTDOWN, MOUSEEVENTF_RIGHTUP),
                    "middle": (MOUSEEVENTF_MIDDLEDOWN, MOUSEEVENTF_MIDDLEUP)}[button]
        for m in mods:
            _key(VK[m])
        time.sleep(0.05 if mods else 0)
        _mouse(down)
        time.sleep(0.08)
        for i in range(1, steps + 1):
            self.mouse_move(x0 + (x1 - x0) * i / steps, y0 + (y1 - y0) * i / steps, client=True)
            time.sleep(0.03)
        time.sleep(0.08 + hold)
        result = None
        if probe:
            self.wait_frames(4)
            result = probe()
        _mouse(up)
        time.sleep(0.08)
        for m in reversed(mods):
            _key(VK[m], up=True)
        self.wait_frames(3)
        return result

    def drag_items(self, src, dst, **kw):
        self.drag(src.rect.cx, src.rect.cy, dst.rect.cx, dst.rect.cy, **kw)

    def wheel(self, x, y, clicks, shift=False):
        """Roda do mouse sobre (x, y) do cliente (clicks > 0 = para cima). Shift = rolagem horizontal."""
        self.hover(x, y)
        if shift:
            _key(VK["shift"])
        for _ in range(abs(clicks)):
            _mouse(MOUSEEVENTF_WHEEL, (120 if clicks > 0 else -120) & 0xFFFFFFFF)
            time.sleep(0.05)
        if shift:
            _key(VK["shift"], up=True)
        self.wait_frames(3)

    def reset_input(self):
        """Solta botoes do mouse e fecha popups/modais abertos (limpeza depois de uma falha)."""
        _mouse(MOUSEEVENTF_LEFTUP)
        _mouse(MOUSEEVENTF_RIGHTUP)
        for _ in range(2):
            self.key("esc")

    def ensure_focus(self):
        if user32.GetForegroundWindow() != self.hwnd:
            self.focus()

    def key(self, name, *mods, wait=True):
        self.ensure_focus()
        vk = VK[name] if name in VK else ord(name.upper())
        for m in mods:
            _key(VK[m])
        _key(vk)
        time.sleep(0.04)
        _key(vk, up=True)
        for m in reversed(mods):
            _key(VK[m], up=True)
        time.sleep(0.06)
        if wait:
            self.wait_frames(2)

    def hold_key(self, name, seconds):
        self.key_down(name)
        time.sleep(seconds)
        self.key_up(name)

    def key_down(self, name):
        """Aperta e segura a tecla (atalho no modo 'segurar'): solte com key_up."""
        self.ensure_focus()
        _key(VK[name] if name in VK else ord(name.upper()))

    def key_up(self, name):
        _key(VK[name] if name in VK else ord(name.upper()), up=True)

    def type_text_nowait(self, text):
        self.ensure_focus()
        for ch in text:
            _key(0, unicode_char=ch)
            _key(0, up=True, unicode_char=ch)
            time.sleep(0.02)
        time.sleep(0.05)

    def type_text(self, text):
        self.type_text_nowait(text)
        self.wait_frames(2)

    def command(self, line):
        self._cmd_serial += 1
        tmp = os.path.join(self.dir, "tmp_cmd.txt")
        with open(tmp, "w", encoding="utf-8") as f:
            f.write(line + "\n")
        os.replace(tmp, os.path.join(self.dir, "cmd.%06d.txt" % self._cmd_serial))
        self.wait_frames(4)

    def screenshot(self, path):
        path = os.path.abspath(path)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        if os.path.exists(path):
            os.remove(path)
        self.command("shot " + path)
        t0 = time.time()
        while not os.path.exists(path) and time.time() - t0 < 5:
            time.sleep(0.05)
        self.wait_frames(2)
        return path

    def drop_files(self, files, at_item):
        """Simula soltar arquivos do Explorer sobre um item: cursor real sobre ele + WM_DROPFILES na janela."""
        self.hover(at_item.rect.cx, at_item.rect.cy)
        self.command("drop " + "|".join(os.path.abspath(f) for f in files))

    def find_dialog(self, timeout=6.0):
        t0 = time.time()
        while time.time() - t0 < timeout:
            found = []

            @ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
            def cb(hwnd, _):
                pid = wintypes.DWORD()
                user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
                if pid.value == self.proc.pid and hwnd != self.hwnd and user32.IsWindowVisible(hwnd):
                    buf = ctypes.create_unicode_buffer(64)
                    user32.GetClassNameW(hwnd, buf, 64)
                    if buf.value == "#32770":
                        found.append(hwnd)
                return True

            user32.EnumWindows(cb, 0)
            if found:
                return found[0]
            time.sleep(0.1)
        return 0

    def dialog_close(self, dlg):
        user32.PostMessageW(dlg, 0x0010, 0, 0)
        time.sleep(0.3)

    def quit(self):
        if self.alive():
            try:
                self.command("quit")
            except QaFailure:
                pass
            for _ in range(60):
                if not self.alive():
                    break
                time.sleep(0.1)
        if self.alive():
            self.proc.kill()
        return self.proc.returncode

    def summary(self):
        try:
            with open(os.path.join(self.dir, "summary.json"), "rb") as f:
                return json.loads(f.read().decode("utf-8"))
        except (OSError, ValueError):
            return None

    def log_text(self):
        try:
            with open(os.path.join(self.dir, "appdata", "bslstage.log"), "rb") as f:
                return f.read().decode("utf-8", "replace")
        except OSError:
            return ""
