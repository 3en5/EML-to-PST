// WLM2PST - cooperative cancellation (spec section 19).
//
// The Windows console control handler only flips an atomic here; all orderly
// shutdown work happens on the worker thread (never inside the callback).
#pragma once

#include "common/errors/result.h"

#include <atomic>

namespace wlm2pst {

class CancelToken {
public:
    // First Ctrl+C: graceful (finish current message, commit, resumable exit).
    // Second Ctrl+C: hard (stop as soon as safely possible).
    void request_cancel() noexcept {
        int current = level_.load(std::memory_order_relaxed);
        if (current < 2) level_.store(current + 1, std::memory_order_relaxed);
    }
    [[nodiscard]] bool cancelled() const noexcept {
        return level_.load(std::memory_order_relaxed) > 0;
    }
    [[nodiscard]] int level() const noexcept { return level_.load(std::memory_order_relaxed); }

private:
    std::atomic<int> level_{0};
};

CancelToken& global_cancel_token();

// Windows-only (console_ctrl_win.cpp): installs a SetConsoleCtrlHandler that
// signals global_cancel_token(). Declared here so main.cpp stays portable to
// read; defined only on Windows builds.
Status install_console_ctrl_handler();

}  // namespace wlm2pst
