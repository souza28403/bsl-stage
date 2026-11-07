#pragma once

#include "update/IssueReport.h"

#include <string>

namespace bsl::update {

std::string windowsVersionText();
std::string totalRamText();
std::string userProfileDir();
std::string userNameText();
std::string computerNameText();

bool openGithubUrl(const std::string& url);

}
