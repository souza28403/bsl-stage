#pragma once

#include "control/Keys.h"

#include <string>
#include <vector>

namespace bsl::control {

class SystemHotkeys
{
public:
    struct Entry
    {
        Chord chord;
        int tag = 0;
    };

    SystemHotkeys() = default;
    ~SystemHotkeys();
    SystemHotkeys(const SystemHotkeys&) = delete;
    SystemHotkeys& operator=(const SystemHotkeys&) = delete;

    void sync(const std::vector<Entry>& wanted);
    void unregisterAll();
    size_t registeredCount() const;
    std::string error(int tag) const;

    std::vector<int> poll();

    static bool keyDown(Key key);
    static std::string probe(Chord chord);

private:
    struct Reg
    {
        Chord chord;
        int tag = 0;
        int id = 0;
        bool ok = false;
        std::string error;
    };
    std::vector<Reg> regs_;
    int nextId_ = 1;
};

}
