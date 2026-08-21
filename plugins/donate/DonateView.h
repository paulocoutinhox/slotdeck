#pragma once

#include "plugins/PluginInterface.h"

#include <QPixmap>
#include <QUrl>
#include <QWidget>

namespace slotdeck::plugins::donate {

class DonateView final : public QWidget {
    Q_OBJECT

  public:
    explicit DonateView(PluginHost& host, QWidget* parent = nullptr);

  private:
    void openDonationPage(const QUrl& url);

    PluginHost& m_host;
};

[[nodiscard]] QPixmap profilePixmap(int logicalSize, qreal devicePixelRatio = 1.0);

} // namespace slotdeck::plugins::donate
