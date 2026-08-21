#include "AiProviderScope.h"

#include "AiProviderCatalog.h"

namespace slotdeck::plugins::ai {

AiProviderScope::AiProviderScope(QObject* parent) : QObject(parent), m_providerId(providerCatalog().isEmpty() ? QString() : providerCatalog().first().id) {}

const QString& AiProviderScope::providerId() const {
    return m_providerId;
}

void AiProviderScope::setProviderId(const QString& providerId) {
    if (providerId == m_providerId) {
        return;
    }

    m_providerId = providerId;
    emit providerChanged(m_providerId);
}

} // namespace slotdeck::plugins::ai
