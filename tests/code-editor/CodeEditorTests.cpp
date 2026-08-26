#include "CodeDocument.h"
#include "CodeEditorPlugin.h"
#include "CodeEditorRepository.h"
#include "CodeEditorTranslations.h"
#include "CodeSyntaxHighlighter.h"
#include "CodeWorkspaceView.h"
#include "EditorConfig.h"
#include "FileFinder.h"
#include "LanguageRegistry.h"
#include "TestFuture.h"
#include "TestPluginHost.h"
#include "TestProcess.h"
#include "TestTranslations.h"
#include "filesystem/FileSystemService.h"
#include "ui/Components.h"
#include "ui/FindBar.h"

#include <QAction>
#include <QCompleter>
#include <QCoreApplication>
#include <QFile>
#include <QFileSystemModel>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPointer>
#include <QPromise>
#include <QScrollBar>
#include <QSignalSpy>
#include <QSplitter>
#include <QStringConverter>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QToolButton>
#include <QTreeView>
#include <QTreeWidget>
#include <QtTest/QTest>

#include <gtest/gtest.h>

#include <memory>
#include <utility>

namespace slotdeck::plugins::codeeditor {

class CodeEditorTestsHelper final {
  public:
    static void installSettingsDocument(test::TestPluginHost& host, bool wordWrap, int fontSize = defaultEditorFontSize, bool languageServers = true);
    static void installWorkspaceRow(test::TestPluginHost& host, const QString& rootPath, const QString& createdAtUtc, const QString& updatedAtUtc);
    static bool writeTestFile(const QString& path, const QByteArray& content);
};

void CodeEditorTestsHelper::installSettingsDocument(test::TestPluginHost& host, bool wordWrap, int fontSize, bool languageServers) {
    host.settingsDocument = {{QStringLiteral("wordWrap"), wordWrap}, {QStringLiteral("fontSize"), fontSize}, {QStringLiteral("languageServersEnabled"), languageServers}};
}

void CodeEditorTestsHelper::installWorkspaceRow(test::TestPluginHost& host, const QString& rootPath, const QString& createdAtUtc, const QString& updatedAtUtc) {
    // clang-format off
    host.queryHandler = [rootPath, createdAtUtc, updatedAtUtc](const QString& statement, const QVariantList&) { persistence::DatabaseRows rows; if (statement.contains(QStringLiteral("code_editor_workspaces"))) { rows.append({{QStringLiteral("id"), QStringLiteral("workspace-1")}, {QStringLiteral("root_path"), rootPath}, {QStringLiteral("position"), 0}, {QStringLiteral("active"), 1}, {QStringLiteral("created_at_utc"), createdAtUtc}, {QStringLiteral("updated_at_utc"), updatedAtUtc}}); } return utils::Result<persistence::DatabaseRows>::success(rows); };
    // clang-format on
}

bool CodeEditorTestsHelper::writeTestFile(const QString& path, const QByteArray& content) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(content) == content.size() && file.flush();
}

TEST(FileSystemServiceTest, PerformsSerializedAtomicFileAndDirectoryOperations) {
    filesystem::FileSystemService service;
    QTemporaryDir root;
    ASSERT_TRUE(root.isValid());
    const QString file = QDir(root.path()).filePath(QStringLiteral("source.cpp"));
    const QString moved = QDir(root.path()).filePath(QStringLiteral("moved.cpp"));
    const QString folder = QDir(root.path()).filePath(QStringLiteral("folder"));

    EXPECT_TRUE(test::awaitFuture(service.createFile(file)).hasValue());
    EXPECT_EQ(test::awaitFuture(service.createFile(file)).error().code, QStringLiteral("filesystem_destination_exists"));
    EXPECT_TRUE(test::awaitFuture(service.writeFile(file, QByteArrayLiteral("int main() {}\n"))).hasValue());
    EXPECT_EQ(test::awaitFuture(service.readFile(file, 1024)).value(), QByteArrayLiteral("int main() {}\n"));
    EXPECT_EQ(test::awaitFuture(service.readFile(file, 2)).error().code, QStringLiteral("filesystem_file_too_large"));
    EXPECT_TRUE(test::awaitFuture(service.movePath(file, moved)).hasValue());
    EXPECT_TRUE(test::awaitFuture(service.createDirectory(folder)).hasValue());
    EXPECT_TRUE(test::awaitFuture(service.removeFile(moved)).hasValue());
    EXPECT_TRUE(test::awaitFuture(service.removeDirectory(folder)).hasValue());

    // Writing a file that is not there yet creates it, and creates the directories it needs, because nothing could write a new one otherwise.
    const QString created = QDir(root.path()).filePath(QStringLiteral("fresh.txt"));
    EXPECT_TRUE(test::awaitFuture(service.writeFile(created, QByteArrayLiteral("new"))).hasValue());
    EXPECT_EQ(test::awaitFuture(service.readFile(created, 1024)).value(), QByteArrayLiteral("new"));

    const QString nested = QDir(root.path()).filePath(QStringLiteral("one/two/page.html"));
    EXPECT_TRUE(test::awaitFuture(service.writeFile(nested, QByteArrayLiteral("<html/>"))).hasValue());
    EXPECT_EQ(test::awaitFuture(service.readFile(nested, 1024)).value(), QByteArrayLiteral("<html/>"));

    // A path that exists as something other than a regular file is still refused.
    EXPECT_EQ(test::awaitFuture(service.writeFile(root.path(), QByteArrayLiteral("x"))).error().code, QStringLiteral("filesystem_file_unavailable"));

    // Creating a directory creates every missing level and still refuses a destination that is already there.
    const QString levels = QDir(root.path()).filePath(QStringLiteral("deep/one/two"));
    EXPECT_TRUE(test::awaitFuture(service.createDirectory(levels)).hasValue());
    EXPECT_TRUE(QFileInfo(levels).isDir());
    EXPECT_EQ(test::awaitFuture(service.createDirectory(levels)).error().code, QStringLiteral("filesystem_destination_exists"));
}

TEST(FileSystemServiceTest, RejectsUnsafeInvalidAndUnavailablePaths) {
    filesystem::FileSystemService service;
    QTemporaryDir root;
    ASSERT_TRUE(root.isValid());
    EXPECT_EQ(test::awaitFuture(service.createFile(QStringLiteral("relative.txt"))).error().code, QStringLiteral("filesystem_path_invalid"));
    EXPECT_EQ(test::awaitFuture(service.removeDirectory(QDir::rootPath())).error().code, QStringLiteral("filesystem_path_unsafe"));
    EXPECT_EQ(test::awaitFuture(service.readFile(QDir(root.path()).filePath(QStringLiteral("missing")), 1024)).error().code, QStringLiteral("filesystem_file_missing"));
    EXPECT_EQ(test::awaitFuture(service.readFile(root.path(), 1024)).error().code, QStringLiteral("filesystem_file_unavailable"));
    EXPECT_EQ(test::awaitFuture(service.readFile(root.path(), 0)).error().code, QStringLiteral("filesystem_read_limit_invalid"));
}

TEST(LanguageRegistryTest, DetectsBuiltInLanguagesAndLanguageServersWithoutLanguagePlugins) {
    EXPECT_GE(LanguageRegistry::languages().size(), 15);
    EXPECT_EQ(LanguageRegistry::languageForPath(QStringLiteral("/project/CMakeLists.txt")).id, QStringLiteral("cmake"));
    EXPECT_EQ(LanguageRegistry::languageForPath(QStringLiteral("/project/main.cpp")).id, QStringLiteral("cpp"));
    EXPECT_EQ(LanguageRegistry::languageForPath(QStringLiteral("/project/app.tsx")).id, QStringLiteral("typescript"));
    EXPECT_EQ(LanguageRegistry::languageForPath(QStringLiteral("/project/unknown.data")).id, QStringLiteral("plaintext"));
    EXPECT_GE(LanguageRegistry::languageServers().size(), 12);
    const auto& python = LanguageRegistry::languageServers().at(2);
    ASSERT_EQ(python.candidates.size(), 3);
    EXPECT_EQ(python.candidates.last().executableName, QStringLiteral("pylsp"));
    EXPECT_TRUE(python.candidates.last().arguments.isEmpty());
}

TEST(CodeWorkspaceViewTest, OpensTheFileALocationPointsAtAndPlacesTheCursorOnItsLine) {
    filesystem::FileSystemService service;
    test::TestPluginHost host;
    host.useFileSystem(service);
    QTemporaryDir root;
    ASSERT_TRUE(root.isValid());
    const QString canonicalRoot = QFileInfo(root.path()).canonicalFilePath();
    const QString target = QDir(canonicalRoot).filePath(QStringLiteral("target.cpp"));
    ASSERT_TRUE(CodeEditorTestsHelper::writeTestFile(target, QByteArrayLiteral("one\ntwo\nthree\nfour\n")));
    const QDateTime now = QDateTime::currentDateTimeUtc();
    CodeWorkspaceView view({QStringLiteral("workspace"), canonicalRoot, 0, true, now, now, {}}, {}, false, CodeEditorFont{}, TextCharset::Latin1, host);
    auto* documents = view.findChild<QTabWidget*>(QStringLiteral("codeEditorDocuments"));
    ASSERT_NE(documents, nullptr);

    QSignalSpy failures(&view, &CodeWorkspaceView::operationFailed);
    view.openLocation(target, 2, 1);
    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { return documents->count() == 1 && qobject_cast<CodeDocument*>(documents->currentWidget())->cursorLine() == 3; }));
    // clang-format on
    EXPECT_EQ(qobject_cast<CodeDocument*>(documents->currentWidget())->cursorColumn(), 2);

    // A second location inside the file that is already open moves the same document instead of opening another tab.
    view.openLocation(target, 0, 0);
    EXPECT_EQ(documents->count(), 1);
    EXPECT_EQ(qobject_cast<CodeDocument*>(documents->currentWidget())->cursorLine(), 1);

    view.openLocation(QDir(QDir::tempPath()).filePath(QStringLiteral("outside.cpp")), 0, 0);
    EXPECT_EQ(documents->count(), 1);
    EXPECT_EQ(failures.count(), 1);
}

TEST(CodeEditorWidgetTest, MarksEveryDiagnosticAndCompletesWithTheTextTheServerDeclared) {
    CodeEditorWidget editor(ui::themeManager().theme());
    editor.setPlainText(QStringLiteral("int value = 0;\nvalue = 1;\n"));
    editor.show();

    editor.setDiagnostics({{QString{}, 0, 4, 0, 9, 1, QStringLiteral("unused variable")}, {QString{}, 1, 0, 1, 5, 2, QStringLiteral("assigned but never read")}});
    ASSERT_EQ(editor.extraSelections().size(), 3);
    EXPECT_EQ(editor.extraSelections().at(1).format.underlineStyle(), QTextCharFormat::WaveUnderline);
    EXPECT_EQ(editor.extraSelections().at(1).format.underlineColor(), ui::themeManager().theme().color(ui::ThemeColor::Danger));
    EXPECT_EQ(editor.extraSelections().at(2).format.underlineColor(), ui::themeManager().theme().color(ui::ThemeColor::Warning));

    // An occurrence of the symbol under the cursor is painted beside the diagnostics rather than replacing them.
    editor.setOccurrences({{QString{}, 0, 4, 0, 9}});
    EXPECT_EQ(editor.extraSelections().size(), 4);
    editor.setDiagnostics({});
    editor.setOccurrences({});
    EXPECT_EQ(editor.extraSelections().size(), 1);

    QTextCursor cursor = editor.textCursor();
    cursor.setPosition(9);
    editor.setTextCursor(cursor);
    CompletionProposal ranged;
    ranged.label = QStringLiteral("value(const int &other)");
    ranged.insertText = QStringLiteral("valueOf");
    ranged.hasRange = true;
    ranged.startLine = 0;
    ranged.startCharacter = 4;
    ranged.endLine = 0;
    ranged.endCharacter = 9;
    CompletionProposal plain;
    plain.label = QStringLiteral("other");
    plain.insertText = QStringLiteral("other");
    editor.showCompletions({ranged, plain});

    auto* completer = editor.findChild<QCompleter*>();
    ASSERT_NE(completer, nullptr);
    auto* popup = completer->popup();
    ASSERT_NE(popup, nullptr);
    EXPECT_EQ(popup->objectName(), QStringLiteral("codeEditorCompletion"));
    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { return popup->isVisible(); }));
    // clang-format on
    EXPECT_EQ(popup->model()->rowCount(), 1);

    popup->setFocus();
    QTest::keyClick(popup, Qt::Key_Enter);
    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { return editor.toPlainText().startsWith(QStringLiteral("int valueOf = 0;")); }));
    // clang-format on
    EXPECT_FALSE(popup->isVisible());
}

TEST(CodeWorkspaceViewTest, ReplacesALanguageServerThatGaveUpInsteadOfKeepingTheDeadOne) {
    filesystem::FileSystemService service;
    test::TestPluginHost host;
    host.useFileSystem(service);
    QTemporaryDir root;
    ASSERT_TRUE(root.isValid());
    const QString canonicalRoot = QFileInfo(root.path()).canonicalFilePath();
    const QString path = QDir(canonicalRoot).filePath(QStringLiteral("main.cpp"));
    ASSERT_TRUE(CodeEditorTestsHelper::writeTestFile(path, QByteArrayLiteral("int main() { return 0; }\n")));
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const ResolvedLanguageServer crashing{QStringLiteral("cpp"), QCoreApplication::applicationFilePath(), {QStringLiteral("--slotdeck-test-lsp-crash")}};
    CodeWorkspaceView view({QStringLiteral("workspace"), canonicalRoot, 0, true, now, now, {}}, {crashing}, false, CodeEditorFont{}, TextCharset::Latin1, host);

    QSignalSpy failures(&view, &CodeWorkspaceView::operationFailed);
    view.openFile(path);
    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { return view.findChildren<LanguageServerClient*>().size() == 1; }));
    ASSERT_TRUE(test::waitUntil([&]() { return view.findChildren<LanguageServerClient*>().isEmpty(); }, 60000));
    // clang-format on
    ASSERT_EQ(failures.count(), 1);
    EXPECT_TRUE(failures.first().first().toString().contains(QStringLiteral("restart limit")));
    ASSERT_FALSE(host.logs.isEmpty());
    EXPECT_TRUE(host.logs.last().message.contains(QStringLiteral("restart limit")));
}

TEST(CodeWorkspaceViewTest, PresentsTheAnalysisOfTheWorkspaceAndForgetsItWhenTheDocumentCloses) {
    filesystem::FileSystemService service;
    test::TestPluginHost host;
    host.useFileSystem(service);
    QTemporaryDir root;
    ASSERT_TRUE(root.isValid());
    const QString canonicalRoot = QFileInfo(root.path()).canonicalFilePath();
    const QString path = QDir(canonicalRoot).filePath(QStringLiteral("main.cpp"));
    ASSERT_TRUE(CodeEditorTestsHelper::writeTestFile(path, QByteArrayLiteral("int main() { return 0; }\n")));
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const ResolvedLanguageServer fixture{QStringLiteral("cpp"), QCoreApplication::applicationFilePath(), {QStringLiteral("--slotdeck-test-lsp")}};
    CodeWorkspaceView view({QStringLiteral("workspace"), canonicalRoot, 0, true, now, now, {}}, {fixture}, false, CodeEditorFont{}, TextCharset::Latin1, host);
    auto* documents = view.findChild<QTabWidget*>(QStringLiteral("codeEditorDocuments"));
    auto* problems = view.findChild<QTreeWidget*>(QStringLiteral("codeEditorProblems"));
    auto* references = view.findChild<QTreeWidget*>(QStringLiteral("codeEditorReferences"));
    auto* symbols = view.findChild<QTreeWidget*>(QStringLiteral("codeEditorSymbols"));
    auto* search = view.findChild<QLineEdit*>(QStringLiteral("codeEditorSymbolSearch"));
    ASSERT_NE(documents, nullptr);
    ASSERT_NE(problems, nullptr);
    ASSERT_NE(references, nullptr);
    ASSERT_NE(symbols, nullptr);
    ASSERT_NE(search, nullptr);

    view.openFile(path);
    ASSERT_EQ(documents->count(), 1);
    auto* opened = qobject_cast<CodeDocument*>(documents->widget(0));
    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { return problems->topLevelItemCount() == 2 && symbols->topLevelItemCount() == 1; }));
    // clang-format on
    EXPECT_EQ(symbols->topLevelItem(0)->text(0), QStringLiteral("main  int ()"));
    ASSERT_EQ(symbols->topLevelItem(0)->childCount(), 1);
    EXPECT_EQ(symbols->topLevelItem(0)->child(0)->text(0), QStringLiteral("value"));

    // The panel carries every file the workspace analysed, so a written filter is what narrows it.
    auto* problemFilter = view.findChild<QLineEdit*>(QStringLiteral("codeEditorProblemFilter"));
    ASSERT_NE(problemFilter, nullptr);
    const QString firstFile = problems->topLevelItem(0)->text(0);
    const QString firstMessage = problems->topLevelItem(0)->text(2);
    problemFilter->setText(QStringLiteral("nothing spells this"));
    EXPECT_EQ(problems->topLevelItemCount(), 0);
    problemFilter->setText(firstMessage);
    EXPECT_GE(problems->topLevelItemCount(), 1);
    problemFilter->setText(firstFile);
    EXPECT_GE(problems->topLevelItemCount(), 1);
    problemFilter->clear();
    EXPECT_EQ(problems->topLevelItemCount(), 2);

    // A reference answer fills its own surface, and the outline stays where it belongs.
    opened->requestSymbolQuery(SymbolQueryKind::References);
    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { return references->topLevelItemCount() == 1; }));
    // clang-format on
    EXPECT_EQ(references->topLevelItem(0)->text(1), QStringLiteral("4"));

    // A query searches the whole workspace and an empty one returns the outline of the open document.
    search->setText(QStringLiteral("main"));
    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { return symbols->topLevelItemCount() == 1 && symbols->topLevelItem(0)->text(0) == QStringLiteral("fixture::main"); }));
    // clang-format on
    search->clear();
    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { return symbols->topLevelItemCount() == 1 && symbols->topLevelItem(0)->text(0) == QStringLiteral("main  int ()"); }));
    // clang-format on

    // A rule file that appears changes how the open document is indented, without reopening it.
    EXPECT_EQ(opened->editor().indentWidth(), 4);
    EXPECT_TRUE(view.watchedEditorConfigPaths().contains(canonicalRoot));
    const QString editorConfig = QDir(canonicalRoot).filePath(QStringLiteral(".editorconfig"));
    ASSERT_TRUE(CodeEditorTestsHelper::writeTestFile(editorConfig, QByteArrayLiteral("root = true\n\n[*]\nindent_style = space\nindent_size = 7\n")));
    view.reloadEditorConfigs();
    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { return opened->editor().indentWidth() == 7; }));
    // clang-format on
    EXPECT_TRUE(view.watchedEditorConfigPaths().contains(editorConfig));

    // The rule file that disappears returns the document to the default, and the watch follows it.
    ASSERT_TRUE(QFile::remove(editorConfig));
    view.reloadEditorConfigs();
    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { return opened->editor().indentWidth() == 4; }));
    // clang-format on
    EXPECT_FALSE(view.watchedEditorConfigPaths().contains(editorConfig));

    // The surface shows the workspace, so a file the server reported on without anyone opening it is listed too.
    QStringList listed;

    for (int index = 0; index < problems->topLevelItemCount(); ++index) {
        listed.append(problems->topLevelItem(index)->text(0));
    }

    listed.sort();
    EXPECT_EQ(listed, QStringList({QStringLiteral("included.h"), QStringLiteral("main.cpp")}));

    // The outline belongs to the open document and leaves with it, while the problems belong to the workspace and stay until the server clears them.
    view.closeCurrentDocument();
    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { return documents->count() == 0; }));
    // clang-format on
    EXPECT_EQ(symbols->topLevelItemCount(), 0);
    EXPECT_EQ(problems->topLevelItemCount(), 2);
}

