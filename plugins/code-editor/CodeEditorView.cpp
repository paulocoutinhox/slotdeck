#include "CodeEditorView.h"

#include "ui/Components.h"
#include "ui/Icons.h"
#include "ui/TabBar.h"

#include <QFileDialog>
#include <QLabel>
#include <QPushButton>
#include <QTabBar>
#include <QVBoxLayout>

namespace slotdeck::plugins::codeeditor {

CodeEditorView::CodeEditorView(CodeEditorPlugin& plugin, QWidget* parent) : QWidget(parent), m_plugin(plugin), m_workspaces(new ui::TabWidget(plugin.host().theme(), this)) {
    auto* header = new ui::PageHeader(m_plugin.host().theme(), m_plugin.host().translate(QStringLiteral("code-editor.plugin.title")), this);
    auto* open = new QPushButton(ui::icon(ui::IconName::Folder, m_plugin.host().theme()), m_plugin.host().translate(QStringLiteral("code-editor.actions.open-folder")), header);
    auto* saveAll = new QPushButton(m_plugin.host().translate(QStringLiteral("code-editor.actions.save-all")), header);
    header->addStretch();
    header->addWidget(open);
    header->addWidget(saveAll);
    m_workspaces->setTabsClosable(true);
    m_workspaces->setMovable(true);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    m_empty = ui::emptyStateLabel(m_plugin.host().translate(QStringLiteral("code-editor.view.empty")), this);
    layout->addWidget(header);
    layout->addWidget(m_workspaces, 1);
    layout->addWidget(m_empty, 1);
    // clang-format off
    connect(open, &QPushButton::clicked, this, [this]() { chooseWorkspace(); });
    connect(saveAll, &QPushButton::clicked, this, [this]() { if (auto* view = qobject_cast<CodeWorkspaceView*>(m_workspaces->currentWidget()); view != nullptr) { view->saveAll(); } });
    connect(m_workspaces, &QTabWidget::tabCloseRequested, this, [this](int index) { closeWorkspace(index); });
    connect(m_workspaces, &QTabWidget::currentChanged, this, [this](int index) { if (!m_rebuilding && index >= 0) { auto* view = qobject_cast<CodeWorkspaceView*>(m_workspaces->widget(index)); const auto result = m_plugin.activateWorkspace(view->workspaceId()); if (!result.hasValue()) { reportError(result.error().message); } } });
    connect(m_workspaces->tabBar(), &QTabBar::tabMoved, this, [this](int from, int to) { if (!m_rebuilding) { const auto result = m_plugin.moveWorkspace(from, to); if (!result.hasValue()) { reportError(result.error().message); } } });
    connect(&m_plugin, &CodeEditorPlugin::workspacesChanged, this, [this]() { synchronizeWorkspaces(); });
    connect(&m_plugin, &CodeEditorPlugin::languageServersChanged, this, [this]() { synchronizeLanguageServers(); });
    connect(&m_plugin, &CodeEditorPlugin::wordWrapChanged, this, [this]() { synchronizeWordWrap(); });
    connect(&m_plugin, &CodeEditorPlugin::editorFontChanged, this, [this]() { synchronizeEditorFont(); });
    // clang-format on
    synchronizeWorkspaces();
}

void CodeEditorView::chooseWorkspace() {
    const QString path = QFileDialog::getExistingDirectory(this, m_plugin.host().translate(QStringLiteral("code-editor.actions.open-folder")));
    if (path.isEmpty()) {
        return;
    }
    const auto result = m_plugin.openWorkspace(path);
    if (!result.hasValue()) {
        reportError(result.error().detail.isEmpty() ? result.error().message : result.error().detail);
    }
}

void CodeEditorView::closeWorkspace(int index) {
    auto* view = qobject_cast<CodeWorkspaceView*>(m_workspaces->widget(index));
    if (view == nullptr || !view->canClose()) {
        return;
    }
    const auto result = m_plugin.closeWorkspace(view->workspaceId());
    if (!result.hasValue()) {
        reportError(result.error().message);
    }
}

void CodeEditorView::synchronizeWorkspaces() {
    m_rebuilding = true;
    for (int index = m_workspaces->count() - 1; index >= 0; --index) {
        auto* view = qobject_cast<CodeWorkspaceView*>(m_workspaces->widget(index));
        bool exists = false;
        for (const auto& state : m_plugin.workspaces()) {
            exists = exists || state.id == view->workspaceId();
        }
        if (!exists) {
            m_workspaces->removeTab(index);
            view->deleteLater();
        }
    }
    for (int index = 0; index < m_plugin.workspaces().size(); ++index) {
        const auto& state = m_plugin.workspaces().at(index);
        auto* view = workspaceView(state.id);
        if (view == nullptr) {
            view = new CodeWorkspaceView(state, m_plugin.activeLanguageServers(), m_plugin.wordWrap(), m_plugin.editorFont(), m_plugin.defaultCharset(), m_plugin.host(), m_workspaces);
            m_workspaces->addTab(view, view->title());
            // clang-format off
            connect(view, &CodeWorkspaceView::stateChanged, this, [this, view]() { const auto result = m_plugin.updateWorkspace(view->state()); if (!result.hasValue()) { reportError(result.error().message); } });
            connect(view, &CodeWorkspaceView::wordWrapToggled, this, [this, view]() { m_plugin.setWordWrap(view->wordWrapEnabled()); });
            connect(view, &CodeWorkspaceView::operationFailed, this, [this](const QString& message) { reportError(message); });
            // clang-format on
        }
        const int current = m_workspaces->indexOf(view);
        if (current != index) {
            m_workspaces->tabBar()->moveTab(current, index);
        }
        if (state.active) {
            m_workspaces->setCurrentIndex(index);
        }
    }
    m_rebuilding = false;
    m_workspaces->setVisible(m_workspaces->count() > 0);
    m_empty->setVisible(m_workspaces->count() == 0);
}

void CodeEditorView::synchronizeWordWrap() {
    for (int index = 0; index < m_workspaces->count(); ++index) {
        qobject_cast<CodeWorkspaceView*>(m_workspaces->widget(index))->setWordWrap(m_plugin.wordWrap());
    }
}

void CodeEditorView::synchronizeEditorFont() {
    for (int index = 0; index < m_workspaces->count(); ++index) {
        qobject_cast<CodeWorkspaceView*>(m_workspaces->widget(index))->setEditorFont(m_plugin.editorFont());
    }
}

void CodeEditorView::synchronizeLanguageServers() {
    for (int index = 0; index < m_workspaces->count(); ++index) {
        qobject_cast<CodeWorkspaceView*>(m_workspaces->widget(index))->setLanguageServers(m_plugin.activeLanguageServers());
    }
}

void CodeEditorView::reportError(const QString& message) {
    m_plugin.host().notify(m_plugin.host().translate(QStringLiteral("code-editor.error.title")), message, AlertSeverity::Error);
}

CodeWorkspaceView* CodeEditorView::workspaceView(const QString& workspaceId) const {
    for (int index = 0; index < m_workspaces->count(); ++index) {
        auto* view = qobject_cast<CodeWorkspaceView*>(m_workspaces->widget(index));
        if (view->workspaceId() == workspaceId) {
            return view;
        }
    }
    return nullptr;
}

} // namespace slotdeck::plugins::codeeditor
