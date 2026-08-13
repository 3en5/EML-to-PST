#include "worker/cancellation/cancel_token.h"

namespace wlm2pst {

CancelToken& global_cancel_token() {
    static CancelToken token;
    return token;
}

}  // namespace wlm2pst