TEST(CodeDocumentTest, SendsOnlyWhatChangedToTheLanguageServerAndSavesThroughIt) {
    filesystem::FileSystemService service;
    test::TestPluginHost host;
    host.useFileSystem(service);
    QTemporaryDir root;
    ASSERT_TRUE(root.isValid());
    const QString path = QDir(root.path()).filePath(QStringLiteral("main.cpp"));
    ASSERT_TRUE(CodeEditorTestsHelper::writeTestFile(path, QByteArrayLiteral("int main() {}\n")));

    LanguageServerClient client({QStringLiteral("cpp"), QCoreApplication::applicationFilePath(), {QStringLiteral("--slotdeck-test-lsp")}}, root.path());
    CodeDocument document(path, root.path(), false, CodeEditorFont{}, TextCharset::Latin1, host);
    QStringList diagnostics;
    QSignalSpy loaded(&document, &CodeDocument::loaded);
    // clang-format off
    QObject::connect(&document, &CodeDocument::diagnosticsChanged, &document, [&diagnostics](const QString&, const QVector<LanguageDiagnostic>& published) { for (const auto& diagnostic : published) { diagnostics.append(diagnostic.message); } });
    ASSERT_TRUE(test::waitUntil([&]() { return loaded.count() >= 1; }));
    // clang-format on

    document.setLanguageServer(&client);
    client.start();
    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { return client.ready(); }));
    // clang-format on

    document.editor().moveCursor(QTextCursor::End);
    document.editor().insertPlainText(QStringLiteral("void run() {}"));
    // clang-format off
    const auto changeEcho = [&diagnostics]() { return diagnostics.filter(QStringLiteral("void run() {}")).value(0); };
    ASSERT_TRUE(test::waitUntil([&]() { return !changeEcho().isEmpty(); }));
    // clang-format on
    EXPECT_TRUE(changeEcho().contains(QStringLiteral("\"line\":1")));
    EXPECT_FALSE(changeEcho().contains(QStringLiteral("int main")));

    document.save();
    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { return !document.dirty(); }));
    // clang-format on

    QSignalSpy stopped(&client, &LanguageServerClient::stopped);
    document.setLanguageServer(nullptr);
    client.stop();
    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { return stopped.count() == 1; }));
    // clang-format on
}

TEST(LanguageServerClientTest, ExchangesInitializationDiagnosticsAndCompletionsOverStandardIo) {
    QTemporaryDir root;
    ASSERT_TRUE(root.isValid());
    const QString path = QDir(root.path()).filePath(QStringLiteral("main.cpp"));
    LanguageServerClient client({QStringLiteral("cpp"), QCoreApplication::applicationFilePath(), {QStringLiteral("--slotdeck-test-lsp")}}, root.path());
    QVector<LanguageDiagnostic> receivedDiagnostics;
    QVector<CompletionProposal> receivedCompletions;
    QString serverError;
    QStringList serverLogs;
    // clang-format off
    QObject::connect(&client, &LanguageServerClient::diagnosticsPublished, &client, [&receivedDiagnostics](const QString&, const QVector<LanguageDiagnostic>& diagnostics) { receivedDiagnostics = diagnostics; });
    QObject::connect(&client, &LanguageServerClient::completionsReady, &client, [&receivedCompletions](const QString&, const QVector<CompletionProposal>& completions) { receivedCompletions = completions; });
    QObject::connect(&client, &LanguageServerClient::serverError, &client, [&serverError](const QString& message) { serverError = message; });
    QObject::connect(&client, &LanguageServerClient::serverLog, &client, [&serverLogs](const QString& message) { serverLogs.append(message); });
    // clang-format on

    client.openDocument(path, QStringLiteral("int main() {}"), QStringLiteral("cpp"));
    client.start();
    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { return receivedDiagnostics.size() == 1; }));
    // clang-format on
    EXPECT_EQ(receivedDiagnostics.first().message, QStringLiteral("Fixture diagnostic"));
    EXPECT_TRUE(serverError.isEmpty());

    // The standard error stream carries the server own diagnostic log and never reaches the user as a failure.
    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { return !serverLogs.isEmpty(); }));
    // clang-format on
    EXPECT_TRUE(serverLogs.first().contains(QStringLiteral("ASTWorker building file")));
    EXPECT_TRUE(serverError.isEmpty());

    // A server request carrying the identity we used for our own initialize is answered instead of being read as our response.
    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { return receivedDiagnostics.size() == 1 && receivedDiagnostics.first().message == QStringLiteral("server request answered"); }));
    // clang-format on

    EXPECT_EQ(client.completionTriggerCharacters(), QStringList{QStringLiteral(".")});
    EXPECT_EQ(client.signatureHelpTriggerCharacters(), QStringList{QStringLiteral("(")});
    EXPECT_TRUE(client.supports(SymbolQueryKind::Definition));
    EXPECT_TRUE(client.supports(SymbolQueryKind::References));
    EXPECT_TRUE(client.supports(SymbolQueryKind::Declaration));
    EXPECT_TRUE(client.supports(SymbolQueryKind::TypeDefinition));
    EXPECT_TRUE(client.supports(SymbolQueryKind::Implementation));

    client.requestCompletion(path, 0, 3);
    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { return receivedCompletions.size() == 2; }));
    // clang-format on
    EXPECT_EQ(receivedCompletions.first().label, QStringLiteral("push_back(const value_type &value)"));
    EXPECT_EQ(receivedCompletions.first().insertText, QStringLiteral("push_back"));
    EXPECT_TRUE(receivedCompletions.first().hasRange);
    EXPECT_EQ(receivedCompletions.first().startCharacter, 4);
    EXPECT_EQ(receivedCompletions.last().insertText, QStringLiteral("completion_item"));
    EXPECT_FALSE(receivedCompletions.last().hasRange);

    // Only the range that really changed travels, and the answer proves the server received the edit it can apply.
    EXPECT_TRUE(client.editDocument(path, {{4, 4, QStringLiteral("start")}}));
    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { return receivedDiagnostics.size() == 1 && receivedDiagnostics.first().message.contains(QStringLiteral("\"start\"")); }));
    // clang-format on
    EXPECT_TRUE(receivedDiagnostics.first().message.contains(QStringLiteral("\"character\":4")));
    EXPECT_TRUE(receivedDiagnostics.first().message.contains(QStringLiteral("\"rangeLength\":4")));

    // An edit that does not fit the copy the server holds is refused so the caller can replace it instead of drifting apart.
    EXPECT_FALSE(client.editDocument(path, {{9000, 1, QStringLiteral("x")}}));
    EXPECT_FALSE(client.editDocument(path, {{0, -1, QStringLiteral("x")}}));
    EXPECT_TRUE(client.editDocument(path, {}));
    EXPECT_FALSE(client.editDocument(QDir(root.path()).filePath(QStringLiteral("other.cpp")), {{0, 0, QStringLiteral("x")}}));

    // A multi line edit reports the end of the range it removed without scanning the file again.
    EXPECT_TRUE(client.editDocument(path, {{0, 0, QStringLiteral("one\ntwo\n")}}));
    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { return receivedDiagnostics.size() == 1 && receivedDiagnostics.first().message.contains(QStringLiteral("one")); }));
    // clang-format on
    EXPECT_TRUE(client.editDocument(path, {{2, 6, QStringLiteral("")}}));
    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { return receivedDiagnostics.size() == 1 && receivedDiagnostics.first().message.contains(QStringLiteral("\"rangeLength\":6")); }));
    // clang-format on
    EXPECT_TRUE(receivedDiagnostics.first().message.contains(QStringLiteral("\"start\":{\"character\":2,\"line\":0}")));
    EXPECT_TRUE(receivedDiagnostics.first().message.contains(QStringLiteral("\"end\":{\"character\":0,\"line\":2}")));

    SourceLocation definition;
    QString hover;
    // clang-format off
    QObject::connect(&client, &LanguageServerClient::symbolLocationsReady, &client, [&definition](const QString&, SymbolQueryKind, const QVector<SourceLocation>& locations) { if (!locations.isEmpty()) { definition = locations.first(); } });
    QObject::connect(&client, &LanguageServerClient::hoverReady, &client, [&hover](const QString&, const QString& contents) { hover = contents; });
    // clang-format on
    client.requestSymbolQuery(path, 0, 4, SymbolQueryKind::Definition);
    client.requestHover(path, 0, 4);
    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { return !definition.path.isEmpty() && !hover.isEmpty(); }));
    // clang-format on
    EXPECT_EQ(definition.path, path);
    EXPECT_EQ(definition.line, 7);
    EXPECT_EQ(hover, QStringLiteral("int main()"));

    // Every answer that explains the file is asked for and understood, from the outline to the tokens the server names.
    QVector<DocumentSymbolNode> outline;
    QVector<WorkspaceSymbolEntry> workspaceSymbols;
    QVector<SourceLocation> highlights;
    QVector<SourceLocation> references;
    QVector<SemanticToken> tokens;
    SignatureHelpInfo signatureHelp;
    // clang-format off
    QObject::connect(&client, &LanguageServerClient::documentSymbolsReady, &client, [&outline](const QString&, const QVector<DocumentSymbolNode>& symbols) { outline = symbols; });
    QObject::connect(&client, &LanguageServerClient::workspaceSymbolsReady, &client, [&workspaceSymbols](const QVector<WorkspaceSymbolEntry>& symbols) { workspaceSymbols = symbols; });
    QObject::connect(&client, &LanguageServerClient::documentHighlightsReady, &client, [&highlights](const QString&, const QVector<SourceLocation>& entries) { highlights = entries; });
    QObject::connect(&client, &LanguageServerClient::semanticTokensReady, &client, [&tokens](const QString&, const QVector<SemanticToken>& entries) { tokens = entries; });
    QObject::connect(&client, &LanguageServerClient::signatureHelpReady, &client, [&signatureHelp](const QString&, const SignatureHelpInfo& help) { signatureHelp = help; });
    // clang-format on
    client.requestDocumentSymbols(path);
    client.requestWorkspaceSymbols(QStringLiteral("main"));
    client.requestDocumentHighlights(path, 0, 4);
    client.requestSemanticTokens(path);
    client.requestSignatureHelp(path, 0, 9);
    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { return !outline.isEmpty() && !workspaceSymbols.isEmpty() && !highlights.isEmpty() && !tokens.isEmpty() && !signatureHelp.signatures.isEmpty(); }));
    // clang-format on
    EXPECT_EQ(outline.first().name, QStringLiteral("main"));
    EXPECT_EQ(outline.first().detail, QStringLiteral("int ()"));
    ASSERT_EQ(outline.first().children.size(), 1);
    EXPECT_EQ(outline.first().children.first().name, QStringLiteral("value"));
    EXPECT_EQ(workspaceSymbols.first().container, QStringLiteral("fixture"));
    EXPECT_EQ(workspaceSymbols.first().location.line, 5);
    EXPECT_EQ(highlights.first().character, 4);
    EXPECT_EQ(highlights.first().path, path);
    ASSERT_EQ(tokens.size(), 2);
    EXPECT_EQ(tokens.first().type, QStringLiteral("function"));
    EXPECT_EQ(tokens.first().startCharacter, 4);
    EXPECT_EQ(tokens.last().type, QStringLiteral("namespace"));
    EXPECT_EQ(tokens.last().line, 1);
    EXPECT_EQ(tokens.last().startCharacter, 2);
    EXPECT_EQ(signatureHelp.signatures.first(), QStringLiteral("main(int argc, char** argv)"));
    EXPECT_EQ(signatureHelp.activeParameter, 1);

    client.requestSymbolQuery(path, 0, 4, SymbolQueryKind::References);
    // clang-format off
    QObject::connect(&client, &LanguageServerClient::symbolLocationsReady, &client, [&references](const QString&, SymbolQueryKind kind, const QVector<SourceLocation>& locations) { if (kind == SymbolQueryKind::References) { references = locations; } });
    ASSERT_TRUE(test::waitUntil([&]() { return !references.isEmpty(); }));
    // clang-format on
    EXPECT_EQ(references.first().line, 3);

    // A server that only answers diagnostics when asked still fills the same surface.
    client.requestDiagnostics(path);
    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { return !receivedDiagnostics.isEmpty() && receivedDiagnostics.first().message == QStringLiteral("pulled diagnostic"); }));
    // clang-format on
    EXPECT_EQ(receivedDiagnostics.first().severity, 1);

    // A save reaches the server with the text it asked to receive, and the message it shows as an error is reported as one.
    client.saveDocument(path, QStringLiteral("int start() {}"));
    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { return serverError == QStringLiteral("fixture failure"); }));
    ASSERT_TRUE(test::waitUntil([&]() { return serverLogs.contains(QStringLiteral("saved int start() {}")); }));
    // clang-format on

    QSignalSpy stopped(&client, &LanguageServerClient::stopped);
    client.stop();
    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { return stopped.count() == 1; }));
    // clang-format on
}

TEST(LanguageServerClientTest, RestartsACrashedServerInsideItsBudgetAndReportsTheLimit) {
    QTemporaryDir root;
    ASSERT_TRUE(root.isValid());
    const QString path = QDir(root.path()).filePath(QStringLiteral("main.cpp"));
    LanguageServerClient client({QStringLiteral("cpp"), QCoreApplication::applicationFilePath(), {QStringLiteral("--slotdeck-test-lsp-crash")}}, root.path());
    QStringList restarts;
    QSignalSpy error(&client, &LanguageServerClient::serverError);
    QSignalSpy stopped(&client, &LanguageServerClient::stopped);
    // clang-format off
    QObject::connect(&client, &LanguageServerClient::serverLog, &client, [&restarts](const QString& message) { if (message.contains(QStringLiteral("started again"))) { restarts.append(message); } });
    // clang-format on

    client.openDocument(path, QStringLiteral("int main() {}"), QStringLiteral("cpp"));
    client.start();
    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { return error.count() == 1; }));
    // clang-format on
    EXPECT_EQ(restarts.size(), 5);
    EXPECT_TRUE(error.first().first().toString().contains(QStringLiteral("restart limit")));
    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { return stopped.count() == 1; }));
    // clang-format on
}

TEST(LanguageServerClientTest, ResolvesTheAddressTheServerAnswersBackToTheOpenDocument) {
    QTemporaryDir root;
    ASSERT_TRUE(root.isValid());
    const QString path = QDir(root.path()).filePath(QStringLiteral("main.cpp"));
    LanguageServerClient client({QStringLiteral("cpp"), QCoreApplication::applicationFilePath(), {QStringLiteral("--slotdeck-test-lsp")}}, root.path());
    QVector<LanguageDiagnostic> diagnostics;
    // clang-format off
    QObject::connect(&client, &LanguageServerClient::diagnosticsPublished, &client, [&diagnostics](const QString& published, const QVector<LanguageDiagnostic>& entries) { if (!entries.isEmpty() && entries.first().path == published) { diagnostics = entries; } });
    // clang-format on

    client.openDocument(path, QStringLiteral("int main() {}"), QStringLiteral("cpp"));
    client.start();
    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { return !diagnostics.isEmpty(); }));
    // clang-format on
    EXPECT_EQ(diagnostics.first().path, path);
    EXPECT_TRUE(samePath(path, path));
    EXPECT_FALSE(samePath(path, path + QStringLiteral("x")));

    QSignalSpy stopped(&client, &LanguageServerClient::stopped);
    client.stop();
    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { return stopped.count() == 1; }));
    // clang-format on
}

TEST(LanguageServerClientTest, GivesUpOnAServerThatNeverAnswersInitializationInsteadOfWaitingForever) {
    QTemporaryDir root;
    ASSERT_TRUE(root.isValid());
    const QString path = QDir(root.path()).filePath(QStringLiteral("main.cpp"));
    LanguageServerClient client({QStringLiteral("cpp"), QCoreApplication::applicationFilePath(), {QStringLiteral("--slotdeck-test-lsp-silent")}}, root.path());
    QStringList logs;
    QSignalSpy error(&client, &LanguageServerClient::serverError);
    // clang-format off
    QObject::connect(&client, &LanguageServerClient::serverLog, &client, [&logs](const QString& message) { logs.append(message); });
    // clang-format on

    client.openDocument(path, QStringLiteral("int main() {}"), QStringLiteral("cpp"));
    client.start();
    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { return !logs.filter(QStringLiteral("did not answer initialization")).isEmpty() && !logs.filter(QStringLiteral("started again")).isEmpty(); }, 60000));
    // clang-format on
    EXPECT_FALSE(client.ready());
    EXPECT_EQ(error.count(), 0);

    QSignalSpy stopped(&client, &LanguageServerClient::stopped);
    client.stop();
    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { return stopped.count() == 1; }));
    // clang-format on
}

