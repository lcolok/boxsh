#ifndef BOXSH_FILE_TYPE_H
#define BOXSH_FILE_TYPE_H

#include <string>

namespace boxsh {

struct FileType {
    bool binary;
    std::string mime;
};

// Detect whether a file is binary by reading its header bytes.
// Uses magic-byte signatures for known formats, then falls back to a
// byte-level heuristic (NUL / control-char ratio).
//
// When the file is empty, returns {false, "inode/x-empty"}.
// On read error, returns {false, "application/octet-stream"}.
FileType detect_file_type(const std::string &path);

// Lower-level: detect from an in-memory buffer.
FileType detect_file_type(const unsigned char *buf, size_t len);

}  // namespace boxsh

#endif  // BOXSH_FILE_TYPE_H
