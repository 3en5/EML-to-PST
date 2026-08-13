// WLM2PST - Windows reparse-point probe (spec section 6). Excluded from
// non-Windows builds by the `_win.cpp` filename suffix convention.
#include "common/win_compat.h"
#include "worker/scanner/file_system_probe.h"

namespace wlm2pst {

namespace {

// Checks FILE_ATTRIBUTE_REPARSE_POINT directly. Unlike
// std::filesystem::is_symlink(), this also catches NTFS junctions and mount
// points, which are reparse points but not "symlinks" in the POSIX sense.
class WindowsFileSystemProbe : public IFileSystemProbe {
public:
    bool is_reparse_point(const std::filesystem::path& p) override {
        const DWORD attrs = ::GetFileAttributesW(p.c_str());
        if (attrs == INVALID_FILE_ATTRIBUTES) {
            // Cannot stat the entry; let the caller's own attempt to read it
            // surface the failure as "unreadable".
            return false;
        }
        return (attrs & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
    }
};

}  // namespace

std::unique_ptr<IFileSystemProbe> make_default_probe() {
    return std::make_unique<WindowsFileSystemProbe>();
}

}  // namespace wlm2pst