// A transport thread outlives the client that asked it to end, so the suite waits for the ones still finishing instead of exiting under them.
class LanguageServerTransportDrain final : public testing::Environment {
  public:
    void TearDown() override {
        LanguageServerClient::drainTransports();
    }
};

const bool languageServerTransportDrainRegistered = testing::AddGlobalTestEnvironment(new LanguageServerTransportDrain) != nullptr;

TEST(LanguageServerClientTest, KeepsEveryRequestInsideTheCapabilitiesTheServerDeclares) {
    QTemporaryDir root;
    ASSERT_TRUE(root.isValid());
    const QString path = QDir(root.path()).filePath(QStringLiteral("main.cpp"));
    LanguageServerClient client({QStringLiteral("cpp"), QDir(root.path()).filePath(QStringLiteral("missing-server")), {}}, root.path());

    // Nothing reaches a server that never started, and no request is queued behind a process that will never answer.
    client.openDocument(path, QStringLiteral("int main() {}"), QStringLiteral("cpp"));
    client.requestCompletion(path, 0, 3);
    client.requestSymbolQuery(path, 0, 3, SymbolQueryKind::Definition);
    client.requestHover(path, 0, 3);
    client.editDocument(path, {{0, 0, QStringLiteral("x")}});
    client.saveDocument(path, QStringLiteral("x"));
    EXPECT_FALSE(client.ready());
    EXPECT_FALSE(client.supports(SymbolQueryKind::Definition));
    EXPECT_FALSE(client.supports(SymbolQueryKind::References));
    EXPECT_FALSE(client.supports(SymbolQueryKind::Declaration));
    EXPECT_TRUE(client.completionTriggerCharacters().isEmpty());
    EXPECT_TRUE(client.signatureHelpTriggerCharacters().isEmpty());
}

TEST(LanguageServerTransportTest, KillsTheServerItOwnsWhenItIsDestroyed) {
    QTemporaryDir root;
    ASSERT_TRUE(root.isValid());
    const ResolvedLanguageServer server{QStringLiteral("cpp"), test::shellExecutable(), test::shellArguments(test::sleepingCommand(30))};

    static QStringList warnings;
    warnings.clear();
    // clang-format off
    QtMessageHandler previous = qInstallMessageHandler([](QtMsgType type, const QMessageLogContext&, const QString& text) { if (type == QtWarningMsg) { warnings.append(text); } });
    // clang-format on

    QPointer<QProcess> child;
    {
        LanguageServerTransport transport(server, root.path());
        QSignalSpy started(&transport, &LanguageServerTransport::started);
        transport.start();
        // clang-format off
        ASSERT_TRUE(test::waitUntil([&started]() { return started.count() == 1; }));
        // clang-format on
        child = transport.findChild<QProcess*>();
        ASSERT_FALSE(child.isNull());
    }

    // The server dies with the transport that owns it, so Qt never has to clean up a process that is still running.
    qInstallMessageHandler(previous);
    // clang-format off
    const bool emergencyCleanup = std::ranges::any_of(warnings, [](const QString& text) { return text.contains(QStringLiteral("Destroyed while process")); });
    // clang-format on
    EXPECT_FALSE(emergencyCleanup) << warnings.join(QStringLiteral(" | ")).toStdString();
    // clang-format off
    ASSERT_TRUE(test::waitUntil([&child]() { return child.isNull(); }));
    // clang-format on
}

TEST(LanguageServerClientTest, ReportsProcessStartupErrorsWithoutBlocking) {
    QTemporaryDir root;
    ASSERT_TRUE(root.isValid());
    LanguageServerClient client({QStringLiteral("cpp"), QDir(root.path()).filePath(QStringLiteral("missing-server")), {}}, root.path());
    QSignalSpy error(&client, &LanguageServerClient::serverError);
    client.start();
    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { return error.count() == 1; }));
    // clang-format on
    EXPECT_FALSE(error.first().first().toString().isEmpty());
}

TEST(LanguageServerClientTest, RejectsMalformedProtocolFramesAndStopsTheServer) {
    QTemporaryDir root;
    ASSERT_TRUE(root.isValid());
    LanguageServerClient client({QStringLiteral("cpp"), QCoreApplication::applicationFilePath(), {QStringLiteral("--slotdeck-test-lsp-invalid")}}, root.path());
    QSignalSpy error(&client, &LanguageServerClient::serverError);
    QSignalSpy stopped(&client, &LanguageServerClient::stopped);
    client.start();
    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { return error.count() >= 1; }));
    // clang-format on
    EXPECT_TRUE(error.first().first().toString().contains(QStringLiteral("content length")));
    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { return stopped.count() == 1; }));
    // clang-format on
}

TEST(EditorConfigTest, MatchesDocumentedGlobsAndResolvesNearestProperties) {
    const QString projectRoot = QDir::cleanPath(QDir::rootPath() + QStringLiteral("project"));
    EXPECT_TRUE(editorConfigSectionMatches(QStringLiteral("*.cpp"), projectRoot, projectRoot + QStringLiteral("/src/main.cpp")));
    EXPECT_TRUE(editorConfigSectionMatches(QStringLiteral("*.{cpp,h}"), projectRoot, projectRoot + QStringLiteral("/main.h")));
    EXPECT_TRUE(editorConfigSectionMatches(QStringLiteral("src/**/*.py"), projectRoot, projectRoot + QStringLiteral("/src/a/b/run.py")));
    EXPECT_TRUE(editorConfigSectionMatches(QStringLiteral("file?.txt"), projectRoot, projectRoot + QStringLiteral("/file1.txt")));
    EXPECT_TRUE(editorConfigSectionMatches(QStringLiteral("page{1..3}.md"), projectRoot, projectRoot + QStringLiteral("/page2.md")));
    EXPECT_TRUE(editorConfigSectionMatches(QStringLiteral("/root.txt"), projectRoot, projectRoot + QStringLiteral("/root.txt")));
    EXPECT_FALSE(editorConfigSectionMatches(QStringLiteral("/root.txt"), projectRoot, projectRoot + QStringLiteral("/nested/root.txt")));
    EXPECT_FALSE(editorConfigSectionMatches(QStringLiteral("[!a]b.txt"), projectRoot, projectRoot + QStringLiteral("/ab.txt")));
    EXPECT_TRUE(editorConfigSectionMatches(QStringLiteral("[!a]b.txt"), projectRoot, projectRoot + QStringLiteral("/cb.txt")));
    EXPECT_FALSE(editorConfigSectionMatches(QStringLiteral("*.cpp"), projectRoot, projectRoot + QStringLiteral("/../other/main.cpp")));

    const QVector<EditorConfigFile> files{{projectRoot + QStringLiteral("/src"), QStringLiteral("[*.cpp]\nindent_size = 2\ntrim_trailing_whitespace = true\n")}, {projectRoot, QStringLiteral("root = true\n[*]\nindent_style = space\nindent_size = 8\nend_of_line = crlf\ninsert_final_newline = true\n")}, {QStringLiteral("/"), QStringLiteral("[*]\nindent_size = 64\n")}};
    const auto properties = resolveEditorConfig(projectRoot + QStringLiteral("/src/main.cpp"), files);
    EXPECT_EQ(properties.indentStyle.value(), IndentStyle::Space);
    EXPECT_EQ(properties.indentSize.value(), 2);
    EXPECT_EQ(properties.lineEnding.value(), LineEnding::Crlf);
    EXPECT_TRUE(properties.trimTrailingWhitespace.value());
    EXPECT_TRUE(properties.insertFinalNewline.value());
    EXPECT_EQ(resolvedIndentWidth(properties), 2);

    const QVector<EditorConfigFile> tabs{{projectRoot, QStringLiteral("[*]\nindent_style = tab\ntab_width = 8\ncharset = latin1\nmax_line_length = off\n")}};
    const auto tabProperties = resolveEditorConfig(projectRoot + QStringLiteral("/main.go"), tabs);
    EXPECT_EQ(tabProperties.indentStyle.value(), IndentStyle::Tab);
    EXPECT_EQ(resolvedIndentWidth(tabProperties), 8);
    EXPECT_EQ(tabProperties.charset.value(), TextCharset::Latin1);
    EXPECT_FALSE(tabProperties.maximumLineLength.has_value());
    EXPECT_TRUE(tabProperties.unsupportedCharsets.isEmpty());

    // A value of unset undoes what a file above declared, which is the whole reason the specification has it.
    const QVector<EditorConfigFile> cleared{{projectRoot + QStringLiteral("/src"), QStringLiteral("[*]\nindent_size = unset\ntrim_trailing_whitespace = unset\n")}, {projectRoot, QStringLiteral("root = true\n[*]\nindent_size = 4\ntrim_trailing_whitespace = true\n")}};
    const auto clearedProperties = resolveEditorConfig(projectRoot + QStringLiteral("/src/main.cpp"), cleared);
    EXPECT_FALSE(clearedProperties.indentSize.has_value());
    EXPECT_FALSE(clearedProperties.trimTrailingWhitespace.has_value());

    // An indent size of tab reads the tab width of the same file whatever order the two were written in.
    const QVector<EditorConfigFile> reordered{{projectRoot, QStringLiteral("[*]\nindent_size = tab\ntab_width = 3\n")}};
    EXPECT_EQ(resolveEditorConfig(projectRoot + QStringLiteral("/main.go"), reordered).indentSize.value(), 3);

    // A brace expression with neither a comma nor a range is the literal text it spells.
    EXPECT_TRUE(editorConfigSectionMatches(QStringLiteral("{single}.b"), projectRoot, projectRoot + QStringLiteral("/{single}.b")));
    EXPECT_FALSE(editorConfigSectionMatches(QStringLiteral("{single}.b"), projectRoot, projectRoot + QStringLiteral("/single.b")));

    const QStringList paths = editorConfigSearchPaths(projectRoot + QStringLiteral("/src/a/main.cpp"), projectRoot);
    EXPECT_EQ(paths, QStringList({projectRoot + QStringLiteral("/src/a/.editorconfig"), projectRoot + QStringLiteral("/src/.editorconfig"), projectRoot + QStringLiteral("/.editorconfig")}));
    EXPECT_TRUE(editorConfigSearchPaths(projectRoot + QStringLiteral("/../outside/main.cpp"), projectRoot).isEmpty());
}

// The cases below are the ones the EditorConfig core test suite defines, so this suite answers them exactly as every conforming implementation does.
TEST(EditorConfigTest, AnswersTheCasesTheCoreTestSuiteDefines) {
    const QString root = QStringLiteral("/project");
    struct Case final {
        QString pattern;
        QString name;
        bool matches;
    };

    const QVector<Case> cases{
        {QStringLiteral("*.{py,js,html}"), QStringLiteral("test.py"), true}, {QStringLiteral("*.{py,js,html}"), QStringLiteral("test.pyc"), false}, {QStringLiteral("{single}.b"), QStringLiteral("{single}.b"), true}, {QStringLiteral("{single}.b"), QStringLiteral(".b"), false}, {QStringLiteral("{}.c"), QStringLiteral("{}.c"), true}, {QStringLiteral("{}.c"), QStringLiteral(".c"), false}, {QStringLiteral("a{b,c,}.d"), QStringLiteral("a.d"), true}, {QStringLiteral("a{b,c,}.d"), QStringLiteral("ab.d"), true}, {QStringLiteral("a{b,c,}.d"), QStringLiteral("ac.d"), true}, {QStringLiteral("a{b,c,}.d"), QStringLiteral("a,.d"), false}, {QStringLiteral("a{,b,,c,}.e"), QStringLiteral("a.e"), true}, {QStringLiteral("a{,b,,c,}.e"), QStringLiteral("ab.e"), true}, {QStringLiteral("a{,b,,c,}.e"), QStringLiteral("a,.e"), false}, {QStringLiteral("{.f"), QStringLiteral("{.f"), true}, {QStringLiteral("{.f"), QStringLiteral(".f"), false}, {QStringLiteral("{word,{also},this}.g"), QStringLiteral("word.g"), true}, {QStringLiteral("{word,{also},this}.g"), QStringLiteral("{also}.g"), true}, {QStringLiteral("{word,{also},this}.g"), QStringLiteral("this.g"), true}, {QStringLiteral("{word,{also},this}.g"), QStringLiteral("{also,this}.g"), false}, {QStringLiteral("{{a,b},c}.k"), QStringLiteral("a.k"), true}, {QStringLiteral("{{a,b},c}.k"), QStringLiteral("c.k"), true}, {QStringLiteral("{{a,b},c}.k"), QStringLiteral("{a,b}.k"), false}, {QStringLiteral("{a,{b,c}}.l"), QStringLiteral("a.l"), true}, {QStringLiteral("{a,{b,c}}.l"), QStringLiteral("c.l"), true}, {QStringLiteral("{a,{b,c}}.l"), QStringLiteral("{b,c}.l"), false}, {QStringLiteral("{a\\,b,cd}.txt"), QStringLiteral("a,b.txt"), true}, {QStringLiteral("{a\\,b,cd}.txt"), QStringLiteral("cd.txt"), true}, {QStringLiteral("{a\\,b,cd}.txt"), QStringLiteral("a.txt"), false}, {QStringLiteral("{e,\\},f}.txt"), QStringLiteral("e.txt"), true}, {QStringLiteral("{e,\\},f}.txt"), QStringLiteral("}.txt"), true}, {QStringLiteral("{e,\\},f}.txt"), QStringLiteral("f.txt"), true}, {QStringLiteral("{3..120}"), QStringLiteral("3"), true}, {QStringLiteral("{3..120}"), QStringLiteral("15"), true}, {QStringLiteral("{3..120}"), QStringLiteral("120"), true}, {QStringLiteral("{3..120}"), QStringLiteral("1"), false}, {QStringLiteral("{3..120}"), QStringLiteral("121"), false}, {QStringLiteral("{3..120}"), QStringLiteral("5a"), false}, {QStringLiteral("{3..120}"), QStringLiteral("060"), false}, {QStringLiteral("{aardvark..antelope}"), QStringLiteral("{aardvark..antelope}"), true}, {QStringLiteral("{aardvark..antelope}"), QStringLiteral("aardvark"), false}, {QStringLiteral("{aardvark..antelope}"), QStringLiteral("antelope"), false},
    };

    for (const auto& sample : cases) {
        EXPECT_EQ(editorConfigSectionMatches(sample.pattern, root, root + QLatin1Char('/') + sample.name), sample.matches) << sample.pattern.toStdString() << " against " << sample.name.toStdString();
    }

    const QVector<Case> globs{
        {QStringLiteral("a*e.c"), QStringLiteral("ace.c"), true}, {QStringLiteral("a*e.c"), QStringLiteral("ae.c"), true}, {QStringLiteral("a*e.c"), QStringLiteral("abcde.c"), true}, {QStringLiteral("a*e.c"), QStringLiteral("a/e.c"), false}, {QStringLiteral("Bar/*"), QStringLiteral("Bar/foo.txt"), true}, {QStringLiteral("Bar/*"), QStringLiteral("Bar/.editorconfig"), true}, {QStringLiteral("Bar/*"), QStringLiteral("bat/Bar/foo.txt"), false}, {QStringLiteral("*"), QStringLiteral(".editorconfig"), true}, {QStringLiteral("som?.c"), QStringLiteral("some.c"), true}, {QStringLiteral("som?.c"), QStringLiteral("som.c"), false}, {QStringLiteral("som?.c"), QStringLiteral("something.c"), false}, {QStringLiteral("som?.c"), QStringLiteral("som/.c"), false}, {QStringLiteral("[ab].a"), QStringLiteral("a.a"), true}, {QStringLiteral("[ab].a"), QStringLiteral("c.a"), false}, {QStringLiteral("[!ab].b"), QStringLiteral("c.b"), true}, {QStringLiteral("[!ab].b"), QStringLiteral("a.b"), false}, {QStringLiteral("[d-g].c"), QStringLiteral("f.c"), true}, {QStringLiteral("[d-g].c"), QStringLiteral("h.c"), false}, {QStringLiteral("[!d-g].d"), QStringLiteral("h.d"), true}, {QStringLiteral("[!d-g].d"), QStringLiteral("f.d"), false}, {QStringLiteral("[abd-g].e"), QStringLiteral("e.e"), true}, {QStringLiteral("[-ab].f"), QStringLiteral("-.f"), true}, {QStringLiteral("[\\]ab].g"), QStringLiteral("].g"), true}, {QStringLiteral("[ab]].g"), QStringLiteral("b].g"), true}, {QStringLiteral("[!\\]ab].g"), QStringLiteral("c.g"), true}, {QStringLiteral("[!ab]].g"), QStringLiteral("c].g"), true}, {QStringLiteral("ab[e/]cd.i"), QStringLiteral("ab[e/]cd.i"), true}, {QStringLiteral("ab[e/]cd.i"), QStringLiteral("ab/cd.i"), false}, {QStringLiteral("ab[e/]cd.i"), QStringLiteral("abecd.i"), false}, {QStringLiteral("ab[/c"), QStringLiteral("ab[/c"), true}, {QStringLiteral("a**z.c"), QStringLiteral("a/z.c"), true}, {QStringLiteral("a**z.c"), QStringLiteral("amnz.c"), true}, {QStringLiteral("a**z.c"), QStringLiteral("a/mn/z.c"), true}, {QStringLiteral("b/**z.c"), QStringLiteral("b/z.c"), true}, {QStringLiteral("b/**z.c"), QStringLiteral("b/mn/z.c"), true}, {QStringLiteral("b/**z.c"), QStringLiteral("bmnz.c"), false}, {QStringLiteral("c**/z.c"), QStringLiteral("c/z.c"), true}, {QStringLiteral("c**/z.c"), QStringLiteral("cmn/z.c"), true}, {QStringLiteral("c**/z.c"), QStringLiteral("cm/nz.c"), false}, {QStringLiteral("d/**/z.c"), QStringLiteral("d/z.c"), true}, {QStringLiteral("d/**/z.c"), QStringLiteral("d/mn/z.c"), true}, {QStringLiteral("d/**/z.c"), QStringLiteral("d/mnz.c"), false}, {QStringLiteral("d/**/z.c"), QStringLiteral("dmn/z.c"), false}, {QStringLiteral("*"), QString::fromUtf8("\u4e2d\u6587.txt"), true},
    };

    for (const auto& sample : globs) {
        EXPECT_EQ(editorConfigSectionMatches(sample.pattern, root, root + QLatin1Char('/') + sample.name), sample.matches) << sample.pattern.toStdString() << " against " << sample.name.toStdString();
    }

    // A hash or a semicolon starts a comment only at the beginning of a line, so anywhere else it is the text it spells.
    const QVector<EditorConfigFile> comments{{root, QStringLiteral("root = true\n; a comment\n# another comment\n[*.c]\nindent_size = 3\n[test#.c]\nindent_size = 7\n")}};
    EXPECT_EQ(resolveEditorConfig(root + QStringLiteral("/plain.c"), comments).indentSize.value(), 3);
    EXPECT_EQ(resolveEditorConfig(root + QStringLiteral("/test#.c"), comments).indentSize.value(), 7);

    // Whitespace outside the brackets belongs to nobody, while whitespace inside them belongs to the pattern.
    const QVector<EditorConfigFile> whitespace{{root, QStringLiteral("root = true\n   [ spaced .c ]   \n   indent_size   =   5   \n")}};
    EXPECT_EQ(resolveEditorConfig(root + QStringLiteral("/ spaced .c "), whitespace).indentSize.value(), 5);
    EXPECT_FALSE(resolveEditorConfig(root + QStringLiteral("/spaced.c"), whitespace).indentSize.has_value());

    // A section written twice applies both times, and a section name keeps the case it was written in.
    const QVector<EditorConfigFile> repeated{{root, QStringLiteral("root = true\n[*.c]\nindent_size = 2\n[*.c]\ntab_width = 6\n[Upper.c]\ninsert_final_newline = true\n")}};
    const auto repeatedProperties = resolveEditorConfig(root + QStringLiteral("/upper.c"), repeated);
    EXPECT_EQ(repeatedProperties.indentSize.value(), 2);
    EXPECT_EQ(repeatedProperties.tabWidth.value(), 6);
    EXPECT_FALSE(repeatedProperties.insertFinalNewline.has_value());
    EXPECT_TRUE(resolveEditorConfig(root + QStringLiteral("/Upper.c"), repeated).insertFinalNewline.value());

    // A key and a value are accepted at the lengths the specification requires cores to carry.
    const QString longSection = QString(1000, QLatin1Char('a')) + QStringLiteral(".c");
    const QVector<EditorConfigFile> limits{{root, QStringLiteral("root = true\n[%1]\nindent_size = 7\n").arg(longSection)}};
    EXPECT_EQ(resolveEditorConfig(root + QLatin1Char('/') + longSection, limits).indentSize.value(), 7);

    // A class offering a separator is not a class at all, so the pattern is the literal text it spells and still matches at any level.
    EXPECT_TRUE(editorConfigSectionMatches(QStringLiteral("[a/b].c"), root, root + QStringLiteral("/nested/[a/b].c")));
    EXPECT_FALSE(editorConfigSectionMatches(QStringLiteral("[a/b].c"), root, root + QStringLiteral("/nested/a.c")));
}

