#pragma once

#include <filesystem>
#include <optional>

namespace sa::imgui_ui {

std::optional<std::filesystem::path> pickFolder(const wchar_t* title,
                                                const std::filesystem::path& start);
std::optional<std::filesystem::path> pickModelFile(const wchar_t* title,
                                                   const std::filesystem::path& start,
                                                   bool whisper);

} // namespace sa::imgui_ui
