#pragma once

#include "ui/Icons.h"
#include "ui/Theme.h"

#include <QColor>
#include <QComboBox>
#include <QDate>
#include <QDateTime>
#include <QDateTimeEdit>
#include <QElapsedTimer>
#include <QFontMetrics>
#include <QGridLayout>
#include <QPlainTextEdit>
#include <QString>
#include <QStringList>
#include <QTextBrowser>
#include <QWidget>

#include <algorithm>
#include <functional>

class QDialog;
class QFormLayout;
class QTimer;
class QVBoxLayout;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QAbstractSpinBox;
class QTableWidget;
class QTableWidgetItem;
class QTreeWidget;
class QTreeWidgetItem;
class QToolButton;

namespace slotdeck::ui {

class Theme;

class PageHeader final : public QWidget {
    Q_OBJECT

  public:
    PageHeader(const Theme& theme, const QString& title, QWidget* parent);

    void setTitle(const QString& title);
    void addWidget(QWidget* widget, int stretch = 0);
    void addStretch();
    void setSpacing(int spacing);

  private:
    QHBoxLayout* m_layout{nullptr};
    QLabel* m_title{nullptr};
};

// The selectable field paints its own indicator, because the platform drop-down draws a box the flat standard does not have.
class ComboBox final : public QComboBox {
    Q_OBJECT

  public:
    ComboBox(const Theme& theme, QWidget* parent);

    void applyTheme(const Theme& theme);

  protected:
    void paintEvent(QPaintEvent* event) override;

  private:
    const Theme* m_theme{nullptr};
};

// A calendar is painted from the active theme like every other surface, because the platform one draws a grid this product does not have.
class CalendarPopup final : public QWidget {
    Q_OBJECT

  public:
    CalendarPopup(const Theme& theme, QWidget* parent);

    void showFor(const QDate& date, QWidget* anchor);

  signals:
    void dateChosen(const QDate& date);

  private:
    void rebuild();
    void step(int months);

    const Theme& m_theme;
    QDate m_month;
    QDate m_selected;
    QLabel* m_title{nullptr};
    QGridLayout* m_grid{nullptr};
    QVector<QToolButton*> m_cells;
};

// A moment is written in the field and chosen in a calendar of this product, so no platform drop-down and no platform grid reaches the reader.
class DateTimeField final : public QDateTimeEdit {
    Q_OBJECT

  public:
    DateTimeField(const Theme& theme, QWidget* parent);

  protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    // The line edit of a spin box covers the whole field, so the press on the indicator is caught where it really lands.
    bool eventFilter(QObject* watched, QEvent* event) override;

  private:
    [[nodiscard]] QRect indicatorRect() const;
    void openCalendar();

    const Theme& m_theme;
    CalendarPopup* m_calendar{nullptr};
};

// A secret is always masked and only reveals its value after the owner confirms the intent.
class SecretField final : public QWidget {
    Q_OBJECT

  public:
    using RevealConfirmation = std::function<bool()>;

    SecretField(const Theme& theme, const QString& placeholder, RevealConfirmation confirmReveal, QWidget* parent);

    [[nodiscard]] QString value() const;
    void setValue(const QString& value);
    [[nodiscard]] bool revealed() const;

  signals:
    void editingFinished();

  private:
    void toggleReveal();
    void applyReveal(bool revealed);

    const Theme* m_theme{nullptr};
    QLineEdit* m_editor{nullptr};
    QToolButton* m_reveal{nullptr};
    RevealConfirmation m_confirmReveal;
};

// A field the user types several lines into declares its own border, because every surface renames the widget it owns.
class TextField final : public QPlainTextEdit {
    Q_OBJECT

  public:
    TextField(const QString& placeholder, QWidget* parent);
};

// A document that reads Markdown with the fonts of the active theme, sized to the words it carries.
class MarkdownView final : public QTextBrowser {
    Q_OBJECT

  public:
    MarkdownView(const Theme& theme, QWidget* parent);

    // A chat message is written with the return key, so a single newline is a line break rather than a space.
    void setChatMarkdown(const QString& text);
    void setDocumentMarkdown(const QString& text);
    void setInk(const QColor& ink);
    void setContentFontSize(int points);
    // The width the words need, bounded by what the surface allows.
    void fitTo(int available);

  private:
    void applyTheme();

    const Theme& m_theme;
    int m_fontSize{0};
};

// An icon inside a filled disc, which is how a chat names who wrote a message.
class AvatarBadge final : public QWidget {
    Q_OBJECT

  public:
    AvatarBadge(IconName name, const QColor& fill, const QColor& ink, int diameter, QWidget* parent);

  protected:
    void paintEvent(QPaintEvent* event) override;

  private:
    QIcon m_icon;
    QColor m_fill;
    int m_diameter{0};
};

// A painted rounded box, because a shape that must stay correct at any size is painted rather than styled.
class RoundedSurface final : public QWidget {
    Q_OBJECT