TEST(CodeDocumentTest, AppliesEditorConfigIndentationAndSaveNormalization) {
    filesystem::FileSystemService service;
    test::TestPluginHost host;
    host.useFileSystem(service);
    QTemporaryDir root;
    ASSERT_TRUE(root.isValid());
    const QString configuration = QDir(root.path()).filePath(QStringLiteral(".editorconfig"));
    ASSERT_TRUE(CodeEditorTestsHelper::writeTestFile(configuration, QByteArrayLiteral("root = true\n[*.cpp]\nindent_style = space\nindent_size = 2\ntrim_trailing_whitespace = true\ninsert_final_newline = true\nend_of_line = crlf\n")));
    const QString path = QDir(root.path()).filePath(QStringLiteral("main.cpp"));
    ASSERT_TRUE(CodeEditorTestsHelper::writeTestFile(path, QByteArrayLiteral("int value = 1;")));

    CodeDocument document(path, root.path(), false, CodeEditorFont{}, TextCharset::Latin1, host);
    QSignalSpy configured(&document, &CodeDocument::editorConfigChanged);
    QSignalSpy loaded(&document, &CodeDocument::loaded);
    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { return configured.count() >= 1 && loaded.count() >= 1; }));
    // clang-format on
    EXPECT_EQ(document.editorConfig().indentSize.value(), 2);
    EXPECT_EQ(document.editor().indentWidth(), 2);
    EXPECT_EQ(document.editor().indentStyle(), IndentStyle::Space);
    document.editor().appendPlainText(QStringLiteral("return;   "));
    document.save();
    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { return !document.dirty(); }));
    // clang-format on
    EXPECT_EQ(test::awaitFuture(service.readFile(path, 1024)).value(), QByteArrayLiteral("int value = 1;\r\nreturn;\r\n"));
}

TEST(CodeDocumentTest, KeepsEveryByteOfTheFileItOpenedWhenNothingDeclaresOtherwise) {
    filesystem::FileSystemService service;
    test::TestPluginHost host;
    host.useFileSystem(service);
    QTemporaryDir root;
    ASSERT_TRUE(root.isValid());

    struct Sample final {
        QString name;
        QByteArray content;
    };

    // Every shape a real file can arrive in, saved back byte for byte because no rule declares a change.
    const QVector<Sample> samples{
        {QStringLiteral("unix.txt"), QByteArrayLiteral("first\nsecond\n")}, {QStringLiteral("unix-without-final-newline.txt"), QByteArrayLiteral("first\nsecond")}, {QStringLiteral("windows.txt"), QByteArrayLiteral("first\r\nsecond\r\n")}, {QStringLiteral("windows-without-final-newline.txt"), QByteArrayLiteral("first\r\nsecond")}, {QStringLiteral("classic-mac.txt"), QByteArrayLiteral("first\rsecond\r")}, {QStringLiteral("bom-unix.txt"), QByteArrayLiteral("\xEF\xBB\xBF") + QByteArrayLiteral("first\nsecond\n")}, {QStringLiteral("bom-windows.txt"), QByteArrayLiteral("\xEF\xBB\xBF") + QByteArrayLiteral("first\r\nsecond\r\n")}, {QStringLiteral("trailing-spaces.txt"), QByteArrayLiteral("first   \nsecond\t\n")}, {QStringLiteral("accents.txt"), QString::fromUtf8("ação\nmüller\n日本語\n").toUtf8()}, {QStringLiteral("emoji.txt"), QString::fromUtf8("hello 👋🏽 world\n").toUtf8()}, {QStringLiteral("empty.txt"), QByteArray{}}, {QStringLiteral("single-line.txt"), QByteArrayLiteral("no newline at all")},
    };

    for (const auto& sample : samples) {
        const QString path = QDir(root.path()).filePath(sample.name);
        ASSERT_TRUE(CodeEditorTestsHelper::writeTestFile(path, sample.content)) << qPrintable(sample.name);

        CodeDocument document(path, root.path(), false, CodeEditorFont{}, TextCharset::Latin1, host);
        QSignalSpy loaded(&document, &CodeDocument::loaded);
        // clang-format off
        ASSERT_TRUE(test::waitUntil([&]() { return loaded.count() >= 1; })) << qPrintable(sample.name);
        // clang-format on

        // A document that was only opened is clean, and saving it must still produce the very same bytes.
        EXPECT_FALSE(document.dirty()) << qPrintable(sample.name);
        document.editor().insertPlainText(QStringLiteral("x"));
        // clang-format off
        ASSERT_TRUE(test::waitUntil([&]() { return document.dirty(); })) << qPrintable(sample.name);
        // clang-format on
        document.editor().undo();
        document.save();
        // clang-format off
        ASSERT_TRUE(test::waitUntil([&]() { return !document.dirty(); })) << qPrintable(sample.name);
        // clang-format on
        EXPECT_EQ(test::awaitFuture(service.readFile(path, 4096)).value(), sample.content) << qPrintable(sample.name);
    }
}

TEST(CodeDocumentTest, ReportsAndKeepsTheLineEndingAndTheByteOrderMarkTheFileCarries) {
    filesystem::FileSystemService service;
    test::TestPluginHost host;
    host.useFileSystem(service);
    QTemporaryDir root;
    ASSERT_TRUE(root.isValid());

    const QString windows = QDir(root.path()).filePath(QStringLiteral("windows.txt"));
    ASSERT_TRUE(CodeEditorTestsHelper::writeTestFile(windows, QByteArrayLiteral("\xEF\xBB\xBF") + QByteArrayLiteral("alpha\r\nbeta\r\n")));
    CodeDocument document(windows, root.path(), false, CodeEditorFont{}, TextCharset::Latin1, host);
    QSignalSpy loaded(&document, &CodeDocument::loaded);
    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { return loaded.count() >= 1; }));
    // clang-format on
    EXPECT_EQ(document.lineEnding(), LineEnding::Crlf);
    EXPECT_EQ(document.charset(), TextCharset::Utf8Bom);

    // An edited line joins the file with the ending the file already uses, and the mark survives.
    document.editor().appendPlainText(QStringLiteral("gamma"));
    document.save();
    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { return !document.dirty(); }));
    // clang-format on
    EXPECT_EQ(test::awaitFuture(service.readFile(windows, 4096)).value(), QByteArrayLiteral("\xEF\xBB\xBF") + QByteArrayLiteral("alpha\r\nbeta\r\n\r\ngamma"));

    const QString unix = QDir(root.path()).filePath(QStringLiteral("unix.txt"));
    ASSERT_TRUE(CodeEditorTestsHelper::writeTestFile(unix, QByteArrayLiteral("alpha\nbeta\n")));
    CodeDocument plain(unix, root.path(), false, CodeEditorFont{}, TextCharset::Latin1, host);
    QSignalSpy plainLoaded(&plain, &CodeDocument::loaded);
    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { return plainLoaded.count() >= 1; }));
    // clang-format on
    EXPECT_EQ(plain.lineEnding(), LineEnding::Lf);
    EXPECT_EQ(plain.charset(), TextCharset::Utf8);
}

TEST(CodeDocumentTest, AnnouncesItsFirstLoadOnlyAfterTheCallerCouldConnect) {
    test::TestPluginHost host;
    QTemporaryDir root;
    ASSERT_TRUE(root.isValid());
    const QString path = QDir(root.path()).filePath(QStringLiteral("main.cpp"));
    // clang-format off
    host.readFileHandler = [](const QString&, qint64) { return QtFuture::makeReadyValueFuture(utils::Result<QByteArray>::success(QByteArrayLiteral("alpha\n"))); };
    // clang-format on

    // A read that is already finished runs its continuation immediately, so a document that loaded inside its own constructor would announce it to nobody.
    CodeDocument document(path, root.path(), false, CodeEditorFont{}, TextCharset::Latin1, host);
    QSignalSpy loaded(&document, &CodeDocument::loaded);
    QSignalSpy configured(&document, &CodeDocument::editorConfigChanged);
    EXPECT_TRUE(document.editor().toPlainText().isEmpty());
    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { return loaded.count() == 1; }));
    // clang-format on
    EXPECT_EQ(document.editor().toPlainText(), QStringLiteral("alpha\n"));
    EXPECT_GE(configured.count(), 1);
}

TEST(CodeDocumentTest, OpensEveryEncodingItCanWriteBackAndNamesTheOnesItCannot) {
    filesystem::FileSystemService service;
    test::TestPluginHost host;
    host.useFileSystem(service);
    QTemporaryDir root;
    ASSERT_TRUE(root.isValid());

    struct Sample final {
        QString name;
        QByteArray content;
        TextCharset charset;
    };

    const QByteArray utf16LeText = QByteArrayLiteral("\xFF\xFE") + QByteArray("a\0l\0p\0h\0a\0\r\0\n\0", 14);
    const QByteArray utf16BeText = QByteArrayLiteral("\xFE\xFF") + QByteArray("\0a\0l\0p\0h\0a\0\n", 12);
    const QVector<Sample> samples{{QStringLiteral("plain.txt"), QByteArrayLiteral("alpha\n"), TextCharset::Utf8}, {QStringLiteral("mark.txt"), QByteArrayLiteral("\xEF\xBB\xBF") + QByteArrayLiteral("alpha\n"), TextCharset::Utf8Bom}, {QStringLiteral("little.txt"), utf16LeText, TextCharset::Utf16Le}, {QStringLiteral("big.txt"), utf16BeText, TextCharset::Utf16Be}};

    for (const auto& sample : samples) {
        const QString path = QDir(root.path()).filePath(sample.name);
        ASSERT_TRUE(CodeEditorTestsHelper::writeTestFile(path, sample.content)) << qPrintable(sample.name);

        CodeDocument document(path, root.path(), false, CodeEditorFont{}, TextCharset::Latin1, host);
        QSignalSpy loaded(&document, &CodeDocument::loaded);
        QSignalSpy failures(&document, &CodeDocument::operationFailed);
        // clang-format off
        ASSERT_TRUE(test::waitUntil([&]() { return loaded.count() >= 1; })) << qPrintable(sample.name);
        // clang-format on
        EXPECT_EQ(failures.count(), 0) << qPrintable(sample.name);
        EXPECT_EQ(document.charset(), sample.charset) << qPrintable(sample.name);
        EXPECT_EQ(document.editor().toPlainText(), QStringLiteral("alpha\n")) << qPrintable(sample.name);

        // The mark, the encoding and the line ending all survive a save that changes nothing else.
        document.editor().insertPlainText(QStringLiteral("x"));
        document.editor().undo();
        document.save();
        // clang-format off
        ASSERT_TRUE(test::waitUntil([&]() { return !document.dirty(); })) << qPrintable(sample.name);
        // clang-format on
        EXPECT_EQ(test::awaitFuture(service.readFile(path, 4096)).value(), sample.content) << qPrintable(sample.name);
    }

    // An encoding the editor cannot write back is named instead of being opened as text or rejected as binary.
    const QString utf32 = QDir(root.path()).filePath(QStringLiteral("wide.txt"));
    ASSERT_TRUE(CodeEditorTestsHelper::writeTestFile(utf32, QByteArray("\xFF\xFE\0\0", 4) + QByteArray("a\0\0\0", 4)));
    CodeDocument wide(utf32, root.path(), false, CodeEditorFont{}, TextCharset::Latin1, host);
    QSignalSpy wideFailures(&wide, &CodeDocument::operationFailed);
    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { return wideFailures.count() >= 1; }));
    // clang-format on
    EXPECT_TRUE(wideFailures.first().first().toString().contains(QStringLiteral("UTF-32")));

    // A file carrying a null byte and no mark is binary and stays refused.
    const QString binary = QDir(root.path()).filePath(QStringLiteral("binary.bin"));
    ASSERT_TRUE(CodeEditorTestsHelper::writeTestFile(binary, QByteArray("\x7F"
                                                                        "ELF\0\1",
                                                                        6)));
    CodeDocument executable(binary, root.path(), false, CodeEditorFont{}, TextCharset::Latin1, host);
    QSignalSpy binaryFailures(&executable, &CodeDocument::operationFailed);
    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { return binaryFailures.count() >= 1; }));
    // clang-format on
    EXPECT_FALSE(binaryFailures.first().first().toString().contains(QStringLiteral("UTF")));
}

TEST(CodeDocumentTest, SavingKeepsTheCursorAndTheScrollWhereTheReaderLeftThem) {
    filesystem::FileSystemService service;
    test::TestPluginHost host;
    host.useFileSystem(service);
    QTemporaryDir root;
    ASSERT_TRUE(root.isValid());

    QByteArray content;

    for (int line = 0; line < 400; ++line) {
        content.append(QStringLiteral("line %1\n").arg(line).toUtf8());
    }

    const QString path = QDir(root.path()).filePath(QStringLiteral("long.txt"));
    ASSERT_TRUE(CodeEditorTestsHelper::writeTestFile(path, content));

    CodeDocument document(path, root.path(), false, CodeEditorFont{}, TextCharset::Latin1, host);
    QSignalSpy loaded(&document, &CodeDocument::loaded);
    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { return loaded.count() >= 1; }));
    // clang-format on
    document.editor().resize(600, 300);
    document.editor().show();
    document.setCursorLocation(320, 2);
    document.editor().insertPlainText(QStringLiteral("!"));
    const int cursorBeforeSave = document.editor().textCursor().position();
    const int scrollBeforeSave = document.editor().verticalScrollBar()->value();

    document.save();
    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { return !document.dirty(); }));
    // clang-format on

    // The file watcher answers our own write, and that must never move the reader.
    // clang-format off
    ASSERT_FALSE(test::waitUntil([&]() { return document.editor().textCursor().position() != cursorBeforeSave || document.editor().verticalScrollBar()->value() != scrollBeforeSave; }, 1200));
    // clang-format on
    EXPECT_EQ(document.editor().textCursor().position(), cursorBeforeSave);
    EXPECT_EQ(document.editor().verticalScrollBar()->value(), scrollBeforeSave);
}

TEST(CodeDocumentTest, ReportsAConflictOnlyWhenTheBytesOnDiskReallyDiffer) {
    filesystem::FileSystemService service;
    test::TestPluginHost host;
    host.useFileSystem(service);
    QTemporaryDir root;
    ASSERT_TRUE(root.isValid());
    const QString path = QDir(root.path()).filePath(QStringLiteral("notes.txt"));
    ASSERT_TRUE(CodeEditorTestsHelper::writeTestFile(path, QByteArrayLiteral("alpha\n")));

    CodeDocument document(path, root.path(), false, CodeEditorFont{}, TextCharset::Latin1, host);
    QSignalSpy loaded(&document, &CodeDocument::loaded);
    QSignalSpy conflicts(&document, &CodeDocument::externalChangeConflict);
    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { return loaded.count() >= 1; }));
    // clang-format on

    // The same bytes written again are not a change, even while the buffer is dirty.
    document.editor().appendPlainText(QStringLiteral("beta"));
    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { return document.dirty(); }));
    // clang-format on
    ASSERT_TRUE(CodeEditorTestsHelper::writeTestFile(path, QByteArrayLiteral("alpha\n")));
    // clang-format off
    ASSERT_FALSE(test::waitUntil([&]() { return conflicts.count() > 0; }, 1200));
    // clang-format on
    EXPECT_TRUE(document.dirty());
    EXPECT_EQ(document.editor().toPlainText(), QStringLiteral("alpha\n\nbeta"));

    // A real external edit while the buffer is dirty is reported and never overwrites what the user typed.
    // The platform can leave a watched file unreported, so this change is driven through the same path that coming back to the application takes.
    ASSERT_TRUE(CodeEditorTestsHelper::writeTestFile(path, QByteArrayLiteral("changed elsewhere\n")));
    document.recheckExternalChange();
    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { return conflicts.count() == 1; }));
    // clang-format on
    EXPECT_EQ(document.editor().toPlainText(), QStringLiteral("alpha\n\nbeta"));
}

