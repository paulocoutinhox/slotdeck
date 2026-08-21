#pragma once

#include <QIcon>
#include <QWidget>

#include "plugins/PluginInterface.h"

class QVBoxLayout;

namespace slotdeck::ui {

class ModeButton;

class ModeBar final : public QWidget {
    Q_OBJECT

  public:
    explicit ModeBar(QWidget* parent = nullptr);
    [[nodiscard]] QSize sizeHint() const override;
    void addMode(const QString& modeId, const QIcon& icon, const QString& title, plugins::NavigationPlacement placement);
    void setCurrentMode(const QString& modeId);

  signals:
    void modeRequested(const QString& modeId);

  private slots:
    void activateMode(const QString& modeId);

  private:
    void updateButtonGeometry();

    QVBoxLayout* m_topLayout{nullptr};
    QVBoxLayout* m_bottomLayout{nullptr};
    QList<ModeButton*> m_buttons;
    int m_preferredWidth{0};
};

} // namespace slotdeck::ui
