#pragma once

#include <span>
#include <string_view>

namespace icd::cli {

    // `icd-cli classify <folder> [--depth N] [--top N] [--list CLASS]`: classifies every file below a folder with the
    // built-in catalog (T076c) and prints files and bytes per class with the folders that hold most of each, or every
    // path of one class. Walks the folder with the ordinary directory API, so it needs no administrator rights and no
    // volume handle; project markers above the folder are not seen. Reads only. Returns 0 on success, 1 on failure, 2
    // on bad arguments.
    int runClassify(std::span<const std::wstring_view> args);

    // One usage line for the command.
    [[nodiscard]] const char* classifyUsage();

} // namespace icd::cli