TEST(LanguageRegistryTest, LoadsEveryLanguageAndServerTheCatalogDeclares) {
    ASSERT_TRUE(LanguageRegistry::catalogError().hasValue()) << LanguageRegistry::catalogError().error().message.toStdString() << " " << LanguageRegistry::catalogError().error().detail.toStdString();

    QFile file(QStringLiteral(":/slotdeck/code-editor/assets/languages.json"));
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    const QJsonObject catalog = QJsonDocument::fromJson(file.readAll()).object();
    const QJsonArray declaredLanguages = catalog.value(QStringLiteral("languages")).toArray();
    const QJsonArray declaredServers = catalog.value(QStringLiteral("servers")).toArray();
    ASSERT_EQ(LanguageRegistry::languages().size(), declaredLanguages.size());
    ASSERT_EQ(LanguageRegistry::languageServers().size(), declaredServers.size());

    // Every declared language answers for the extensions it claims, and no extension is claimed twice.
    QStringList claimed;

    for (const auto& value : declaredLanguages) {
        const QJsonObject entry = value.toObject();
        const QString id = entry.value(QStringLiteral("id")).toString();
        const LanguageDefinition* language = LanguageRegistry::languageForId(id);
        ASSERT_NE(language, nullptr) << id.toStdString();
        EXPECT_FALSE(language->name.isEmpty()) << id.toStdString();
        for (const auto& extension : entry.value(QStringLiteral("extensions")).toArray()) {
            const QString suffix = extension.toString();
            EXPECT_FALSE(claimed.contains(suffix, Qt::CaseInsensitive)) << suffix.toStdString();
            claimed.append(suffix);
            EXPECT_EQ(LanguageRegistry::languageForPath(QStringLiteral("sample.") + suffix).id, id) << suffix.toStdString();
        }
        for (const auto& name : entry.value(QStringLiteral("fileNames")).toArray()) {
            EXPECT_EQ(LanguageRegistry::languageForPath(QDir(QStringLiteral("/tmp")).filePath(name.toString())).id, id) << name.toString().toStdString();
        }
    }

    // The languages the editor already answered for are still there, and the common ones joined them.
    for (const auto& id : QStringList({QStringLiteral("cpp"), QStringLiteral("python"), QStringLiteral("typescript"), QStringLiteral("go"), QStringLiteral("rust"), QStringLiteral("php"), QStringLiteral("ruby"), QStringLiteral("csharp"), QStringLiteral("kotlin"), QStringLiteral("swift"), QStringLiteral("lua"), QStringLiteral("sql"), QStringLiteral("toml"), QStringLiteral("xml"), QStringLiteral("dockerfile"), QStringLiteral("terraform")})) {
        EXPECT_NE(LanguageRegistry::languageForId(id), nullptr) << id.toStdString();
    }

    // A C file is announced as C even though the C and C++ definition is one, and an unknown suffix falls to plain text.
    EXPECT_EQ(LanguageRegistry::protocolLanguageId(QStringLiteral("main.c")), QStringLiteral("c"));
    EXPECT_EQ(LanguageRegistry::protocolLanguageId(QStringLiteral("main.cpp")), QStringLiteral("cpp"));
    EXPECT_EQ(LanguageRegistry::languageForPath(QStringLiteral("notes.unknown")).id, QStringLiteral("plaintext"));

    // The tunable limits come from the catalog as well, each one inside the bounds that keep it sane.
    const EditorLimits& limits = LanguageRegistry::limits();
    EXPECT_GT(limits.maximumFileBytes, 0);
    EXPECT_GT(limits.maximumHighlightedLineLength, 0);
    EXPECT_GT(limits.maximumSemanticTokenLines, 0);
    EXPECT_GT(limits.maximumSearchMatches, 0);
    EXPECT_GT(limits.partialRepaintDivisor, 0);
    EXPECT_GT(limits.changeDebounceMs, 0);
    EXPECT_GT(limits.analysisDebounceMs, 0);
    EXPECT_GT(limits.highlightDebounceMs, 0);
    EXPECT_GT(limits.externalChangeDebounceMs, 0);
    EXPECT_GE(limits.maximumRestarts, 0);
    EXPECT_GT(limits.restartWindowMs, 0);
    EXPECT_GT(limits.initializeTimeoutMs, 0);
    EXPECT_GT(limits.maximumReferences, 0);
    EXPECT_GT(limits.maximumProblems, 0);
    EXPECT_GE(limits.bottomPanelInitialHeight, limits.bottomPanelMinimumHeight);

    // The highlighting the catalog declares is loaded, and a language that needs its own patterns declares them there rather than in code.
    EXPECT_FALSE(LanguageRegistry::commonPatterns().isEmpty());
    EXPECT_FALSE(LanguageRegistry::semanticRoles().isEmpty());

    for (const auto& pattern : LanguageRegistry::commonPatterns()) {
        EXPECT_TRUE(QRegularExpression(pattern.pattern).isValid()) << pattern.pattern.toStdString();
    }

    const LanguageDefinition* markdown = LanguageRegistry::languageForId(QStringLiteral("markdown"));
    ASSERT_NE(markdown, nullptr);
    EXPECT_FALSE(markdown->patterns.isEmpty());
    EXPECT_TRUE(LanguageRegistry::semanticRoles().contains(QStringLiteral("function")));

    // Every server names a language the catalog declares and a program to start.
    for (const auto& server : LanguageRegistry::languageServers()) {
        EXPECT_NE(LanguageRegistry::languageForId(server.languageId), nullptr) << server.languageId.toStdString();
        ASSERT_FALSE(server.candidates.isEmpty()) << server.languageId.toStdString();
        for (const auto& candidate : server.candidates) {
            EXPECT_FALSE(candidate.executableName.isEmpty()) << server.languageId.toStdString();
        }
    }
}

TEST(CodeWorkspaceViewTest, AnalysesADocumentAgainWhenTheServerFinishesStartingLate) {
    test::TestPluginHost host;
    filesystem::FileSystemService service;
    host.useFileSystem(service);
    QTemporaryDir root;
    ASSERT_TRUE(root.isValid());
    const QString canonicalRoot = QFileInfo(root.path()).canonicalFilePath();
    const QString path = QDir(canonicalRoot).filePath(QStringLiteral("main.cpp"));
    ASSERT_TRUE(CodeEditorTestsHelper::writeTestFile(path, QByteArrayLiteral("int main() { return 0; }\n")));
    const QDateTime now = QDateTime::currentDateTimeUtc();

    // The server answers initialization long after the analysis debounce, so everything the document asked for first was never sent.
    const ResolvedLanguageServer late{QStringLiteral("cpp"), QCoreApplication::applicationFilePath(), {QStringLiteral("--slotdeck-test-lsp-slow-start")}};
    CodeWorkspaceView view({QStringLiteral("workspace"), canonicalRoot, 0, true, now, now, {}}, {late}, false, CodeEditorFont{}, TextCharset::Latin1, host);
    auto* symbols = view.findChild<QTreeWidget*>(QStringLiteral("codeEditorSymbols"));
    ASSERT_NE(symbols, nullptr);

    view.openFile(path);
    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { return symbols->topLevelItemCount() == 1; }));
    // clang-format on
    EXPECT_EQ(symbols->topLevelItem(0)->text(0), QStringLiteral("main  int ()"));
}

TEST(CodeEditorPluginTest, OpensAFolderOnceAndRevealsItselfWhenAnotherPluginAsks) {
    test::TestPluginHost host;
    filesystem::FileSystemService service;
    host.useFileSystem(service);
    CodeEditorTestsHelper::installSettingsDocument(host, false);
    QTemporaryDir root;
    ASSERT_TRUE(root.isValid());
    CodeEditorPlugin plugin;
    ASSERT_TRUE(plugin.initialize(host).hasValue());

    QVector<utils::Result<QJsonObject>> answers;
    // clang-format off
    const auto collect = [&answers](utils::Result<QJsonObject> answer) { answers.append(std::move(answer)); };
    // clang-format on
    plugin.handleRequest(QStringLiteral("ai"), QStringLiteral("code-editor.workspace.open"), {{QStringLiteral("path"), root.path()}}, collect);
    ASSERT_EQ(answers.size(), 1);
    ASSERT_TRUE(answers.at(0).hasValue());

    // The same folder is never opened twice, so asking again answers with the workspace that already has it.
    plugin.handleRequest(QStringLiteral("ai"), QStringLiteral("code-editor.workspace.open"), {{QStringLiteral("path"), root.path()}}, collect);
    ASSERT_EQ(answers.size(), 2);
    ASSERT_TRUE(answers.at(1).hasValue());
    EXPECT_EQ(answers.at(1).value().value(QStringLiteral("workspaceId")).toString(), answers.at(0).value().value(QStringLiteral("workspaceId")).toString());

    // The editor reveals itself both times, because the folder the caller asked for is the one the user must now see.
    EXPECT_EQ(host.revealedNavigation, QStringList({QStringLiteral("editor"), QStringLiteral("editor")}));

    // A path that names no directory is refused instead of opening an empty workspace.
    plugin.handleRequest(QStringLiteral("ai"), QStringLiteral("code-editor.workspace.open"), {{QStringLiteral("path"), QDir(root.path()).filePath(QStringLiteral("absent"))}}, collect);
    ASSERT_EQ(answers.size(), 3);
    EXPECT_FALSE(answers.at(2).hasValue());
    EXPECT_EQ(host.revealedNavigation.size(), 2);
    // The reason travels in the language of the shell, because the caller shows it and only this plugin knows what failed.
    EXPECT_EQ(answers.at(2).error().message, host.translate(QStringLiteral("code-editor.error.workspace-open")));

    plugin.shutdown();
}

TEST(CodeEditorPluginTest, NamesEveryLanguageTheServerSectionLists) {
    test::TestPluginHost host;
    CodeEditorTestsHelper::installSettingsDocument(host, false);
    CodeEditorPlugin plugin;
    ASSERT_TRUE(plugin.initialize(host).hasValue());

    std::unique_ptr<QWidget> section(plugin.createSettingsSection(QStringLiteral("editor"), QStringLiteral("language-servers"), nullptr));
    ASSERT_NE(section, nullptr);
    auto* table = section->findChild<QTableWidget*>();
    ASSERT_NE(table, nullptr);
    ASSERT_EQ(table->rowCount(), LanguageRegistry::languageServers().size());

    // Every row reads as the name of its language, so a language the section lists is never presented as a translation key.
    for (int row = 0; row < table->rowCount(); ++row) {
        const QString name = table->item(row, 0)->text();
        EXPECT_FALSE(name.isEmpty());
        EXPECT_FALSE(name.startsWith(QStringLiteral("code-editor."))) << name.toStdString();
        EXPECT_EQ(name, LanguageRegistry::languageForId(LanguageRegistry::languageServers().at(row).languageId)->name);
    }

    plugin.shutdown();
}

TEST(CodeWorkspaceViewTest, GivesTheBottomPanelEveryPixelItIsDraggedTo) {
    test::TestPluginHost host;
    filesystem::FileSystemService service;
    host.useFileSystem(service);
    QTemporaryDir root;
    ASSERT_TRUE(root.isValid());
    const QString canonicalRoot = QFileInfo(root.path()).canonicalFilePath();
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const ResolvedLanguageServer fixture{QStringLiteral("cpp"), QCoreApplication::applicationFilePath(), {QStringLiteral("--slotdeck-test-lsp")}};
    CodeWorkspaceView view({QStringLiteral("workspace"), canonicalRoot, 0, true, now, now, {}}, {fixture}, false, CodeEditorFont{}, TextCharset::Latin1, host);
    view.resize(1400, 900);
    view.show();

    auto* panel = view.findChild<QWidget*>(QStringLiteral("codeEditorBottomPanel"));
    auto* problems = view.findChild<QTreeWidget*>(QStringLiteral("codeEditorProblems"));
    auto* references = view.findChild<QTreeWidget*>(QStringLiteral("codeEditorReferences"));
    ASSERT_NE(panel, nullptr);
    ASSERT_NE(problems, nullptr);
    ASSERT_NE(references, nullptr);

    QSplitter* editorArea = nullptr;

    for (auto* candidate : view.findChildren<QSplitter*>(QStringLiteral("codeEditorSplitter"))) {
        if (candidate->orientation() == Qt::Vertical && candidate->indexOf(panel) >= 0) {
            editorArea = candidate;
        }
    }

    ASSERT_NE(editorArea, nullptr);

    // The panel opens tall enough to read and is bounded by nothing above it, so dragging it larger really enlarges it.
    EXPECT_GE(panel->height(), panel->minimumHeight());
    EXPECT_GT(panel->minimumHeight(), 0);
    editorArea->setSizes({300, 500});
    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { return panel->height() == 500; }));
    // clang-format on

    // The page reaches the bottom of the panel, so no row is cut by a band nothing occupies.
    auto* problemsPage = problems->parentWidget();
    ASSERT_NE(problemsPage, nullptr);
    EXPECT_EQ(problems->mapTo(panel, QPoint(0, problems->height())).y(), panel->height());
    auto* tabs = qobject_cast<QTabWidget*>(panel);
    ASSERT_NE(tabs, nullptr);
    tabs->setCurrentWidget(references);
    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { return references->height() == problemsPage->height(); }));
    // clang-format on
    EXPECT_EQ(references->mapTo(panel, QPoint(0, references->height())).y(), panel->height());
}

TEST(CodeWorkspaceViewTest, NarrowsTheTreeToWhatWasTypedAndKeepsThePathThatLeadsToIt) {
    QTemporaryDir root;
    ASSERT_TRUE(root.isValid());
    ASSERT_TRUE(QDir(root.path()).mkpath(QStringLiteral("js")));
    ASSERT_TRUE(QDir(root.path()).mkpath(QStringLiteral("css")));
    ASSERT_TRUE(CodeEditorTestsHelper::writeTestFile(QDir(root.path()).filePath(QStringLiteral("index.html")), QByteArrayLiteral("<html></html>")));
    ASSERT_TRUE(CodeEditorTestsHelper::writeTestFile(QDir(root.path()).filePath(QStringLiteral("js/main.js")), QByteArrayLiteral("const answer = 42;")));
    ASSERT_TRUE(CodeEditorTestsHelper::writeTestFile(QDir(root.path()).filePath(QStringLiteral("css/site.css")), QByteArrayLiteral("body {}")));

    test::TestPluginHost host;
    host.translations = translations::catalog().value(QStringLiteral("en"));
    CodeWorkspaceState state;
    state.id = QStringLiteral("workspace-1");
    state.rootPath = root.path();
    CodeWorkspaceView view(state, {}, false, {}, TextCharset::Utf8, host, nullptr);
    view.resize(900, 600);
    view.show();

    auto* tree = view.findChild<QTreeView*>(QStringLiteral("codeEditorTree"));
    auto* filter = view.findChild<ui::FilterField*>(QStringLiteral("codeEditorFileFilter"));
    ASSERT_NE(tree, nullptr);
    ASSERT_NE(filter, nullptr);
    auto* model = qobject_cast<QFileSystemModel*>(tree->model());
    ASSERT_NE(model, nullptr);
    // clang-format off
    ASSERT_TRUE(test::waitUntil([tree, model]() { return model->rowCount(tree->rootIndex()) == 3; }));
    // clang-format on

    // The filter is a caption over a field, so the reader knows what typing into it does.
    auto* caption = filter->findChild<QLabel*>(QStringLiteral("filterCaption"));
    ASSERT_NE(caption, nullptr);
    EXPECT_EQ(caption->text(), host.translate(QStringLiteral("code-editor.tree.filter")));

    auto* editor = filter->findChild<QLineEdit*>(QStringLiteral("filterField"));
    ASSERT_NE(editor, nullptr);
    editor->setText(QStringLiteral("main"));

    const QModelIndex treeRoot = tree->rootIndex();
    // clang-format off
    const auto rowNamed = [model, treeRoot](const QString& name) { for (int row = 0; row < model->rowCount(treeRoot); ++row) { if (model->index(row, 0, treeRoot).data(Qt::DisplayRole).toString() == name) { return row; } } return -1; };
    ASSERT_TRUE(test::waitUntil([&]() { return rowNamed(QStringLiteral("js")) >= 0 && !tree->isRowHidden(rowNamed(QStringLiteral("js")), treeRoot); }));
    // clang-format on

    // What the filter did not match is hidden, and the folder that leads to what it matched stays.
    EXPECT_TRUE(tree->isRowHidden(rowNamed(QStringLiteral("css")), treeRoot));
    EXPECT_TRUE(tree->isRowHidden(rowNamed(QStringLiteral("index.html")), treeRoot));
    EXPECT_FALSE(tree->isRowHidden(rowNamed(QStringLiteral("js")), treeRoot));

    const QModelIndex folder = model->index(rowNamed(QStringLiteral("js")), 0, treeRoot);
    ASSERT_EQ(model->rowCount(folder), 1);
    EXPECT_FALSE(tree->isRowHidden(0, folder));

    // The filter area is separated from the tree by one divider, and its caption is not written against the field.
    auto* panel = filter->parentWidget()->parentWidget();
    ASSERT_NE(panel, nullptr);
    auto* divider = panel->findChild<QWidget*>(QStringLiteral("sharedDivider"));
    ASSERT_NE(divider, nullptr);
    EXPECT_GT(divider->mapTo(panel, QPoint(0, 0)).y(), filter->mapTo(panel, QPoint(0, 0)).y() + filter->height() - 1);
    EXPECT_LT(divider->mapTo(panel, QPoint(0, 0)).y(), tree->mapTo(panel, QPoint(0, 0)).y());
    EXPECT_GE(editor->mapTo(filter, QPoint(0, 0)).y() - (caption->mapTo(filter, QPoint(0, 0)).y() + caption->height()), 4);

    // Clearing it gives the tree back.
    editor->clear();
    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { return !tree->isRowHidden(rowNamed(QStringLiteral("css")), treeRoot); }));
    // clang-format on
    EXPECT_FALSE(tree->isRowHidden(rowNamed(QStringLiteral("index.html")), treeRoot));
}

