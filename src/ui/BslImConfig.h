#pragma once

#define IMGUI_ENABLE_TEST_ENGINE

void bslImGuiAssertFailed(const char* expr, const char* file, int line);
#define IM_ASSERT(_EXPR) ((void)((_EXPR) || (bslImGuiAssertFailed(#_EXPR, __FILE__, __LINE__), false)))
