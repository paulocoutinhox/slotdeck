#pragma once

#include "ui/Theme.h"

#include <QColor>
#include <QPalette>
#include <QProxyStyle>

namespace slotdeck::ui {

[[nodiscard]] QString applicationStyleSheet(const Theme& theme);

class AppStyle final : public QProxyStyle {
  public:
    using Color = ThemeColor;

    explicit AppStyle(QStyle* baseStyle);

    [[nodiscard]] static QColor color(Color role);
    [[nodiscard]] static QPalette applicationPalette();

    [[nodiscard]] int pixelMetric(PixelMetric metric, const QStyleOption* option = nullptr, const QWidget* widget = nullptr) const override;
    [[nodiscard]] int styleHint(StyleHint hint, const QStyleOption* option = nullptr, const QWidget* widget = nullptr, QStyleHintReturn* returnData = nullptr) const override;
    [[nodiscard]] QSize sizeFromContents(ContentsType type, const QStyleOption* option, const QSize& contentsSize, const QWidget* widget = nullptr) const override;
    void drawPrimitive(PrimitiveElement element, const QStyleOption* option, QPainter* painter, const QWidget* widget = nullptr) const override;
    void drawControl(ControlElement element, const QStyleOption* option, QPainter* painter, const QWidget* widget = nullptr) const override;

  private:
    static void drawChevron(const QStyleOption* option, QPainter* painter);
    static void drawSpinSign(const QStyleOption* option, QPainter* painter, bool plus);
    [[nodiscard]] bool drawToolButtonLabel(const QStyleOption* option, QPainter* painter, const QWidget* widget) const;
};

} // namespace slotdeck::ui
