#pragma once

#include "CodeEditorPlugin.h"
#include "CodeWorkspaceView.h"
#include "ui/TabBar.h"

class QLabel;

namespace slotdeck::plugins::codeeditor {

class CodeEditorView final : public QWidget {
    Q_OBJECT

  public:
    CodeEditorView(CodeEditorPlugin& plugin, QWidget* parent = nullptr);

  private:
    void chooseWorkspace();
    void closeWorkspace(int index);
    void synchronizeWorkspaces();
    void synchronizeWordWrap();
    void synchronizeEditorFont();
    void synchronizeLanguageServers();
    void reportError(const QString& message);
    [[nodiscard]] CodeWorkspaceView* workspaceView(const QString& workspaceId) const;

    CodeEditorPlugin& m_plugin;
    ui::TabWidget* m_workspaces{nullptr};
    QLabel* m_empty{nullptr};
    bool m_rebuilding{false};
};

} // namespace slotdeck::plugins::codeeditor
