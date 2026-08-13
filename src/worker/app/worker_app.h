// WLM2PST - worker application flow: CLI -> preflight -> job setup ->
// import pipeline -> validation -> reports -> exit code.
//
// Portable: takes the MAPI environment and system probe as interfaces so the
// whole flow is exercisable in tests with fakes; main.cpp injects the real
// Windows implementations.
#pragma once

#include "worker/import_engine.h"
#include "worker/preflight/preflight.h"

#include <string>
#include <vector>

namespace wlm2pst {

// Returns the process exit code (see common/exit_codes.h).
int run_worker_app(const std::vector<std::wstring>& args,
                   IMapiEnvironment& mapi_env,
                   ISystemProbe& probe);

}  // namespace wlm2pst
