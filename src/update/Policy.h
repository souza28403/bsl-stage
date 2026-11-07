#pragma once

#include "update/Release.h"
#include "update/SemVer.h"

#include <cstdint>
#include <string>

namespace bsl::update {

struct Prefs
{
    bool autoCheck = true;
    bool beta = false;
    std::string skippedVersion;
    int64_t lastCheckUnix = 0;

    friend bool operator==(const Prefs&, const Prefs&) = default;
};

inline constexpr int64_t kCheckIntervalSeconds = 24 * 60 * 60;

struct ShowState
{
    bool outputLive = false;
    bool windowFullscreen = false;
    bool layoutLocked = false;
    bool ecoProfile = false;
};

bool showBlocksAutoCheck(const ShowState& s);

bool dueForAutoCheck(const Prefs& prefs, int64_t nowUnix);

inline constexpr double kAutoCheckDelaySeconds = 8.0;
inline constexpr double kAutoCheckRetrySeconds = 6.0 * 60.0 * 60.0;

bool isSkipped(const Prefs& prefs, const Release& release);

bool shouldOffer(const Prefs& prefs, const Release& release, const SemVer& current);

bool isUpdateFor(const Prefs& prefs, const Release& release, const SemVer& current);

std::string formatPublishedDate(const std::string& iso);
std::string formatLocalDateTime(int64_t unixSeconds);

}
