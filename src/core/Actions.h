#pragma once

#include <span>
#include <string>

namespace bsl {

struct Composition;

struct ActionDef
{
    const char* id;
    const char* label;
    const char* group;
    int (*run)(Composition&);
};

std::span<const ActionDef> allActions();
const ActionDef* findAction(const std::string& id);

bool runAction(Composition& comp, const std::string& id);

}