  public:
    RoundedSurface(QColor fill, int radius, QWidget* parent);

    [[nodiscard]] QVBoxLayout* content() const;

  protected:
    void paintEvent(QPaintEvent* event) override;

  private:
    QColor m_fill;
    int m_radius{0};
    QVBoxLayout* m_layout{nullptr};
};

// Work that is still running says so, because a silence reads as a finished run.
class BusyIndicator final : public QWidget {
    Q_OBJECT

  public:
    BusyIndicator(const Theme& theme, QWidget* parent);

    void setRunning(bool running);

  protected:
    void paintEvent(QPaintEvent* event) override;

  private:
    const Theme& m_theme;
    QTimer* m_timer{nullptr};
    QElapsedTimer m_clock;
};

// A caption over a field that narrows a list by what is typed into it.
class FilterField final : public QWidget {
    Q_OBJECT

  public:
    FilterField(const QString& caption, const QString& placeholder, QWidget* parent);

    [[nodiscard]] QString text() const;
    void clear();

  signals:
    void filterChanged(const QString& text);

  private:
    QLineEdit* m_editor{nullptr};
};

class StatusIndicator final : public QWidget {
    Q_OBJECT

  public:
    explicit StatusIndicator(QWidget* parent);

    void setColor(const QColor& color);
    // A dot inside a selected row is drawn on the accent, so it is painted in the ink that reads on it.
    void setSelectionInk(const QColor& ink);

  protected:
    void paintEvent(QPaintEvent* event) override;

  private:
    QColor m_color;
    QColor m_selectionInk;
};

[[nodiscard]] QTableWidget* dataGrid(const QStringList& headerLabels, QWidget* parent);
void stretchGridColumn(QTableWidget* grid, int column);
// An action drawn inside a row of a grid, which the grid repaints when its row is selected.
[[nodiscard]] QToolButton* rowActionButton(IconName name, ThemeColor role, const Theme& theme, const QString& toolTip, QWidget* parent);
// A cell whose glyph and ink carry a meaning, which the grid repaints while its row is selected.
[[nodiscard]] QTableWidgetItem* gridStatusItem(const QString& text, IconName name, ThemeColor role, const Theme& theme);
// A glyph a tree item draws, which the tree repaints while that item is selected.
void setItemGlyph(QTreeWidgetItem* item, int column, IconName name, ThemeColor role, const Theme& theme);
void repaintTreeGlyphs(QTreeWidget* tree, const Theme& theme);
void repaintRowActions(QTableWidget* grid, const Theme& theme);
[[nodiscard]] QToolButton* toolButton(const QIcon& buttonIcon, const Theme& theme, const QString& toolTip, QWidget* parent);
[[nodiscard]] QToolButton* toolButton(IconName name, const Theme& theme, const QString& toolTip, QWidget* parent);
[[nodiscard]] QWidget* settingsSectionPage(QWidget* parent);
void sortComboBoxItems(QComboBox* box);
[[nodiscard]] QWidget* horizontalDivider(QWidget* parent);
[[nodiscard]] QWidget* verticalDivider(QWidget* parent);
[[nodiscard]] QFormLayout* settingsForm();
void addSettingsRow(QFormLayout* form, const QString& label, QWidget* field);
[[nodiscard]] QToolButton* chipButton(const QString& text, const Theme& theme, QWidget* parent);
[[nodiscard]] QWidget* stepperRow(QAbstractSpinBox* box, const Theme& theme, QWidget* parent);
[[nodiscard]] QWidget* settingsActionRow(QWidget* parent);
[[nodiscard]] QLabel* sectionTitleLabel(const QString& text, QWidget* parent);
// A dialog grows to fit the message it shows instead of taking the room from the fields above it.
void growDialogToContents(QDialog* dialog);
// A surface opened over the application is a modal window of the platform, so it carries its title and the buttons every window carries.
void showDialogWindow(QDialog* dialog, const QString& title);
[[nodiscard]] QLabel* emptyStateLabel(const QString& text, QWidget* parent);
[[nodiscard]] const QStringList& monospacedFontFamilies();
[[nodiscard]] QString localTimestamp(const QDateTime& utcTimestamp);
// A label breaks between words, and only a single word wider than the space it has left has no word boundary to break at.
[[nodiscard]] Qt::TextFlag labelWrapping(const QString& text, const QFontMetrics& metrics, int availableWidth);
[[nodiscard]] int longestWordWidth(const QString& text, const QFontMetrics& metrics);

inline constexpr int minimumContentFontSize = 8;
inline constexpr int maximumContentFontSize = 36;

[[nodiscard]] inline bool validContentFontSize(int pointSize) {
    return pointSize >= minimumContentFontSize && pointSize <= maximumContentFontSize;
}

[[nodiscard]] inline int steppedContentFontSize(int pointSize, int step) {
    return std::clamp(pointSize + step, minimumContentFontSize, maximumContentFontSize);
}

} // namespace slotdeck::ui
