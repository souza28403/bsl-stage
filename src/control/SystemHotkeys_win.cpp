#include "control/SystemHotkeys.h"

#include <windows.h>

namespace bsl::control {

namespace {

UINT modifiersOf(const Chord& chord)
{
    UINT m = MOD_NOREPEAT;
    if (chord.mods & ModCtrl)
        m |= MOD_CONTROL;
    if (chord.mods & ModShift)
        m |= MOD_SHIFT;
    if (chord.mods & ModAlt)
        m |= MOD_ALT;
    return m;
}

std::string failureText(DWORD code)
{
    if (code == ERROR_HOTKEY_ALREADY_REGISTERED)
        return "Esta combinação já é usada pelo Windows ou por outro programa.";
    if (code == ERROR_INVALID_PARAMETER || code == ERROR_INVALID_WINDOW_HANDLE)
        return "O Windows não aceita esta combinação como atalho global.";
    return "Não foi possível registrar o atalho no sistema (código " + std::to_string(code) + ").";
}

}

SystemHotkeys::~SystemHotkeys()
{
    unregisterAll();
}

void SystemHotkeys::unregisterAll()
{
    for (const Reg& r : regs_)
        if (r.ok)
            UnregisterHotKey(nullptr, r.id);
    regs_.clear();
}

size_t SystemHotkeys::registeredCount() const
{
    size_t n = 0;
    for (const Reg& r : regs_)
        n += r.ok ? 1 : 0;
    return n;
}

std::string SystemHotkeys::error(int tag) const
{
    for (const Reg& r : regs_)
        if (r.tag == tag)
            return r.error;
    return {};
}

void SystemHotkeys::sync(const std::vector<Entry>& wanted)
{
    for (size_t i = 0; i < regs_.size();) {
        bool keep = false;
        for (const Entry& e : wanted)
            keep = keep || (e.tag == regs_[i].tag && e.chord == regs_[i].chord);
        if (keep) {
            ++i;
            continue;
        }
        if (regs_[i].ok)
            UnregisterHotKey(nullptr, regs_[i].id);
        regs_.erase(regs_.begin() + static_cast<std::ptrdiff_t>(i));
    }
    for (const Entry& e : wanted) {
        bool have = false;
        for (const Reg& r : regs_)
            have = have || (r.tag == e.tag && r.chord == e.chord);
        if (have || !e.chord.valid())
            continue;
        Reg r;
        r.chord = e.chord;
        r.tag = e.tag;
        const unsigned vk = keyVirtualCode(e.chord.key);
        if (vk == 0) {
            r.error = "Esta tecla não pode ser um atalho global do sistema.";
        } else {
            bool dup = false;
            for (const Reg& o : regs_)
                dup = dup || (o.ok && o.chord == e.chord);
            if (dup) {
                r.error = "Esta combinação já está registrada para outro atalho.";
            } else {
                r.id = nextId_++;
                if (nextId_ > 0xBFF0)
                    nextId_ = 1;
                if (RegisterHotKey(nullptr, r.id, modifiersOf(e.chord), vk))
                    r.ok = true;
                else
                    r.error = failureText(GetLastError());
            }
        }
        regs_.push_back(std::move(r));
    }
}

std::vector<int> SystemHotkeys::poll()
{
    std::vector<int> tags;
    MSG msg;
    while (PeekMessageW(&msg, nullptr, WM_HOTKEY, WM_HOTKEY, PM_REMOVE)) {
        const int id = static_cast<int>(msg.wParam);
        for (const Reg& r : regs_)
            if (r.ok && r.id == id)
                tags.push_back(r.tag);
    }
    return tags;
}

bool SystemHotkeys::keyDown(Key key)
{
    const unsigned vk = keyVirtualCode(key);
    return vk != 0 && (GetAsyncKeyState(static_cast<int>(vk)) & 0x8000) != 0;
}

std::string SystemHotkeys::probe(Chord chord)
{
    const unsigned vk = keyVirtualCode(chord.key);
    if (vk == 0)
        return "Esta tecla não pode ser um atalho global do sistema.";
    constexpr int kProbeId = 0xBFFE;
    if (!RegisterHotKey(nullptr, kProbeId, modifiersOf(chord), vk))
        return failureText(GetLastError());
    UnregisterHotKey(nullptr, kProbeId);
    return {};
}

}
