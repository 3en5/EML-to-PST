#include "common/win_compat.h"
#include "worker/cancellation/cancel_token.h"

namespace wlm2pst {

namespace {

// Runs on a system thread. Only signal-safe work here (spec section 19: no
// complex code inside the console control callback).
BOOL WINAPI console_ctrl_handler(DWORD ctrl_type) {
    switch (ctrl_type) {
        case CTRL_C_EVENT:
        case CTRL_BREAK_EVENT:
            global_cancel_token().request_cancel();
            // After the second Ctrl+C the worker exits at the next safe
            // point; a third falls through to default termination.
            return global_cancel_token().level() <= 2 ? TRUE : FALSE;
        case CTRL_CLOSE_EVENT:
        case CTRL_LOGOFF_EVENT:
        case CTRL_SHUTDOWN_EVENT:
            global_cancel_token().request_cancel();
            global_cancel_token().request_cancel();  // treat as hard cancel
            return TRUE;
        default:
            return FALSE;
    }
}

}  // namespace

Status install_console_ctrl_handler() {
    if (!SetConsoleCtrlHandler(console_ctrl_handler, TRUE)) {
        return make_win32_error(GetLastError(), "SetConsoleCtrlHandler");
    }
    return Status::success();
}

}  // namespace wlm2pst