TEST(CodeWorkspaceViewTest, RenamesTheFileOnDiskAndFollowsItWithTheOpenDocument) {
    filesystem::FileSystemService service;
    test::TestPluginHost host;
    host.useFileSystem(service);
    QTemporaryDir root;
    ASSERT_TRUE(root.isValid());
    const QString canonicalRoot = QFileInfo(root.path()).canonicalFilePath();
    const QString path = QDir(canonicalRoot).filePath(QStringLiteral("AppDelegatex.h"));
    ASSERT_TRUE(CodeEditorTestsHelper::writeTestFile(path, QByteArrayLiteral("#pragma once\n")));
    const QDateTime now = QDateTime::currentDateTimeUtc();

    CodeWorkspaceView view({QStringLiteral("workspace"), canonicalRoot, 0, true, now, now, {}}, {}, false, CodeEditorFont{}, TextCharset::Latin1, host);
    view.resize(1000, 700);
    view.show();
    view.openFile(path);
    auto* documents = view.findChild<QTabWidget*>(QStringLiteral("codeEditorDocuments"));
    ASSERT_NE(documents, nullptr);
    ASSERT_EQ(documents->count(), 1);
    auto* openDocument = qobject_cast<CodeDocument*>(documents->widget(0));
    QSignalSpy loaded(openDocument, &CodeDocument::loaded);
    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { return loaded.count() >= 1; }));
    // clang-format on

    const QString renamed = QDir(canonicalRoot).filePath(QStringLiteral("AppDelegate.h"));
    view.renameEntry(path, QStringLiteral("AppDelegate.h"));

    // The tab may only say what the disk says, so both change or neither does.
    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { return openDocument->path() == renamed; }));
    // clang-format on
    EXPECT_TRUE(QFileInfo::exists(renamed));
    EXPECT_FALSE(QFileInfo::exists(path));
    EXPECT_TRUE(documents->tabText(0).startsWith(QStringLiteral("AppDelegate.h")));

    // A name that would leave the workspace is refused and nothing moves.
    QSignalSpy failures(&view, &CodeWorkspaceView::operationFailed);
    view.renameEntry(renamed, QStringLiteral("../escaped.h"));
    EXPECT_EQ(failures.count(), 1);
    EXPECT_TRUE(QFileInfo::exists(renamed));
    EXPECT_EQ(openDocument->path(), renamed);

    // Double clicking the tab points the tree at the file that tab belongs to.
    auto* tree = view.findChild<QTreeView*>(QStringLiteral("codeEditorTree"));
    ASSERT_NE(tree, nullptr);
    emit documents->tabBar()->tabBarDoubleClicked(0);
    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { auto* model = qobject_cast<QFileSystemModel*>(tree->model()); return model != nullptr && tree->currentIndex().isValid() && model->filePath(tree->currentIndex()) == renamed; }));
    // clang-format on
}

TEST(CodeWorkspaceViewTest, RenamingAnOpenFileFollowsItWithoutReportingAConflict) {
    filesystem::FileSystemService service;
    test::TestPluginHost host;
    host.useFileSystem(service);
    QTemporaryDir root;
    ASSERT_TRUE(root.isValid());
    const QString path = QDir(root.path()).filePath(QStringLiteral("before.txt"));
    ASSERT_TRUE(CodeEditorTestsHelper::writeTestFile(path, QByteArrayLiteral("content\n")));

    CodeDocument document(path, root.path(), false, CodeEditorFont{}, TextCharset::Latin1, host);
    QSignalSpy loaded(&document, &CodeDocument::loaded);
    QSignalSpy conflicts(&document, &CodeDocument::externalChangeConflict);
    QSignalSpy removals(&document, &CodeDocument::externalFileRemoved);
    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { return loaded.count() >= 1; }));
    // clang-format on
    document.editor().appendPlainText(QStringLiteral("typed"));
    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { return document.dirty(); }));
    // clang-format on

    const QString renamed = QDir(root.path()).filePath(QStringLiteral("after.txt"));
    ASSERT_TRUE(test::awaitFuture(service.movePath(path, renamed)).hasValue());
    document.updatePath(renamed);

    // The document follows the rename, keeps the unsaved buffer and says nothing about a conflict.
    // clang-format off
    ASSERT_FALSE(test::waitUntil([&]() { return conflicts.count() > 0 || removals.count() > 0; }, 1200));
    // clang-format on
    EXPECT_EQ(document.path(), renamed);
    EXPECT_TRUE(document.title().startsWith(QStringLiteral("after.txt")));
    EXPECT_TRUE(document.dirty());
    EXPECT_EQ(document.editor().toPlainText(), QStringLiteral("content\n\ntyped"));

    document.save();
    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { return !document.dirty(); }));
    // clang-format on
    EXPECT_EQ(test::awaitFuture(service.readFile(renamed, 1024)).value(), QByteArrayLiteral("content\n\ntyped"));
}

TEST(CodeDocumentTest, FindsMatchesForwardBackwardAndWrapsAround) {
    filesystem::FileSystemService service;
    test::TestPluginHost host;
    host.useFileSystem(service);
    QTemporaryDir root;
    ASSERT_TRUE(root.isValid());
    const QString path = QDir(root.path()).filePath(QStringLiteral("search.txt"));
    ASSERT_TRUE(CodeEditorTestsHelper::writeTestFile(path, QByteArrayLiteral("alpha\nbeta\nalpha\nALPHA alphabet\n")));

    CodeDocument document(path, root.path(), false, CodeEditorFont{}, TextCharset::Latin1, host);
    document.resize(800, 600);
    document.show();
    QSignalSpy loaded(&document, &CodeDocument::loaded);
    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { return loaded.count() >= 1; }));
    // clang-format on
    auto* findBar = document.findChild<ui::FindBar*>(QStringLiteral("findBar"));
    ASSERT_NE(findBar, nullptr);
    EXPECT_FALSE(findBar->isVisible());

    document.editor().setFocus();
    QTest::keyClick(&document.editor(), Qt::Key_F, QKeySequence(QKeySequence::Find)[0].keyboardModifiers());
    // clang-format off
    ASSERT_TRUE(test::waitUntil([findBar]() { return findBar->isVisible(); }));
    // clang-format on

    auto* query = findBar->findChild<QLineEdit*>(QStringLiteral("findQuery"));
    auto* status = findBar->findChild<QLabel*>(QStringLiteral("mutedLabel"));
    auto* caseSensitive = findBar->findChild<QToolButton*>(QStringLiteral("findCase"));
    auto* wholeWord = findBar->findChild<QToolButton*>(QStringLiteral("findWholeWord"));
    ASSERT_NE(query, nullptr);
    ASSERT_NE(status, nullptr);
    ASSERT_NE(caseSensitive, nullptr);
    ASSERT_NE(wholeWord, nullptr);

    // Typing already searches from where the reader was, reports how many matches exist and marks every one of them.
    query->setText(QStringLiteral("alpha"));
    EXPECT_EQ(document.cursorLine(), 1);
    EXPECT_EQ(status->text(), QStringLiteral("1/4"));
    EXPECT_EQ(document.editor().extraSelections().size(), 5);

    QTest::keyClick(query, Qt::Key_Return);
    EXPECT_EQ(document.cursorLine(), 3);
    EXPECT_EQ(status->text(), QStringLiteral("2/4"));
    QTest::keyClick(query, Qt::Key_Return);
    EXPECT_EQ(document.cursorLine(), 4);
    QTest::keyClick(query, Qt::Key_Return);
    EXPECT_EQ(document.cursorLine(), 4);
    QTest::keyClick(query, Qt::Key_Return);
    EXPECT_EQ(document.cursorLine(), 1);
    QTest::keyClick(query, Qt::Key_Return, Qt::ShiftModifier);
    EXPECT_EQ(document.cursorLine(), 4);

    // The case of the query is only respected when the reader asks for it.
    caseSensitive->click();
    EXPECT_EQ(status->text(), QStringLiteral("1/3"));
    query->setText(QStringLiteral("ALPHA"));
    EXPECT_EQ(status->text(), QStringLiteral("1/1"));
    caseSensitive->click();
    EXPECT_EQ(status->text(), QStringLiteral("1/4"));

    // A whole word query stops matching inside a longer word.
    query->setText(QStringLiteral("alpha"));
    wholeWord->click();
    EXPECT_EQ(status->text(), QStringLiteral("1/3"));

    query->setText(QStringLiteral("missing"));
    EXPECT_EQ(status->text(), host.translate(QStringLiteral("code-editor.find.not-found")));
    EXPECT_EQ(document.editor().extraSelections().size(), 1);

    QTest::keyClick(query, Qt::Key_Escape);
    // clang-format off
    ASSERT_TRUE(test::waitUntil([findBar]() { return !findBar->isVisible(); }));
    // clang-format on
    EXPECT_EQ(document.editor().extraSelections().size(), 1);
}

TEST(CodeWorkspaceViewTest, ReportsCursorLocationAndClosesDocumentsWithTheNativeShortcut) {
    filesystem::FileSystemService service;
    test::TestPluginHost host;
    host.useFileSystem(service);
    QTemporaryDir root;
    ASSERT_TRUE(root.isValid());
    const QString path = QDir(root.path()).filePath(QStringLiteral("view.cpp"));
    ASSERT_TRUE(CodeEditorTestsHelper::writeTestFile(path, QByteArrayLiteral("int main() {\n    return 0;\n}\n")));
    const QDateTime now = QDateTime::currentDateTimeUtc();

    CodeWorkspaceView view({QStringLiteral("workspace"), QFileInfo(root.path()).canonicalFilePath(), 0, true, now, now, {}}, {}, false, CodeEditorFont{}, TextCharset::Latin1, host);
    view.resize(1000, 700);
    view.show();
    view.openFile(path);
    auto* documents = view.findChild<QTabWidget*>(QStringLiteral("codeEditorDocuments"));
    ASSERT_NE(documents, nullptr);
    ASSERT_EQ(documents->count(), 1);

    auto* cursorLocation = view.findChild<QLabel*>(QStringLiteral("codeEditorCursorLocation"));
    ASSERT_NE(cursorLocation, nullptr);
    auto* openDocument = qobject_cast<CodeDocument*>(documents->widget(0));
    ASSERT_NE(openDocument, nullptr);
    QSignalSpy loaded(openDocument, &CodeDocument::loaded);
    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { return loaded.count() >= 1; }));
    // clang-format on
    openDocument->setCursorLocation(1, 4);
    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { return cursorLocation->text() == QStringLiteral("code-editor.status.cursor"); }));
    // clang-format on
    EXPECT_EQ(openDocument->cursorLine(), 2);
    EXPECT_EQ(openDocument->cursorColumn(), 5);

    auto* wordWrap = view.findChild<QToolButton*>(QStringLiteral("codeEditorWordWrap"));
    ASSERT_NE(wordWrap, nullptr);
    QSignalSpy wordWrapToggled(&view, &CodeWorkspaceView::wordWrapToggled);
    wordWrap->click();
    EXPECT_TRUE(view.wordWrapEnabled());
    EXPECT_EQ(wordWrapToggled.count(), 1);
    EXPECT_EQ(openDocument->editor().lineWrapMode(), QPlainTextEdit::WidgetWidth);

    auto* closeAction = view.findChild<QAction*>(QStringLiteral("codeEditorCloseDocument"));
    ASSERT_NE(closeAction, nullptr);
    EXPECT_EQ(closeAction->shortcut(), QKeySequence(QKeySequence::Close));
    EXPECT_EQ(closeAction->shortcutContext(), Qt::WidgetWithChildrenShortcut);
    closeAction->trigger();
    EXPECT_EQ(documents->count(), 0);
    EXPECT_TRUE(cursorLocation->text().isEmpty());
}

TEST(CodeEditorRepositoryTest, MigratesLoadsAndPersistsStrictWorkspaceState) {
    test::TestPluginHost host;
    CodeEditorRepository repository(host);
    ASSERT_TRUE(repository.initialize().hasValue());
    ASSERT_EQ(host.appliedMigrations.size(), 1);
    EXPECT_EQ(host.appliedMigrations.first().version, 1);
    EXPECT_TRUE(repository.load().hasValue());
    // An empty document is every declared default.
    EXPECT_FALSE(repository.loadSettings().wordWrap);
    EXPECT_TRUE(repository.loadSettings().languageServersEnabled);
    CodeEditorTestsHelper::installSettingsDocument(host, true);
    EXPECT_TRUE(repository.loadSettings().wordWrap);
    EXPECT_EQ(repository.loadSettings().fontSize, defaultEditorFontSize);
    EXPECT_TRUE(test::awaitFuture(repository.saveSettings({false, true, QString{}, defaultEditorFontSize})).hasValue());

    // A value this editor cannot use is the declared default, and everything around it still loads.
    CodeEditorTestsHelper::installSettingsDocument(host, true, ui::maximumContentFontSize + 1);
    EXPECT_EQ(repository.loadSettings().fontSize, defaultEditorFontSize);
    EXPECT_TRUE(repository.loadSettings().wordWrap);

    host.settingsDocument = {{QStringLiteral("wordWrap"), QStringLiteral("yes")}, {QStringLiteral("fontFamily"), 7}, {QStringLiteral("nobodyDeclaresThis"), true}};
    EXPECT_FALSE(repository.loadSettings().wordWrap);
    EXPECT_TRUE(repository.loadSettings().fontFamily.isEmpty());
    EXPECT_TRUE(repository.loadSettings().languageServersEnabled);
    CodeEditorTestsHelper::installSettingsDocument(host, true, defaultEditorFontSize, false);
    EXPECT_FALSE(repository.loadSettings().languageServersEnabled);
    CodeEditorTestsHelper::installSettingsDocument(host, true);

    QTemporaryDir root;
    ASSERT_TRUE(root.isValid());
    const QDateTime now = QDateTime::currentDateTimeUtc();
    QVector<CodeWorkspaceState> state{{QStringLiteral("workspace-1"), root.path(), 0, true, now, now, {}}};
    EXPECT_TRUE(test::awaitFuture(repository.save(state)).hasValue());
    ASSERT_EQ(host.databaseTransactions.size(), 1);
    EXPECT_EQ(host.databaseTransactions.last().size(), 3);
    state.first().position = 2;
    EXPECT_EQ(test::awaitFuture(repository.save(state)).error().code, QStringLiteral("code_editor_state_invalid"));

    const QStringList invalidTimestamps{QString{}, QStringLiteral("not a timestamp"), QStringLiteral("2026-08-15T12:00:00.000-03:00")};

    for (const auto& invalid : invalidTimestamps) {
        CodeEditorTestsHelper::installWorkspaceRow(host, root.path(), now.toString(Qt::ISODateWithMs), invalid);
        EXPECT_EQ(repository.load().error().code, QStringLiteral("code_editor_state_invalid"));
    }

    CodeEditorTestsHelper::installWorkspaceRow(host, root.path(), now.addSecs(1).toString(Qt::ISODateWithMs), now.toString(Qt::ISODateWithMs));
    EXPECT_EQ(repository.load().error().code, QStringLiteral("code_editor_state_invalid"));
    CodeEditorTestsHelper::installWorkspaceRow(host, root.path(), now.toString(Qt::ISODateWithMs), now.toString(Qt::ISODateWithMs));
    ASSERT_TRUE(repository.load().hasValue());
    EXPECT_EQ(repository.load().value().size(), 1);
}

TEST(CodeEditorPluginTest, PublishesMetadataAndManagesFolderTabs) {
    test::TestPluginHost host;
    CodeEditorPlugin plugin;
    EXPECT_EQ(plugin.id(), QStringLiteral("code-editor"));
    EXPECT_EQ(plugin.dependencies(), QStringList{QStringLiteral("logs")});
    EXPECT_EQ(plugin.databaseSchemaVersion(), 1);
    EXPECT_EQ(plugin.navigationItems(ui::themeManager().theme()).size(), 1);
    EXPECT_EQ(plugin.settingsGroups().first().sections.size(), 3);
    EXPECT_EQ(plugin.settingsGroups().first().sections.first().id, QStringLiteral("appearance"));
    EXPECT_EQ(plugin.settingsGroups().first().sections.at(1).id, QStringLiteral("files"));
    EXPECT_EQ(plugin.settingsGroups().first().sections.last().id, QStringLiteral("language-servers"));
    CodeEditorTestsHelper::installSettingsDocument(host, true);
    ASSERT_TRUE(plugin.initialize(host).hasValue());
    std::unique_ptr<QWidget> files(plugin.createSettingsSection(QStringLiteral("editor"), QStringLiteral("files"), nullptr));
    ASSERT_NE(files, nullptr);
    auto* declaredCharset = files->findChild<QComboBox*>(QStringLiteral("codeEditorDefaultCharset"));
    ASSERT_NE(declaredCharset, nullptr);
    EXPECT_EQ(declaredCharset->count(), textCharsets().size());
    EXPECT_EQ(declaredCharset->currentData().toString(), QStringLiteral("latin1"));
    EXPECT_TRUE(plugin.wordWrap());

    QTemporaryDir first;
    QTemporaryDir second;
    ASSERT_TRUE(first.isValid());
    ASSERT_TRUE(second.isValid());
    const auto firstWorkspace = plugin.openWorkspace(first.path());
    ASSERT_TRUE(firstWorkspace.hasValue());
    EXPECT_EQ(plugin.workspaces().size(), 1);
    EXPECT_TRUE(plugin.openWorkspace(second.path()).hasValue());
    EXPECT_EQ(plugin.workspaces().size(), 2);
    CodeWorkspaceState movedState = plugin.workspaces().last();
    EXPECT_TRUE(plugin.moveWorkspace(1, 0).hasValue());
    EXPECT_TRUE(plugin.updateWorkspace(movedState).hasValue());
    EXPECT_EQ(plugin.workspaces().first().position, 0);
    EXPECT_TRUE(plugin.closeWorkspace(firstWorkspace.value()).hasValue());
    EXPECT_EQ(plugin.openWorkspace(QStringLiteral("relative")).error().code, QStringLiteral("code_editor_workspace_invalid"));
    plugin.shutdown();
}

