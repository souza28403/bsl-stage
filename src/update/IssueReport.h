#pragma once

#include <string>
#include <string_view>

namespace bsl::update {

enum class ReportKind { Bug, Improvement };

struct SystemInfo
{
    std::string appVersion;
    std::string windows;
    std::string gpu;
    std::string openGl;
    std::string profile;
    std::string ram;
};

struct ReportInput
{
    ReportKind kind = ReportKind::Bug;
    std::string title;
    std::string description;
    std::string steps;
    bool includeSystem = false;
    bool includeLog = false;
    SystemInfo system;
    std::string logTail;
};

std::string buildReportBody(const ReportInput& in);
const char* labelFor(ReportKind kind);

std::string urlEncode(std::string_view text);

struct IssueUrl
{
    std::string url;
    std::string bodyInUrl;
    std::string remainder;
    bool truncated = false;
};

inline constexpr size_t kMaxIssueUrl = 7000;

IssueUrl buildIssueUrl(const std::string& repo, ReportKind kind, const std::string& title, const std::string& body,
                       size_t maxUrl = kMaxIssueUrl);

std::string tailLines(const std::string& text, int maxLines, size_t maxBytes);
std::string redactPersonalData(const std::string& text, const std::string& userProfileDir, const std::string& userName,
                               const std::string& computerName);

}
