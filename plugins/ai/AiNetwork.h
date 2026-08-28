#pragma once

#include <QByteArray>

#include <memory>

class QNetworkReply;

namespace slotdeck::plugins::ai {

struct BoundedReply final {
    QByteArray bytes;
    bool truncated{false};
};

// The size of an answer is decided by whoever sends it, so the bound holds while the bytes arrive rather than once they all have.
[[nodiscard]] std::shared_ptr<BoundedReply> boundReply(QNetworkReply* reply, qsizetype maximumBytes);

} // namespace slotdeck::plugins::ai
