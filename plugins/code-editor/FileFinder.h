#pragma once

#include <QDialog>
#include <QStringList>

class QLabel;
class QLineEdit;
class QListWidget;

namespace slotdeck::plugins {
class PluginHost;
}

namespace slotdeck::plugins::codeeditor {

// Ranks a path against a query typed in pieces, which is how a file is looked for by name rather than by its whole path.
[[nodiscard]] int fileMatchScore(const QString& path, const QString& query);
[[nodiscard]] QStringList rankedFileMatches(const QStringList& paths, const QString& query, int maximumResults);

class FileFinder final : public QDialog {
    Q_OBJECT

  public:
    FileFinder(const QString& rootPath, QStringList paths, bool complete, PluginHost& host, QWidget* parent);

    [[nodiscard]] QString chosenPath() const;

  signals:
    void pathChosen(const QString& path);

  protected:
    // The list is walked from the field, so the query keeps the focus while the choice moves.
    void keyPressEvent(QKeyEvent* event) override;

  private:
    void refreshMatches();
    void chooseCurrent();

    QString m_rootPath;
    QStringList m_paths;
    QLineEdit* m_query{nullptr};
    QListWidget* m_matches{nullptr};
    QLabel* m_summary{nullptr};
    PluginHost& m_host;
};

} // namespace slotdeck::plugins::codeeditor
