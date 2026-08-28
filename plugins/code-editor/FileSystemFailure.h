#pragma once

#include "plugins/PluginInterface.h"
#include "utils/Result.h"

#include <QString>

namespace slotdeck::plugins::codeeditor {

// A notification carries a sentence of the catalog while the diagnostic of the filesystem stays in the log.
[[nodiscard]] QString fileSystemFailureMessage(const utils::Error& error, PluginHost& host);

} // namespace slotdeck::plugins::codeeditor
