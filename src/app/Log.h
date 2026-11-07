#pragma once

#include <string>

namespace bsl::log {

void info(const std::string& message);
void warn(const std::string& message);

std::string recentWarning(double seconds = 8.0);

}
