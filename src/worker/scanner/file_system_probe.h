// WLM2PST - injectable filesystem probe used by the scanner (spec section 6).
//
// The scanner never descends into reparse points, junctions, or symbolic
// links. Real reparse-point detection differs by platform (Windows needs
// FILE_ATTRIBUTE_REPARSE_POINT to catch junctions, which std::filesystem's
// is_symlink() misses), so it is hidden behind this interface. Tests inject
// a fake probe; production code uses make_default_probe().
#pragma once

#include <filesystem>
#include <memory>

namespace wlm2pst {

class IFileSystemProbe {
public:
    virtual ~IFileSystemProbe() = default;

    // True if the directory entry at `p` is a reparse point / symlink /
    // junction that traversal must NOT follow. `p` is not itself dereferenced
    // (implementations must not follow the link to answer this question).
    virtual bool is_reparse_point(const std::filesystem::path& p) = 0;
};

// Portable implementation: std::filesystem::is_symlink(symlink_status).
// Covers POSIX symlinks. Does not see Windows junctions (no reparse-point
// attribute concept in std::filesystem); the Windows override below covers
// that case.
class DefaultFileSystemProbe : public IFileSystemProbe {
public:
    bool is_reparse_point(const std::filesystem::path& p) override;
};

// Returns the platform-appropriate probe. On Windows this checks
// FILE_ATTRIBUTE_REPARSE_POINT via GetFileAttributesW (covers junctions,
// which is_symlink() misses); elsewhere it returns DefaultFileSystemProbe.
// Implemented once per platform: the portable body lives in
// file_system_probe.cpp (guarded by #ifndef _WIN32), the Windows body in
// scanner_win.cpp.
std::unique_ptr<IFileSystemProbe> make_default_probe();

}  // namespace wlm2pst
