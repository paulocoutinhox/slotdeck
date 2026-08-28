#pragma once

#include "plugins/PluginInterface.h"
#include "utils/Result.h"

#include <QString>

namespace slotdeck::plugins::terminalplugin {

// A notification carries a sentence of the catalog while a fault of the engine stays in the log.
[[nodiscard]] QString terminalFailureMessage(const utils::Error& error, PluginHost& host);

} // namespace slotdeck::plugins::terminalplugin
