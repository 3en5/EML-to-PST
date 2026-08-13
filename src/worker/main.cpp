// WLM2PST worker entry point (Windows-only). All logic lives in
// worker_app.cpp behind portable interfaces; this file only injects the real
// MAPI environment and system probe.
#include "common/win_compat.h"

#include "worker/app/worker_app.h"
#include "worker/cancellation/cancel_token.h"
#include "worker/import_engine.h"
#include "worker/preflight/preflight.h"

#include <vector>

int wmain(int argc, wchar_t** argv) {
    (void)wlm2pst::install_console_ctrl_handler();

    std::vector<std::wstring> args;
    args.reserve(static_cast<size_t>(argc > 0 ? argc - 1 : 0));
    for (int i = 1; i < argc; ++i) args.emplace_back(argv[i]);

    auto mapi_env = wlm2pst::make_mapi_environment();
    auto probe = wlm2pst::make_system_probe();
    return wlm2pst::run_worker_app(args, *mapi_env, *probe);
}
