#pragma once

// Artifacts are written in binary: a text-mode stream turns every LF into CRLF
// on Windows, and the committed bytes would stop being the ones this code
// produced.

#include <fstream>
#include <string>

namespace qtoolkit {

[[nodiscard]] inline bool write_text(const std::string& path, const std::string& text,
                                     std::string& error) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        error = path + ": cannot open the file for writing";
        return false;
    }
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    file.close();
    if (!file) {
        error = path + ": short write";
        return false;
    }
    return true;
}

}  // namespace qtoolkit
