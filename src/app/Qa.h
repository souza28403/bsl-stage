#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace bsl::qa {

bool enabled();
void init(const std::filesystem::path& dir);
void beginFrame();
void endFrame(int frame, const std::string& stateJson, float scale, int fbW, int fbH, int winW, int winH);
std::vector<std::string> takeCommands();
void finish();

void note(const std::string& name);
void label(const char* name);
void noteRect(const std::string& name, float x0, float y0, float x1, float y1);

}

void bslImGuiAssertFailed(const char* expr, const char* file, int line);
