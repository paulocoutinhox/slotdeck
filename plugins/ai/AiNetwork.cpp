#include "AiNetwork.h"

#include <QNetworkReply>

namespace slotdeck::plugins::ai {

std::shared_ptr<BoundedReply> boundReply(QNetworkReply* reply, qsizetype maximumBytes) {
    auto answer = std::make_shared<BoundedReply>();
    // clang-format off
    QObject::connect(reply, &QNetworkReply::readyRead, reply, [reply, answer, maximumBytes]() {
        answer->bytes.append(reply->read(maximumBytes - answer->bytes.size()));
        if (answer->bytes.size() >= maximumBytes) {
            answer->truncated = true;
            reply->abort();
        }
    });
    // clang-format on
    return answer;
}

} // namespace slotdeck::plugins::ai
