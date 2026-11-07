#pragma once

#include <filesystem>
#include <string>

namespace bsl::ffgl {

enum class PeVerdict
{
    Candidate,
    Unreadable,
    NotPe,
    NotX64,
    NoPlugMain,
};

PeVerdict inspectPe(const std::filesystem::path& file, std::string* detail = nullptr);
const char* peVerdictText(PeVerdict v);

}
