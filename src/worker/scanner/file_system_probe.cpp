// WLM2PST - portable reparse-point probe (spec section 6). See
// file_system_probe.h. The Windows factory override lives in scanner_win.cpp.
#include "worker/scanner/file_system_probe.h"

#include <system_error>

namespace wlm2pst {

bool DefaultFileSystemProbe::is_reparse_point(const std::filesystem::path& p) {
    std::error_code ec;
    std::filesystem::file_status status = std::filesystem::symlink_status(p, ec);
    if (ec) {
        // Cannot stat the entry at all; treat conservatively as "not a
        // reparse point" here. The caller's own stat of the entry will
        // separately surface unreadable entries.
        return false;
    }
    return std::filesystem::is_symlink(status);
}

#ifndef _WIN32
std::unique_ptr<IFileSystemProbe> make_default_probe() {
    return std::make_unique<DefaultFileSystemProbe>();
}
#endif

}  // namespace wlm2pst