TEST(CodeEditorPluginTest, DisablingLanguageServersStopsThemAndHidesTheProblemsPanel) {
    test::TestPluginHost host;
    CodeEditorPlugin plugin;
    CodeEditorTestsHelper::installSettingsDocument(host, false);
    ASSERT_TRUE(plugin.initialize(host).hasValue());
    EXPECT_TRUE(plugin.languageServersEnabled());
    QSignalSpy changed(&plugin, &CodeEditorPlugin::languageServersChanged);

    plugin.setLanguageServersEnabled(false);
    EXPECT_FALSE(plugin.languageServersEnabled());
    EXPECT_TRUE(plugin.activeLanguageServers().isEmpty());
    EXPECT_EQ(changed.count(), 1);
    plugin.setLanguageServersEnabled(false);
    EXPECT_EQ(changed.count(), 1);
    plugin.setLanguageServersEnabled(true);
    EXPECT_TRUE(plugin.languageServersEnabled());
    EXPECT_EQ(plugin.activeLanguageServers().size(), plugin.languageServers().size());

    QTemporaryDir root;
    ASSERT_TRUE(root.isValid());
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const ResolvedLanguageServer resolved{QStringLiteral("cpp"), QStringLiteral("/usr/bin/false"), {}};
    CodeWorkspaceView view({QStringLiteral("workspace"), QFileInfo(root.path()).canonicalFilePath(), 0, true, now, now, {}}, {resolved}, false, CodeEditorFont{}, TextCharset::Latin1, host);
    view.show();
    auto* problems = view.findChild<QTreeWidget*>(QStringLiteral("codeEditorProblems"));
    ASSERT_NE(problems, nullptr);
    EXPECT_TRUE(problems->isVisible());
    view.setLanguageServers({});
    EXPECT_FALSE(problems->isVisible());
    view.setLanguageServers({resolved});
    EXPECT_TRUE(problems->isVisible());
    plugin.shutdown();
}

TEST(CodeEditorPluginTest, OwnsItsFontSizeAndAppliesTheBroadcastFontStep) {
    test::TestPluginHost host;
    CodeEditorPlugin plugin;
    CodeEditorTestsHelper::installSettingsDocument(host, false);
    ASSERT_TRUE(plugin.initialize(host).hasValue());
    EXPECT_EQ(plugin.editorFont().size, defaultEditorFontSize);
    QSignalSpy fontChanged(&plugin, &CodeEditorPlugin::editorFontChanged);

    plugin.handleEvent(QStringLiteral("slotdeck"), QString::fromLatin1(contentFontStepTopic), {{QStringLiteral("step"), QStringLiteral("increase")}});
    EXPECT_EQ(plugin.editorFont().size, defaultEditorFontSize + 1);
    plugin.handleEvent(QStringLiteral("slotdeck"), QString::fromLatin1(contentFontStepTopic), {{QStringLiteral("step"), QStringLiteral("decrease")}});
    EXPECT_EQ(plugin.editorFont().size, defaultEditorFontSize);
    plugin.setEditorFontSize(ui::maximumContentFontSize);
    plugin.handleEvent(QStringLiteral("slotdeck"), QString::fromLatin1(contentFontStepTopic), {{QStringLiteral("step"), QStringLiteral("reset")}});
    EXPECT_EQ(plugin.editorFont().size, defaultEditorFontSize);

    const qsizetype applied = fontChanged.count();
    plugin.handleEvent(QStringLiteral("slotdeck"), QString::fromLatin1(contentFontStepTopic), {{QStringLiteral("step"), QStringLiteral("bigger")}});
    plugin.handleEvent(QStringLiteral("slotdeck"), QString::fromLatin1(contentFontStepTopic), {{QStringLiteral("step"), 1}});
    plugin.handleEvent(QStringLiteral("slotdeck"), QString::fromLatin1(contentFontStepTopic), {{QStringLiteral("step"), QStringLiteral("increase")}, {QStringLiteral("extra"), true}});
    plugin.handleEvent(QStringLiteral("slotdeck"), QStringLiteral("slotdeck.appearance.unknown"), {{QStringLiteral("step"), QStringLiteral("increase")}});
    EXPECT_EQ(plugin.editorFont().size, defaultEditorFontSize);
    EXPECT_EQ(fontChanged.count(), applied);

    plugin.setEditorFontSize(ui::minimumContentFontSize - 1);
    plugin.setEditorFontSize(ui::maximumContentFontSize + 1);
    EXPECT_EQ(plugin.editorFont().size, defaultEditorFontSize);
    EXPECT_EQ(fontChanged.count(), applied);
    plugin.shutdown();
}

TEST(CodeEditorPluginTest, RollsBackFailedPersistenceAndCancelsCallbacksDuringShutdown) {
    test::TestPluginHost host;
    CodeEditorPlugin plugin;
    CodeEditorTestsHelper::installSettingsDocument(host, false);
    ASSERT_TRUE(plugin.initialize(host).hasValue());
    auto transaction = std::make_shared<QPromise<utils::Result<void>>>();
    transaction->start();
    // clang-format off
    host.transactionFutureHandler = [transaction](const QVector<persistence::DatabaseStatement>&) { return transaction->future(); };
    // clang-format on
    QTemporaryDir root;
    ASSERT_TRUE(root.isValid());
    ASSERT_TRUE(plugin.openWorkspace(root.path()).hasValue());
    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { return !host.databaseTransactions.isEmpty(); }));
    // clang-format on

    transaction->addResult(utils::Result<void>::failure({"write_failed", "Write failed", {}}));
    transaction->finish();
    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { return plugin.workspaces().isEmpty(); }));
    // clang-format on
    ASSERT_EQ(host.notifications.size(), 1);
    EXPECT_EQ(host.notifications.first().severity, AlertSeverity::Error);

    auto pendingShutdown = std::make_shared<QPromise<utils::Result<void>>>();
    pendingShutdown->start();
    // clang-format off
    host.transactionFutureHandler = [pendingShutdown](const QVector<persistence::DatabaseStatement>&) { return pendingShutdown->future(); };
    // clang-format on
    ASSERT_TRUE(plugin.openWorkspace(root.path()).hasValue());
    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { return host.databaseTransactions.size() >= 2; }));
    // clang-format on
    const int notifications = static_cast<int>(host.notifications.size());
    plugin.shutdown();
    pendingShutdown->addResult(utils::Result<void>::failure({"write_failed", "Write failed", {}}));
    pendingShutdown->finish();
    QCoreApplication::processEvents();
    EXPECT_EQ(host.notifications.size(), notifications);
}

TEST(CodeDocumentTest, LoadsHighlightsEditsAndSavesUtf8TextAsynchronously) {
    filesystem::FileSystemService service;
    test::TestPluginHost host;
    host.useFileSystem(service);
    QTemporaryDir root;
    ASSERT_TRUE(root.isValid());
    const QString path = QDir(root.path()).filePath(QStringLiteral("main.cpp"));
    ASSERT_TRUE(test::awaitFuture(service.createFile(path)).hasValue());
    ASSERT_TRUE(test::awaitFuture(service.writeFile(path, QByteArrayLiteral("int value = 1;\n"))).hasValue());

    CodeDocument document(path, root.path(), false, CodeEditorFont{}, TextCharset::Latin1, host);
    QSignalSpy loaded(&document, &CodeDocument::loaded);
    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { return loaded.count() == 1; }));
    // clang-format on
    EXPECT_EQ(document.language().id, QStringLiteral("cpp"));
    EXPECT_EQ(document.editor().toPlainText(), QStringLiteral("int value = 1;\n"));
    document.editor().appendPlainText(QStringLiteral("return;"));
    EXPECT_TRUE(document.dirty());
    document.save();
    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { return !document.dirty(); }));
    // clang-format on
    EXPECT_TRUE(test::awaitFuture(service.readFile(path, 1024)).value().contains("return;"));
}

TEST(CodeDocumentTest, RejectsBinaryAndInvalidUtf8FilesWithFriendlyErrors) {
    filesystem::FileSystemService service;
    test::TestPluginHost host;
    host.useFileSystem(service);
    QTemporaryDir root;
    ASSERT_TRUE(root.isValid());
    const QString path = QDir(root.path()).filePath(QStringLiteral("data.txt"));
    ASSERT_TRUE(test::awaitFuture(service.createFile(path)).hasValue());
    ASSERT_TRUE(test::awaitFuture(service.writeFile(path, QByteArray("abc\0def", 7))).hasValue());

    CodeDocument binary(path, root.path(), false, CodeEditorFont{}, TextCharset::Latin1, host);
    QSignalSpy binaryError(&binary, &CodeDocument::operationFailed);
    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { return binaryError.count() == 1; }));
    // clang-format on
    EXPECT_EQ(binaryError.first().first().toString(), QStringLiteral("code-editor.error.binary-file"));

    // A byte sequence that carries no mark and spells no valid UTF-8 opens in the encoding the settings declare, which is named in the status bar.
    ASSERT_TRUE(test::awaitFuture(service.writeFile(path, QByteArray::fromHex("c328"))).hasValue());
    CodeDocument unmarked(path, root.path(), false, CodeEditorFont{}, TextCharset::Latin1, host);
    QSignalSpy unmarkedLoaded(&unmarked, &CodeDocument::loaded);
    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { return unmarkedLoaded.count() == 1; }));
    // clang-format on
    EXPECT_EQ(unmarked.charset(), TextCharset::Latin1);

    // Latin-1 returns every byte it was given, so saving a file read that way writes back exactly what it held.
    unmarked.editor().insertPlainText(QStringLiteral("x"));
    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { return unmarked.dirty(); }));
    // clang-format on
    unmarked.editor().undo();
    unmarked.save();
    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { return !unmarked.dirty(); }));
    // clang-format on
    EXPECT_EQ(test::awaitFuture(service.readFile(path, 64)).value(), QByteArray::fromHex("c328"));
}

TEST(FileFinderTest, RanksThePathsAWrittenQueryReallyNames) {
    const QStringList paths{QStringLiteral("src/main.cpp"), QStringLiteral("src/ui/MainWindow.cpp"), QStringLiteral("docs/manual.md"), QStringLiteral("tests/main_test.cpp")};

    // The characters are found in order, so a query names a file without spelling its whole path.
    EXPECT_GE(fileMatchScore(QStringLiteral("src/main.cpp"), QStringLiteral("main")), 0);
    EXPECT_GE(fileMatchScore(QStringLiteral("src/ui/MainWindow.cpp"), QStringLiteral("mwin")), 0);
    EXPECT_LT(fileMatchScore(QStringLiteral("src/main.cpp"), QStringLiteral("zzz")), 0);

    // A run found together is worth more than the same characters found apart.
    EXPECT_GT(fileMatchScore(QStringLiteral("main.cpp"), QStringLiteral("main")), fileMatchScore(QStringLiteral("m-a-i-n.cpp"), QStringLiteral("main")));

    // A match in the name outranks one that only the directory above it carries.
    EXPECT_GT(fileMatchScore(QStringLiteral("other/docs.md"), QStringLiteral("docs")), fileMatchScore(QStringLiteral("docs/other.md"), QStringLiteral("docs")));

    const QStringList ranked = rankedFileMatches(paths, QStringLiteral("main"), 10);
    EXPECT_FALSE(ranked.isEmpty());
    EXPECT_FALSE(ranked.contains(QStringLiteral("docs/manual.md")));
    EXPECT_TRUE(ranked.contains(QStringLiteral("src/main.cpp")));

    // An empty query names every file, and the count asked for is the most that come back.
    EXPECT_EQ(rankedFileMatches(paths, QString{}, 10).size(), paths.size());
    EXPECT_EQ(rankedFileMatches(paths, QString{}, 2).size(), 2);
}

TEST(CodeWorkspaceViewTest, FindsAFileByPartOfItsNameAndOpensIt) {
    test::TestPluginHost host;
    host.translations = translations::catalog().value(QStringLiteral("en"));
    filesystem::FileSystemService service;
    host.useFileSystem(service);
    QTemporaryDir root;
    ASSERT_TRUE(root.isValid());
    const QString canonicalRoot = QFileInfo(root.path()).canonicalFilePath();
    ASSERT_TRUE(QDir(canonicalRoot).mkpath(QStringLiteral("nested/deep")));
    ASSERT_TRUE(CodeEditorTestsHelper::writeTestFile(QDir(canonicalRoot).filePath(QStringLiteral("nested/deep/target.txt")), QByteArrayLiteral("found\n")));
    ASSERT_TRUE(CodeEditorTestsHelper::writeTestFile(QDir(canonicalRoot).filePath(QStringLiteral("other.txt")), QByteArrayLiteral("other\n")));

    const QDateTime now = QDateTime::currentDateTimeUtc();
    CodeWorkspaceView view({QStringLiteral("workspace"), canonicalRoot, 0, true, now, now, {}}, {}, false, CodeEditorFont{}, TextCharset::Latin1, host);
    view.resize(1200, 800);

    auto* action = view.findChild<QAction*>(QStringLiteral("codeEditorFindFileAction"));
    ASSERT_NE(action, nullptr);
    action->trigger();

    // The folder is walked away from the interface, so the form appears once that walk has answered.
    FileFinder* finder = nullptr;
    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { finder = view.findChild<FileFinder*>(QStringLiteral("codeEditorFileFinder")); return finder != nullptr; }));
    // clang-format on
    auto* query = finder->findChild<QLineEdit*>(QStringLiteral("codeEditorFileFinderQuery"));
    auto* matches = finder->findChild<QListWidget*>(QStringLiteral("codeEditorFileFinderMatches"));
    ASSERT_NE(query, nullptr);
    ASSERT_NE(matches, nullptr);
    EXPECT_EQ(matches->count(), 2);

    // Typing part of the name leaves only the file that carries it.
    query->setText(QStringLiteral("target"));
    ASSERT_EQ(matches->count(), 1);
    EXPECT_EQ(matches->item(0)->text(), QStringLiteral("nested/deep/target.txt"));
    EXPECT_EQ(finder->chosenPath(), QDir(canonicalRoot).filePath(QStringLiteral("nested/deep/target.txt")));

    // Choosing it opens that file in the workspace.
    emit query->returnPressed();
    auto* documents = view.findChild<QTabWidget*>(QStringLiteral("codeEditorDocuments"));
    ASSERT_NE(documents, nullptr);
    EXPECT_EQ(documents->count(), 1);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    EXPECT_EQ(view.findChild<FileFinder*>(QStringLiteral("codeEditorFileFinder")), nullptr);
}

TEST(CodeWorkspaceViewTest, ReadsAndWritesTheOpenFileInAnEncodingTheReaderChooses) {
    test::TestPluginHost host;
    host.translations = translations::catalog().value(QStringLiteral("en"));
    filesystem::FileSystemService service;
    host.useFileSystem(service);
    QTemporaryDir root;
    ASSERT_TRUE(root.isValid());
    const QString canonicalRoot = QFileInfo(root.path()).canonicalFilePath();
    const QString path = QDir(canonicalRoot).filePath(QStringLiteral("notes.txt"));

    // The file spells valid UTF-8, so it is read as UTF-8 until the reader says otherwise.
    ASSERT_TRUE(CodeEditorTestsHelper::writeTestFile(path, QByteArrayLiteral("caf\xC3\xA9\n")));
    const QDateTime now = QDateTime::currentDateTimeUtc();
    CodeWorkspaceView view({QStringLiteral("workspace"), canonicalRoot, 0, true, now, now, {}}, {}, false, CodeEditorFont{}, TextCharset::Latin1, host);
    view.resize(1200, 800);
    view.openFile(path);
    auto* documents = view.findChild<QTabWidget*>(QStringLiteral("codeEditorDocuments"));
    ASSERT_NE(documents, nullptr);
    auto* document = qobject_cast<CodeDocument*>(documents->widget(0));
    ASSERT_NE(document, nullptr);
    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { return !document->editor().toPlainText().isEmpty(); }));
    // clang-format on
    EXPECT_EQ(document->charset(), TextCharset::Utf8);
    EXPECT_EQ(document->editor().toPlainText(), QString::fromUtf8("caf\xC3\xA9\n"));

    auto* encoding = view.findChild<QToolButton*>(QStringLiteral("codeEditorEncoding"));
    ASSERT_NE(encoding, nullptr);
    EXPECT_TRUE(encoding->isEnabled());
    EXPECT_EQ(encoding->text(), QStringLiteral("UTF-8"));

    // The control offers every encoding for reading the bytes again and every one for writing them.
    encoding->click();
    auto* menu = view.findChild<QMenu*>();
    ASSERT_NE(menu, nullptr);
    QVector<QAction*> entries;

    for (auto* candidate : menu->findChildren<QAction*>()) {
        if (!candidate->data().toString().isEmpty()) {
            entries.append(candidate);
        }
    }

    EXPECT_EQ(entries.size(), textCharsets().size() * 2);

    // Reading the same bytes as Latin-1 shows the two bytes the accent really is.
    QAction* reopenLatin1 = nullptr;
    QAction* saveUtf16 = nullptr;

    for (auto* candidate : entries) {
        if (candidate->data().toString() == QStringLiteral("reopen/latin1")) {
            reopenLatin1 = candidate;
        }
        if (candidate->data().toString() == QStringLiteral("save/utf-16le")) {
            saveUtf16 = candidate;
        }
    }

    ASSERT_NE(reopenLatin1, nullptr);
    ASSERT_NE(saveUtf16, nullptr);
    const QString asLatin1 = QString::fromUtf8("caf\xC3\x83\xC2\xA9\n");
    reopenLatin1->trigger();
    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { return document->editor().toPlainText() == asLatin1; }));
    // clang-format on
    EXPECT_EQ(document->charset(), TextCharset::Latin1);
    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { return encoding->text() == QStringLiteral("Latin-1"); }));
    // clang-format on

    // Writing them in another encoding rewrites the file whole, even though nothing was typed.
    saveUtf16->trigger();
    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { const auto written = test::awaitFuture(service.readFile(path, 128)); return written.hasValue() && written.value().startsWith(QByteArrayLiteral("\xFF\xFE")); }));
    // clang-format on
    EXPECT_EQ(document->charset(), TextCharset::Utf16Le);

    // The bytes written spell the same text the buffer held, in the encoding that was chosen for them.
    const auto written = test::awaitFuture(service.readFile(path, 128));
    ASSERT_TRUE(written.hasValue());
    QStringDecoder decoder(QStringConverter::Utf16LE);
    EXPECT_EQ(decoder.decode(written.value().mid(2)), asLatin1);
    menu->close();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

TEST(CodeDocumentTest, RefusesToSaveACharacterTheDeclaredEncodingCannotWrite) {
    filesystem::FileSystemService service;
    test::TestPluginHost host;
    host.translations = translations::catalog().value(QStringLiteral("en"));
    host.useFileSystem(service);
    QTemporaryDir root;
    ASSERT_TRUE(root.isValid());
    const QString path = QDir(root.path()).filePath(QStringLiteral("notes.txt"));
    ASSERT_TRUE(CodeEditorTestsHelper::writeTestFile(root.path() + QStringLiteral("/.editorconfig"), QByteArrayLiteral("root = true\n[*]\ncharset = latin1\n")));
    ASSERT_TRUE(CodeEditorTestsHelper::writeTestFile(path, QByteArrayLiteral("plain\n")));

    CodeDocument document(path, root.path(), false, CodeEditorFont{}, TextCharset::Latin1, host);
    QSignalSpy loaded(&document, &CodeDocument::loaded);
    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { return loaded.count() == 1 && document.charset() == TextCharset::Latin1; }));
    // clang-format on

    // A character the declared encoding cannot spell would be written as a question mark, so the save is refused instead.
    QSignalSpy failed(&document, &CodeDocument::operationFailed);
    document.editor().insertPlainText(QString::fromUtf8("\u4e2d"));
    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { return document.dirty(); }));
    // clang-format on
    document.save();
    ASSERT_EQ(failed.count(), 1);
    EXPECT_TRUE(failed.first().first().toString().contains(QStringLiteral("latin1")));
    EXPECT_EQ(test::awaitFuture(service.readFile(path, 64)).value(), QByteArrayLiteral("plain\n"));

    // Removing it lets the same buffer be written, because every character left is one Latin-1 spells.
    document.editor().undo();
    document.save();
    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { return !document.dirty(); }));
    // clang-format on
    EXPECT_EQ(failed.count(), 1);
}

TEST(CodeDocumentTest, ReloadsCleanExternalChangesAndPreservesDirtyBuffers) {
    filesystem::FileSystemService service;
    test::TestPluginHost host;
    host.useFileSystem(service);
    QTemporaryDir root;
    ASSERT_TRUE(root.isValid());
    const QString path = QDir(root.path()).filePath(QStringLiteral("external.cpp"));
    ASSERT_TRUE(test::awaitFuture(service.createFile(path)).hasValue());
    ASSERT_TRUE(test::awaitFuture(service.writeFile(path, QByteArrayLiteral("first\n"))).hasValue());

    CodeDocument document(path, root.path(), false, CodeEditorFont{}, TextCharset::Latin1, host);
    QSignalSpy loaded(&document, &CodeDocument::loaded);
    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { return loaded.count() == 1; }));
    // clang-format on
    ASSERT_TRUE(CodeEditorTestsHelper::writeTestFile(path, QByteArrayLiteral("second\n")));
    ASSERT_TRUE(QMetaObject::invokeMethod(&document, "watchedFileChanged"));
    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { return document.editor().toPlainText() == QStringLiteral("second\n"); }));
    // clang-format on

    document.editor().appendPlainText(QStringLiteral("unsaved"));
    QSignalSpy conflict(&document, &CodeDocument::externalChangeConflict);
    ASSERT_TRUE(CodeEditorTestsHelper::writeTestFile(path, QByteArrayLiteral("third\n")));
    ASSERT_TRUE(QMetaObject::invokeMethod(&document, "watchedFileChanged"));
    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { return conflict.count() >= 1; }));
    // clang-format on
    EXPECT_TRUE(document.editor().toPlainText().contains(QStringLiteral("unsaved")));
}

TEST(CodeDocumentTest, KeepsWhatWasTypedWhileTheFileItWasReadingWasStillBeingDecoded) {
    filesystem::FileSystemService service;
    test::TestPluginHost host;
    host.useFileSystem(service);
    QTemporaryDir root;
    ASSERT_TRUE(root.isValid());
    const QString path = QDir(root.path()).filePath(QStringLiteral("raced.cpp"));
    ASSERT_TRUE(test::awaitFuture(service.createFile(path)).hasValue());
    ASSERT_TRUE(test::awaitFuture(service.writeFile(path, QByteArrayLiteral("first\n"))).hasValue());

    CodeDocument document(path, root.path(), false, CodeEditorFont{}, TextCharset::Latin1, host);
    QSignalSpy loaded(&document, &CodeDocument::loaded);
    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { return loaded.count() == 1; }));
    // clang-format on

    ASSERT_TRUE(CodeEditorTestsHelper::writeTestFile(path, QByteArrayLiteral("second\n")));
    QSignalSpy conflict(&document, &CodeDocument::externalChangeConflict);
    ASSERT_TRUE(QMetaObject::invokeMethod(&document, "watchedFileChanged"));
    document.editor().appendPlainText(QStringLiteral("typed while it was reading"));
    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { return conflict.count() >= 1; }));
    // clang-format on
    EXPECT_TRUE(document.editor().toPlainText().contains(QStringLiteral("typed while it was reading")));
    EXPECT_TRUE(document.dirty());
}

TEST(CodeDocumentTest, WritesTheEditThatArrivedWhileTheEarlierSaveWasStillInFlight) {
    filesystem::FileSystemService service;
    test::TestPluginHost host;
    host.useFileSystem(service);
    QTemporaryDir root;
    ASSERT_TRUE(root.isValid());
    const QString path = QDir(root.path()).filePath(QStringLiteral("queued-save.cpp"));
    ASSERT_TRUE(test::awaitFuture(service.createFile(path)).hasValue());
    ASSERT_TRUE(test::awaitFuture(service.writeFile(path, QByteArrayLiteral("first\n"))).hasValue());

    CodeDocument document(path, root.path(), false, CodeEditorFont{}, TextCharset::Latin1, host);
    QSignalSpy loaded(&document, &CodeDocument::loaded);
    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { return loaded.count() == 1; }));
    // clang-format on

    document.editor().setPlainText(QStringLiteral("earlier"));
    document.save();
    document.editor().setPlainText(QStringLiteral("later"));
    document.save();
    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { return !document.dirty(); }));
    // clang-format on
    const auto stored = test::awaitFuture(service.readFile(path, 1024));
    ASSERT_TRUE(stored.hasValue());
    EXPECT_EQ(QString::fromUtf8(stored.value()), QStringLiteral("later"));
}

TEST(CodeWorkspaceViewTest, SurvivesManyDocumentsOpenedEditedSavedAndClosedInOneWorkspace) {
    // Thirty documents reach one serialized worker, so the wait is measured by what it asks for and a refused replace is answered the way a reader answers it.
    constexpr int savingBudgetMilliseconds = 30000;
    constexpr int savingAttempts = 4;
    filesystem::FileSystemService service;
    test::TestPluginHost host;
    host.useFileSystem(service);
    QTemporaryDir root;
    ASSERT_TRUE(root.isValid());
    QStringList paths;

    for (int index = 0; index < 30; ++index) {
        const QString path = QDir(root.path()).filePath(QStringLiteral("source-%1.cpp").arg(index));
        ASSERT_TRUE(test::awaitFuture(service.createFile(path)).hasValue());
        ASSERT_TRUE(test::awaitFuture(service.writeFile(path, QByteArrayLiteral("int value = 0;\n"))).hasValue());
        paths.append(path);
    }

    const QDateTime now = QDateTime::currentDateTimeUtc();
    CodeWorkspaceView view({QStringLiteral("workspace"), root.path(), 0, true, now, now, {}}, {}, false, CodeEditorFont{}, TextCharset::Latin1, host);
    auto* documents = view.findChild<ui::TabWidget*>(QStringLiteral("codeEditorDocuments"));
    ASSERT_NE(documents, nullptr);

    QStringList failures;
    // clang-format off
    QObject::connect(&view, &CodeWorkspaceView::operationFailed, &view, [&failures](const QString& message) { failures.append(message); });
    // clang-format on

    for (int round = 0; round < 3; ++round) {
        for (const auto& path : paths) {
            view.openFile(path);
        }
        ASSERT_TRUE(failures.isEmpty()) << failures.join(QStringLiteral(" | ")).toStdString();
        // clang-format off
        ASSERT_TRUE(test::waitUntil([&]() { return documents->count() == paths.size(); }));
        // clang-format on
        for (int index = 0; index < documents->count(); ++index) {
            auto* document = qobject_cast<CodeDocument*>(documents->widget(index));
            ASSERT_NE(document, nullptr);
            document->editor().appendPlainText(QStringLiteral("int added = %1;").arg(round));
        }
        // clang-format off
        const auto everyDocumentIsClean = [&]() { for (int index = 0; index < documents->count(); ++index) { if (qobject_cast<CodeDocument*>(documents->widget(index))->dirty()) { return false; } } return true; };
        // clang-format on

        // A platform may refuse to replace a file another process is holding, and such a save reports and leaves the document dirty on purpose, so the reader saves again.
        bool saved = false;
        for (int attempt = 0; attempt < savingAttempts && !saved; ++attempt) {
            failures.clear();
            view.saveAll();
            saved = test::waitUntil(everyDocumentIsClean, savingBudgetMilliseconds);
            ASSERT_TRUE(saved || !failures.isEmpty()) << "round " << round << " left documents dirty with nothing reported";
        }

        ASSERT_TRUE(saved) << "round " << round << " never saved, reported: " << failures.join(QStringLiteral(" | ")).toStdString();
        while (documents->count() > 0) {
            documents->setCurrentIndex(0);
            view.closeCurrentDocument();
        }
        QApplication::processEvents();
    }

    // Every edit of every round reached the file, so nothing was dropped by a save that raced a close.
    const auto stored = test::awaitFuture(service.readFile(paths.first(), 4096));
    ASSERT_TRUE(stored.hasValue());
    EXPECT_TRUE(QString::fromUtf8(stored.value()).contains(QStringLiteral("int added = 2;")));
    EXPECT_EQ(documents->count(), 0);
}

TEST(CodeWorkspaceViewTest, TracksExternalTreeChangesAndRejectsSymlinkEscape) {
    filesystem::FileSystemService service;
    test::TestPluginHost host;
    host.useFileSystem(service);
    QTemporaryDir root;
    QTemporaryDir external;
    ASSERT_TRUE(root.isValid());
    ASSERT_TRUE(external.isValid());
    const QDateTime now = QDateTime::currentDateTimeUtc();
    CodeWorkspaceView view({QStringLiteral("workspace"), root.path(), 0, true, now, now, {}}, {}, false, CodeEditorFont{}, TextCharset::Latin1, host);
    auto* model = view.findChild<QFileSystemModel*>();
    ASSERT_NE(model, nullptr);

    const QString created = QDir(root.path()).filePath(QStringLiteral("created.cpp"));
    ASSERT_TRUE(test::awaitFuture(service.createFile(created)).hasValue());
    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { return model->index(created).isValid(); }));
    // clang-format on

    const QString externalFile = QDir(external.path()).filePath(QStringLiteral("outside.cpp"));
    ASSERT_TRUE(test::awaitFuture(service.createFile(externalFile)).hasValue());
    const QString link = QDir(root.path()).filePath(QStringLiteral("outside.cpp"));

    if (!QFile::link(externalFile, link) || !QFileInfo(link).isSymLink()) {
        GTEST_SKIP() << "The platform did not allow creating a symbolic link";
    }

    QSignalSpy error(&view, &CodeWorkspaceView::operationFailed);
    view.openFile(link);
    ASSERT_EQ(error.count(), 1);
    EXPECT_EQ(error.first().first().toString(), QStringLiteral("code-editor.error.path-outside"));
}

TEST(CodeEditorTranslationsTest, SpellsEveryKeyInEveryLanguageTheSelectorOffers) {
    slotdeck::plugins::codeeditor::CodeEditorPlugin plugin;
    slotdeck::test::expectCompleteCatalog(QStringLiteral("code-editor"), plugin.translations());
}

TEST(EditorConfigTest, AnswersEveryHostileDocumentInsteadOfReadingPastIt) {
    const QStringList hostile{
        QString{}, QStringLiteral("["), QStringLiteral("[*"), QStringLiteral("[*]"), QStringLiteral("[{"), QStringLiteral("[{a"), QStringLiteral("[{a,"), QStringLiteral("[a-"), QStringLiteral("[!"), QStringLiteral("[/]"), QStringLiteral("[**"), QStringLiteral("[{1..}]"), QStringLiteral("[{..2}]"), QStringLiteral("[{99999999999999999999..99999999999999999999}]"), QStringLiteral("[{1..999999999}]"), QStringLiteral("[") + QString(4096, QLatin1Char('*')) + QStringLiteral("]\nindent_size = 4"), QStringLiteral("[") + QString(200, QLatin1Char('{')) + QStringLiteral("]\nindent_size = 4"), QString(200, QLatin1Char('{')), QStringLiteral("root"), QStringLiteral("root ="), QStringLiteral("= value"), QStringLiteral("indent_size ="), QStringLiteral("indent_size = notanumber"), QStringLiteral("indent_size = 99999999999999999999"), QStringLiteral("indent_size = -1"), QStringLiteral("[*]\nindent_style"), QStringLiteral("[*]\n\n\n[*]\n"), QString::fromUtf8("[*]\ncharset = \xC3\x28"),

    };

    for (const auto& text : hostile) {
        const QVector<EditorConfigFile> files{{QStringLiteral("/workspace"), text}};
        const auto properties = resolveEditorConfig(QStringLiteral("/workspace/source.cpp"), files);
        EXPECT_GE(resolvedIndentWidth(properties), 0) << text.toStdString();
        EXPECT_FALSE(editorConfigSectionMatches(text, QStringLiteral("/workspace"), QStringLiteral("/workspace/source.cpp")) && text.isEmpty());
    }

    // Pseudo-random documents are seeded, so a failure here reproduces exactly.
    quint32 seed = 0x5eed1234;
    for (int round = 0; round < 400; ++round) {
        QString text;
        const int length = static_cast<int>(seed % 120) + 1;
        for (int index = 0; index < length; ++index) {
            seed = seed * 1664525U + 1013904223U;
            static const QString alphabet = QStringLiteral("[]{}*?!,.-=\n abc019_/\\");
            text.append(alphabet.at(static_cast<int>(seed >> 16) % alphabet.size()));
        }
        const QVector<EditorConfigFile> files{{QStringLiteral("/workspace"), text}};
        const auto properties = resolveEditorConfig(QStringLiteral("/workspace/source.cpp"), files);
        EXPECT_GE(resolvedIndentWidth(properties), 0);
        std::ignore = editorConfigSectionMatches(text, QStringLiteral("/workspace"), QStringLiteral("/workspace/source.cpp"));
    }
}

TEST(LanguageServerClientTest, AnswersEveryMalformedFrameInsteadOfReadingPastIt) {
    QTemporaryDir root;
    ASSERT_TRUE(root.isValid());
    const QString path = QDir(root.path()).filePath(QStringLiteral("main.cpp"));

    // Each shape is one way a server can frame a message wrongly, and every one must end in an answer.
    for (int shape = 0; shape <= 11; ++shape) {
        LanguageServerClient client({QStringLiteral("cpp"), QCoreApplication::applicationFilePath(), {QStringLiteral("--slotdeck-test-lsp-malformed"), QString::number(shape)}}, root.path());
        int answers = 0;
        // clang-format off
        QObject::connect(&client, &LanguageServerClient::serverError, &client, [&answers](const QString&) { ++answers; });
        QObject::connect(&client, &LanguageServerClient::serverLog, &client, [&answers](const QString&) { ++answers; });
        QObject::connect(&client, &LanguageServerClient::stopped, &client, [&answers]() { ++answers; });
        // clang-format on

        client.openDocument(path, QStringLiteral("int main() {}"), QStringLiteral("cpp"));
        client.start();
        // clang-format off
        ASSERT_TRUE(test::waitUntil([&]() { return answers > 0; }, 30000)) << "shape " << shape;
        // clang-format on
        EXPECT_FALSE(client.ready()) << "shape " << shape;
        client.stop();
    }

    LanguageServerClient::drainTransports();
}

TEST(LanguageServerClientTest, StopsReadingAnOutlineNestedDeeperThanItDeclares) {
    QTemporaryDir root;
    ASSERT_TRUE(root.isValid());
    const QString path = QDir(root.path()).filePath(QStringLiteral("main.cpp"));
    LanguageServerClient client({QStringLiteral("cpp"), QCoreApplication::applicationFilePath(), {QStringLiteral("--slotdeck-test-lsp-deep-outline")}}, root.path());
    QVector<DocumentSymbolNode> outline;
    bool answered = false;
    // clang-format off
    QObject::connect(&client, &LanguageServerClient::documentSymbolsReady, &client, [&](const QString&, const QVector<DocumentSymbolNode>& symbols) { outline = symbols; answered = true; });
    // clang-format on

    client.openDocument(path, QStringLiteral("int main() {}"), QStringLiteral("cpp"));
    client.start();
    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { return client.ready(); }, 30000));
    // clang-format on
    client.requestDocumentSymbols(path);
    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { return answered; }, 30000));
    // clang-format on

    // An outline nests as deep as the server says, so what is read of it costs the depth this project declares.
    int depth = 0;
    const QVector<DocumentSymbolNode>* level = &outline;
    while (!level->isEmpty()) {
        ++depth;
        level = &level->first().children;
    }
    EXPECT_GT(depth, 0);
    EXPECT_LE(depth, 64);

    client.stop();
    LanguageServerClient::drainTransports();
}

} // namespace slotdeck::plugins::codeeditor
