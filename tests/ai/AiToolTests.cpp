#include "AiAgentSettingsView.h"
#include "AiTestSupport.h"

#include <QImage>

namespace slotdeck::plugins::ai {

TEST(AiChatClientTest, ReadsTheRejectionReasonFromEveryShapeAServiceReportsIt) {
    const ProviderDescriptor* openai = findProvider(QStringLiteral("openai"));
    ASSERT_NE(openai, nullptr);
    const QString model = QStringLiteral("gpt-4o");
    const ModelConnection connection{openai->id, model, {}, QStringLiteral("sk"), {}, defaultParameters(*openai, model), {}};

    // clang-format off
    const auto reasonOf = [&connection](const QByteArray& body) {
        QTcpServer server;
        if (!server.listen(QHostAddress::LocalHost, 0)) {
            return QString{};
        }
        QObject::connect(&server, &QTcpServer::newConnection, &server, [&server, body]() {
            QTcpSocket* socket = server.nextPendingConnection();
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket, body]() {
                if (!socket->readAll().contains(QByteArrayLiteral("\r\n\r\n"))) {
                    return;
                }
                socket->write(QByteArrayLiteral("HTTP/1.1 429 Too Many Requests\r\nRetry-After: 0\r\nContent-Length: ") + QByteArray::number(body.size()) + QByteArrayLiteral("\r\nConnection: close\r\n\r\n") + body);
                socket->disconnectFromHost();
            });
        });

        AiRequestGate clientGate;
        AiHttpChatClient client(clientGate);
        QVector<utils::Error> failures;
        QObject::connect(&client, &AiChatClient::failed, &client, [&failures](const utils::Error& error) { failures.append(error); });
        client.send({connection, QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort()), QJsonArray{QJsonObject{{QStringLiteral("role"), QStringLiteral("user")}, {QStringLiteral("content"), QStringLiteral("hello")}}}, {}}, [](const QString& key) { return key; });
        return test::waitUntil([&]() { return !failures.isEmpty(); }, 20000) && !failures.isEmpty() ? failures.first().message : QString{};
    };
    // clang-format on

    EXPECT_EQ(reasonOf(QByteArrayLiteral(R"({"error":{"type":"rate_limit","message":"slow down"}})")), QStringLiteral("rate_limit: slow down"));
    EXPECT_EQ(reasonOf(QByteArrayLiteral(R"({"error":"too many requests"})")), QStringLiteral("too many requests"));
    EXPECT_EQ(reasonOf(QByteArrayLiteral(R"({"message":"quota exhausted"})")), QStringLiteral("quota exhausted"));
    EXPECT_EQ(reasonOf(QByteArrayLiteral(R"({"detail":"rate limit reached for this key"})")), QStringLiteral("rate limit reached for this key"));

    // A service reporting no reason at all is quoted, because that body is still what explains the rejection.
    EXPECT_EQ(reasonOf(QByteArrayLiteral("Too Many Requests")), QStringLiteral("Too Many Requests"));
}

TEST(AiChatClientTest, SendsOnlyTheSamplingControlEachProviderAccepts) {
    const ProviderDescriptor* anthropic = findProvider(QStringLiteral("anthropic"));
    ASSERT_NE(anthropic, nullptr);
    const QString model = QStringLiteral("claude-3-haiku-20240307");
    const ModelConnection connection{anthropic->id, model, {}, QStringLiteral("sk"), {}, defaultParameters(*anthropic, model), {}};

    // The Anthropic API rejects a request carrying both sampling controls.
    // clang-format off
    const QJsonObject body = buildRequestBody(*anthropic, {connection, {}, QJsonArray{}, {}}, [](const QString& key) { return key; });
    // clang-format on
    EXPECT_TRUE(body.contains(QStringLiteral("temperature")));
    EXPECT_FALSE(body.contains(QStringLiteral("top_p")));

    for (const auto& parameter : applicableParameters(*anthropic, model)) {
        EXPECT_NE(parameter.id, QStringLiteral("topP"));
    }
}

TEST(AiTaskRepositoryTest, NeverBindsANullValueToATextColumnDeclaredNotNull) {
    test::TestPluginHost host;
    AiTaskRepository repository(host);
    ASSERT_TRUE(repository.initialize().hasValue());

    // A default execution carries null strings, which Qt would bind as SQL NULL against a NOT NULL column.
    TaskExecution execution;
    execution.id = QStringLiteral("execution-1");
    execution.taskId = QStringLiteral("task-1");
    execution.status = ExecutionStatus::Succeeded;
    execution.startedAtUtc = QDateTime::currentDateTimeUtc();
    execution.finishedAtUtc = execution.startedAtUtc.addSecs(1);
    ASSERT_TRUE(execution.finishReason.isNull());
    ASSERT_TRUE(execution.errorMessage.isNull());
    ASSERT_TRUE(execution.content.isNull());
    EXPECT_TRUE(test::awaitFuture(repository.finishExecution(execution)).hasValue());

    AiTask task;
    task.id = QStringLiteral("task-1");
    task.workspaceId = QStringLiteral("workspace-1");
    task.title = QStringLiteral("Review");
    task.prompt = QStringLiteral("Prompt");
    task.agentId = AiTestsHelper::testAgent().id;
    task.createdAtUtc = QDateTime::currentDateTimeUtc();
    task.updatedAtUtc = task.createdAtUtc;
    ASSERT_TRUE(task.description.isNull());
    EXPECT_TRUE(test::awaitFuture(repository.saveTask(task)).hasValue());

    qsizetype inspected = 0;

    for (const auto& recorded : host.databaseExecutions) {
        for (const auto& binding : recorded.value(QStringLiteral("bindings")).toList()) {
            EXPECT_FALSE(binding.typeId() == QMetaType::QString && binding.toString().isNull()) << recorded.value(QStringLiteral("statement")).toString().toStdString();
            ++inspected;
        }
    }

    for (const auto& transaction : host.databaseTransactions) {
        for (const auto& statement : transaction) {
            for (const auto& binding : statement.bindings) {
                EXPECT_FALSE(binding.typeId() == QMetaType::QString && binding.toString().isNull()) << statement.statement.toStdString();
                ++inspected;
            }
        }
    }

    EXPECT_GT(inspected, 0);
}

TEST(AiTaskDialogTest, LetsAnAgentChooseItsWorkingDirectoryAndValidatesEveryDeclaredField) {
    test::TestPluginHost host;
    host.translations = translations::english();
    QTemporaryDir root;
    ASSERT_TRUE(root.isValid());

    const ModelConnection anthropicConnection = declaredConnection(*findProvider(QStringLiteral("anthropic")), QStringLiteral("claude-opus-5"));
    const QVector<ModelConnection> connections{AiTestsHelper::testConnection(), anthropicConnection};
    AiTaskDialog dialog(host, QStringLiteral("workspace-1"), std::nullopt, {12, 60}, {AiTestsHelper::testAgent()}, nullptr);
    dialog.show();

    auto* kind = dialog.findChild<QComboBox*>(QStringLiteral("aiTaskExecutionKind"));
    auto* title = dialog.findChild<QLineEdit*>(QStringLiteral("aiTaskTitleField"));
    auto* prompt = dialog.findChild<QPlainTextEdit*>(QStringLiteral("aiTaskPromptField"));
    auto* workdir = dialog.findChild<QLineEdit*>(QStringLiteral("aiTaskWorkdir"));
    auto* command = dialog.findChild<QLineEdit*>(QStringLiteral("aiTaskCommand"));
    auto* validation = dialog.findChild<QLabel*>(QStringLiteral("aiTaskValidation"));
    auto* buttons = dialog.findChild<QDialogButtonBox*>();
    ASSERT_NE(kind, nullptr);
    ASSERT_NE(workdir, nullptr);
    ASSERT_NE(command, nullptr);
    ASSERT_NE(buttons, nullptr);

    // An agent needs the working directory its file tools are bound to, while the command fields belong to a command.
    EXPECT_TRUE(workdir->isVisible());
    EXPECT_FALSE(command->isVisible());
    EXPECT_TRUE(dialog.findChild<QComboBox*>(QStringLiteral("aiTaskAgent"))->isVisible());

    title->setText(QStringLiteral("Write the report"));
    prompt->setPlainText(QStringLiteral("Write it"));
    workdir->setText(QStringLiteral("relative/path"));
    QTest::mouseClick(buttons->button(QDialogButtonBox::Save), Qt::LeftButton);
    EXPECT_EQ(dialog.result(), 0);
    EXPECT_TRUE(validation->isVisible());
    EXPECT_EQ(validation->text(), host.translate(QStringLiteral("ai.validation.workdir")));

    workdir->setText(root.path());
    dialog.findChild<QLineEdit*>(QStringLiteral("aiTaskIssueUrl"))->setText(QStringLiteral("not-an-address"));
    QTest::mouseClick(buttons->button(QDialogButtonBox::Save), Qt::LeftButton);
    EXPECT_EQ(dialog.result(), 0);
    EXPECT_EQ(validation->text(), host.translate(QStringLiteral("ai.validation.issue-url")));

    dialog.findChild<QLineEdit*>(QStringLiteral("aiTaskIssueUrl"))->setText(QStringLiteral("https://github.com/paulo/slotdeck/issues/7"));
    QTest::mouseClick(buttons->button(QDialogButtonBox::Save), Qt::LeftButton);
    ASSERT_EQ(dialog.result(), static_cast<int>(QDialog::Accepted));
    EXPECT_EQ(dialog.task().executionKind, TaskExecutionKind::Agent);
    EXPECT_EQ(dialog.task().workdir, QDir::cleanPath(root.path()));

    // A new task starts in the home directory of the running system, and clearing the field means no file access at all.
    AiTaskDialog fresh(host, QStringLiteral("workspace-1"), std::nullopt, {12, 60}, {AiTestsHelper::testAgent()}, nullptr);
    fresh.show();
    EXPECT_EQ(fresh.findChild<QLineEdit*>(QStringLiteral("aiTaskWorkdir"))->text(), QDir::homePath());
    fresh.findChild<QLineEdit*>(QStringLiteral("aiTaskTitleField"))->setText(QStringLiteral("At home"));
    fresh.findChild<QPlainTextEdit*>(QStringLiteral("aiTaskPromptField"))->setPlainText(QStringLiteral("Answer"));
    QTest::mouseClick(fresh.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Save), Qt::LeftButton);
    ASSERT_EQ(fresh.result(), static_cast<int>(QDialog::Accepted));
    EXPECT_EQ(fresh.task().workdir, QDir::cleanPath(QDir::homePath()));

    AiTaskDialog withoutRoot(host, QStringLiteral("workspace-1"), std::nullopt, {12, 60}, {AiTestsHelper::testAgent()}, nullptr);
    withoutRoot.show();
    withoutRoot.findChild<QLineEdit*>(QStringLiteral("aiTaskWorkdir"))->setText(QString{});
    withoutRoot.findChild<QLineEdit*>(QStringLiteral("aiTaskTitleField"))->setText(QStringLiteral("No files"));
    withoutRoot.findChild<QPlainTextEdit*>(QStringLiteral("aiTaskPromptField"))->setPlainText(QStringLiteral("Answer"));
    QTest::mouseClick(withoutRoot.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Save), Qt::LeftButton);
    ASSERT_EQ(withoutRoot.result(), static_cast<int>(QDialog::Accepted));
    EXPECT_TRUE(withoutRoot.task().workdir.isEmpty());

    // The schedule kinds read from no schedule to the most expressive one instead of alphabetically.
    auto* scheduleKind = dialog.findChild<QComboBox*>(QStringLiteral("aiTaskScheduleKind"));
    ASSERT_NE(scheduleKind, nullptr);
    ASSERT_EQ(scheduleKind->count(), 4);
    EXPECT_TRUE(scheduleKind->itemData(0).toString().isEmpty());
    EXPECT_EQ(scheduleKind->itemData(1).toString(), AiTaskRepository::scheduleKindName(ScheduleKind::Once));
    EXPECT_EQ(scheduleKind->itemData(2).toString(), AiTaskRepository::scheduleKindName(ScheduleKind::Interval));
    EXPECT_EQ(scheduleKind->itemData(3).toString(), AiTaskRepository::scheduleKindName(ScheduleKind::Cron));

    // The task chooses one configured connection, and it opens on the default one.
    auto* agentBox = dialog.findChild<QComboBox*>(QStringLiteral("aiTaskAgent"));
    ASSERT_NE(agentBox, nullptr);
    EXPECT_EQ(agentBox->count(), 1);
    EXPECT_EQ(dialog.task().agentId, AiTestsHelper::testAgent().id);

    AiTaskDialog another(host, QStringLiteral("workspace-1"), std::nullopt, {12, 60}, {AiTestsHelper::testAgent()}, nullptr);
    another.show();
    another.findChild<QLineEdit*>(QStringLiteral("aiTaskTitleField"))->setText(QStringLiteral("Ported"));
    another.findChild<QPlainTextEdit*>(QStringLiteral("aiTaskPromptField"))->setPlainText(QStringLiteral("Answer"));
    QTest::mouseClick(another.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Save), Qt::LeftButton);
    ASSERT_EQ(another.result(), static_cast<int>(QDialog::Accepted));
    EXPECT_EQ(another.task().agentId, AiTestsHelper::testAgent().id);

    // Choosing the command kind exposes the command fields and keeps the same working directory row.
    AiTaskDialog commandDialog(host, QStringLiteral("workspace-1"), std::nullopt, {12, 60}, {AiTestsHelper::testAgent()}, nullptr);
    commandDialog.show();
    auto* commandKind = commandDialog.findChild<QComboBox*>(QStringLiteral("aiTaskExecutionKind"));
    commandKind->setCurrentIndex(commandKind->findData(AiTaskRepository::taskExecutionKindName(TaskExecutionKind::Command)));
    EXPECT_TRUE(commandDialog.findChild<QLineEdit*>(QStringLiteral("aiTaskCommand"))->isVisible());
    EXPECT_TRUE(commandDialog.findChild<QLineEdit*>(QStringLiteral("aiTaskWorkdir"))->isVisible());
    EXPECT_FALSE(commandDialog.findChild<QComboBox*>(QStringLiteral("aiTaskAgent"))->isVisible());

    commandDialog.findChild<QLineEdit*>(QStringLiteral("aiTaskTitleField"))->setText(QStringLiteral("Build"));
    QTest::mouseClick(commandDialog.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Save), Qt::LeftButton);
    EXPECT_EQ(commandDialog.findChild<QLabel*>(QStringLiteral("aiTaskValidation"))->text(), host.translate(QStringLiteral("ai.validation.command")));
    commandDialog.findChild<QLineEdit*>(QStringLiteral("aiTaskCommand"))->setText(QStringLiteral("make"));
    commandDialog.findChild<QLineEdit*>(QStringLiteral("aiTaskWorkdir"))->setText(root.path());
    QTest::mouseClick(commandDialog.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Save), Qt::LeftButton);
    ASSERT_EQ(commandDialog.result(), static_cast<int>(QDialog::Accepted));
    EXPECT_EQ(commandDialog.task().command, QStringLiteral("make"));
    EXPECT_EQ(commandDialog.task().commandTimeoutSeconds, 60);
    EXPECT_TRUE(commandDialog.task().agentId.isEmpty());
}

TEST(AiTaskInfoDialogTest, PresentsExecutionsLogsAndExplainsAnEmptyOutput) {
    test::TestPluginHost host;
    host.translations = translations::english();
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const AiWorkspace workspace{QStringLiteral("workspace-1"), QStringLiteral("Product"), 0, true, now, now};
    const AiTask task = AiTestsHelper::makeTask(QStringLiteral("task-1"), workspace.id);

    TaskExecution failed;
    failed.id = QStringLiteral("execution-1");
    failed.taskId = task.id;
    failed.status = ExecutionStatus::Failed;
    failed.startedAtUtc = now;
    failed.finishedAtUtc = now.addSecs(1);
    failed.errorMessage = QStringLiteral("invalid_request_error: model is unknown");
    AiTestsHelper::installAiRows(host, {workspace}, {task}, {});
    AiTestsHelper::installExecutionRows(host, {failed}, {{QStringLiteral("log-1"), failed.id, 1, now, ExecutionLogLevel::Error, ExecutionLogKind::Failed, failed.errorMessage}});

    AiPlugin plugin;
    ASSERT_TRUE(plugin.initialize(host).hasValue());
    AiTaskInfoDialog dialog(plugin, host, task, nullptr);
    dialog.resize(900, 600);
    dialog.show();

    auto* executionGrid = dialog.findChild<QTableWidget*>(QStringLiteral("aiExecutionGrid"));
    auto* logGrid = dialog.findChild<QTableWidget*>(QStringLiteral("aiLogGrid"));
    auto* outputPages = dialog.findChild<QStackedWidget*>(QStringLiteral("aiOutputPages"));
    auto* outputEmpty = dialog.findChild<QLabel*>(QStringLiteral("aiOutputEmpty"));
    ASSERT_NE(executionGrid, nullptr);
    ASSERT_NE(logGrid, nullptr);
    ASSERT_NE(outputPages, nullptr);
    ASSERT_NE(outputEmpty, nullptr);

    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { return executionGrid->rowCount() == 1 && logGrid->rowCount() == 1; }));
    // clang-format on
    ASSERT_EQ(executionGrid->rowCount(), 1);
    EXPECT_EQ(executionGrid->item(0, 1)->text(), QStringLiteral("Failed"));
    EXPECT_EQ(executionGrid->item(0, 4)->text(), failed.errorMessage);
    EXPECT_EQ(logGrid->item(0, 2)->text(), host.translations.value(QStringLiteral("ai.log-kind.failed")));
    EXPECT_EQ(logGrid->item(0, 3)->text(), failed.errorMessage);

    // A long message wraps instead of being truncated.
    EXPECT_TRUE(logGrid->wordWrap());
    EXPECT_TRUE(executionGrid->wordWrap());

    // An execution without returned text explains why instead of showing a blank surface.
    EXPECT_EQ(outputPages->currentWidget(), outputEmpty);
    EXPECT_EQ(outputEmpty->text(), failed.errorMessage);

    // The returned content is rendered as Markdown instead of being shown as its source.
    TaskExecution answered = failed;
    answered.id = QStringLiteral("execution-2");
    answered.status = ExecutionStatus::Succeeded;
    answered.errorMessage.clear();
    answered.content = QStringLiteral("# Title\n\nA paragraph with `code` and a [link](https://example.com).\n\n- first\n- second\n");
    AiTestsHelper::installExecutionRows(host, {answered}, {});
    AiTaskInfoDialog rendered(plugin, host, task, nullptr);
    rendered.resize(900, 600);
    rendered.show();
    auto* renderedContent = rendered.findChild<QTextBrowser*>(QStringLiteral("aiExecutionContent"));
    ASSERT_NE(renderedContent, nullptr);
    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { return !renderedContent->toPlainText().isEmpty(); }));
    // clang-format on
    EXPECT_EQ(renderedContent->toMarkdown().trimmed().isEmpty(), false);
    EXPECT_FALSE(renderedContent->toPlainText().contains(QStringLiteral("# Title")));
    EXPECT_TRUE(renderedContent->toPlainText().contains(QStringLiteral("Title")));
    EXPECT_TRUE(renderedContent->toHtml().contains(QStringLiteral("https://example.com")));
    EXPECT_EQ(renderedContent->frameShape(), QFrame::NoFrame);

    // A payload entry is opened on demand while every other kind stays readable in the grid.
    AiTestsHelper::installExecutionRows(host, {failed}, {{QStringLiteral("log-1"), failed.id, 1, now, ExecutionLogLevel::Error, ExecutionLogKind::Failed, failed.errorMessage}, {QStringLiteral("log-2"), failed.id, 2, now, ExecutionLogLevel::Debug, ExecutionLogKind::RequestSent, QStringLiteral("{\"model\":\"gpt-4o\"}")}});
    AiTaskInfoDialog payloads(plugin, host, task, nullptr);
    payloads.resize(900, 600);
    payloads.show();
    auto* payloadGrid = payloads.findChild<QTableWidget*>(QStringLiteral("aiLogGrid"));
    ASSERT_NE(payloadGrid, nullptr);
    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { return payloadGrid->rowCount() == 2; }));
    // clang-format on
    // The newest entry is the first row, so the payload it carries is the one on top.
    EXPECT_EQ(payloadGrid->cellWidget(1, 3), nullptr);
    EXPECT_EQ(payloadGrid->item(1, 3)->text(), failed.errorMessage);
    ASSERT_NE(payloadGrid->cellWidget(0, 3), nullptr);
    auto* openPayload = payloadGrid->cellWidget(0, 3)->findChild<QToolButton*>();
    ASSERT_NE(openPayload, nullptr);
    EXPECT_EQ(openPayload->text(), host.translate(QStringLiteral("ai.log.open-payload")));
    EXPECT_EQ(openPayload->objectName(), QStringLiteral("chipButton"));
    EXPECT_EQ(openPayload->height(), host.theme().metric(ui::ThemeMetric::BadgeRadius) * 2);
    EXPECT_TRUE(AiTaskRepository::carriesExchangedPayload(ExecutionLogKind::ResponseReceived));
    EXPECT_FALSE(AiTaskRepository::carriesExchangedPayload(ExecutionLogKind::UsageReported));

    // The payload answers nothing, so it opens without a nested loop and the chip that opened it survives the reload that follows.
    const QPointer<QWidget> chipCell = payloadGrid->cellWidget(0, 3);
    openPayload->click();
    auto* viewer = payloads.findChild<QDialog*>(QStringLiteral("aiPayloadDialog"));
    ASSERT_NE(viewer, nullptr);
    EXPECT_TRUE(viewer->isVisible());
    EXPECT_FALSE(chipCell.isNull());

    viewer->reject();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    EXPECT_EQ(payloads.findChild<QDialog*>(QStringLiteral("aiPayloadDialog")), nullptr);

    // The dialog opens with the shared page header, so it closes with the same divider as every other surface.
    ASSERT_NE(payloads.findChild<QWidget*>(QStringLiteral("pageHeader")), nullptr);

    // A command run exchanges nothing with a model and stores no artifact, so those surfaces do not exist for it.
    AiTask commandTask = task;
    commandTask.executionKind = TaskExecutionKind::Command;
    commandTask.command = QStringLiteral("make");
    AiTaskInfoDialog commandDialog(plugin, host, commandTask, nullptr);
    commandDialog.resize(900, 600);
    commandDialog.show();
    EXPECT_EQ(commandDialog.findChild<QTableWidget*>(QStringLiteral("aiLogGrid")), nullptr);
    EXPECT_EQ(commandDialog.findChild<QTableWidget*>(QStringLiteral("aiArtifactGrid")), nullptr);
    ASSERT_NE(commandDialog.findChild<QTableWidget*>(QStringLiteral("aiExecutionGrid")), nullptr);
    ASSERT_NE(commandDialog.findChild<QStackedWidget*>(QStringLiteral("aiOutputPages")), nullptr);

    // Selecting an execution of a command run must stay safe without those surfaces.
    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { return commandDialog.findChild<QTableWidget*>(QStringLiteral("aiExecutionGrid"))->rowCount() == 1; }));
    // clang-format on
    EXPECT_EQ(commandDialog.findChild<QTableWidget*>(QStringLiteral("aiExecutionGrid"))->currentRow(), 0);
}

TEST(AiServiceSettingsViewTest, AsksOnlyForWhatTheSelectedServiceDoesNotPublish) {
    test::TestPluginHost host;
    host.translations = translations::english();
    AiTestsHelper::installEmptyProviderRows(host);
    AiPlugin plugin;
    ASSERT_TRUE(plugin.initialize(host).hasValue());

    std::unique_ptr<QWidget> search(plugin.createSettingsSection(QStringLiteral("tools"), QStringLiteral("search"), nullptr));
    ASSERT_NE(search, nullptr);
    search->show();
    auto* service = search->findChild<QComboBox*>(QStringLiteral("aiSearchProvider"));
    auto* instance = search->findChild<QLineEdit*>(QStringLiteral("aiSearchInstance"));
    auto* searchKey = search->findChild<ui::SecretField*>(QStringLiteral("aiSearchKey"));
    ASSERT_NE(service, nullptr);
    ASSERT_NE(instance, nullptr);
    ASSERT_NE(searchKey, nullptr);

    // A hosted service publishes its endpoint, so only its credential is asked for and it arrives already referenced.
    EXPECT_FALSE(instance->isVisible());
    EXPECT_TRUE(searchKey->isVisible());
    EXPECT_EQ(searchKey->value(), QStringLiteral("{env.BRAVE_API_KEY}"));

    service->setCurrentIndex(service->findData(searchProviderIdentifier(SearchProvider::Tavily)));
    EXPECT_EQ(searchAddress({SearchProvider::Tavily, {}, {}}), QStringLiteral("https://api.tavily.com"));
    EXPECT_FALSE(instance->isVisible());

    // A self-hosted instance is the only one that carries an address, and it needs no key.
    service->setCurrentIndex(service->findData(searchProviderIdentifier(SearchProvider::SearxNg)));
    EXPECT_TRUE(instance->isVisible());
    EXPECT_FALSE(searchKey->isVisible());

    std::unique_ptr<QWidget> speech(plugin.createSettingsSection(QStringLiteral("tools"), QStringLiteral("speech"), nullptr));
    ASSERT_NE(speech, nullptr);
    speech->show();
    auto* speechService = speech->findChild<QComboBox*>(QStringLiteral("aiSpeechProvider"));
    auto* typedVoice = speech->findChild<QLineEdit*>(QStringLiteral("aiSpeechVoice"));
    auto* declaredVoice = speech->findChild<QComboBox*>(QStringLiteral("aiSpeechDeclaredVoice"));
    auto* speechKey = speech->findChild<ui::SecretField*>(QStringLiteral("aiSpeechKey"));
    ASSERT_NE(speechService, nullptr);
    ASSERT_NE(typedVoice, nullptr);
    ASSERT_NE(declaredVoice, nullptr);
    ASSERT_NE(speechKey, nullptr);

    // The address is never asked for, and an account catalog is typed while a closed set is chosen.
    EXPECT_EQ(speech->findChild<QLineEdit*>(QStringLiteral("aiSpeechAddress")), nullptr);
    EXPECT_EQ(speechKey->value(), QStringLiteral("{env.ELEVENLABS_API_KEY}"));
    EXPECT_TRUE(typedVoice->isVisible());
    EXPECT_FALSE(declaredVoice->isVisible());

    speechService->setCurrentIndex(speechService->findData(speechProviderIdentifier(SpeechProvider::OpenAi)));
    EXPECT_FALSE(typedVoice->isVisible());
    EXPECT_TRUE(declaredVoice->isVisible());
    EXPECT_GT(declaredVoice->count(), 0);
    EXPECT_EQ(declaredVoice->currentData().toString(), speechProviderDefaultVoice(SpeechProvider::OpenAi));
    plugin.shutdown();
}

TEST(AiPluginTest, BuildsEveryDeclaredSettingsSectionAndRefusesAnUndeclaredOne) {
    test::TestPluginHost host;
    host.translations = translations::english();
    AiTestsHelper::installEmptyProviderRows(host);
    AiPlugin plugin;
    ASSERT_TRUE(plugin.initialize(host).hasValue());

    const auto groups = plugin.settingsGroups();
    QStringList groupIds;
    QStringList sectionIds;

    for (const auto& group : groups) {
        groupIds.append(group.id);
        for (const auto& section : group.sections) {
            sectionIds.append(QStringLiteral("%1/%2").arg(group.id, section.id));
            std::unique_ptr<QWidget> page(plugin.createSettingsSection(group.id, section.id, nullptr));
            ASSERT_NE(page, nullptr) << qPrintable(section.id);
            page->show();
        }
    }

    EXPECT_EQ(groupIds, QStringList({QStringLiteral("connections"), QStringLiteral("providers"), QStringLiteral("agents"), QStringLiteral("tools"), QStringLiteral("general")}));
    EXPECT_EQ(sectionIds, QStringList({QStringLiteral("connections/general"), QStringLiteral("providers/selection"), QStringLiteral("providers/rate-limits"), QStringLiteral("agents/general"), QStringLiteral("tools/mcp"), QStringLiteral("tools/search"), QStringLiteral("tools/speech"), QStringLiteral("general/general")}));

    std::unique_ptr<QWidget> unknown(plugin.createSettingsSection(QStringLiteral("providers"), QStringLiteral("absent"), nullptr));
    EXPECT_EQ(unknown, nullptr);
    std::unique_ptr<QWidget> foreign(plugin.createSettingsSection(QStringLiteral("absent"), QStringLiteral("general"), nullptr));
    EXPECT_EQ(foreign, nullptr);

    // The server section starts from the empty state, because no server is registered yet.
    std::unique_ptr<QWidget> servers(plugin.createSettingsSection(QStringLiteral("tools"), QStringLiteral("mcp"), nullptr));
    ASSERT_NE(servers, nullptr);
    servers->resize(700, 400);
    servers->show();
    auto* grid = servers->findChild<QTableWidget*>(QStringLiteral("aiMcpGrid"));
    auto* empty = servers->findChild<QLabel*>(QStringLiteral("aiMcpEmpty"));
    ASSERT_NE(grid, nullptr);
    ASSERT_NE(empty, nullptr);
    EXPECT_FALSE(grid->isVisible());
    EXPECT_TRUE(empty->isVisible());
    plugin.shutdown();
}

TEST(AiMcpSettingsViewTest, OpensTheEditorOfTheServerThatWasDoubleClicked) {
    test::TestPluginHost host;
    host.translations = translations::english();
    AiTestsHelper::installEmptyProviderRows(host);
    const QJsonObject server{{QStringLiteral("id"), QStringLiteral("files")}, {QStringLiteral("transport"), QStringLiteral("stdio")}, {QStringLiteral("command"), QStringLiteral("mcp-files")}};
    host.settingsDocument = {{QStringLiteral("mcpServers"), QJsonArray{server}}};

    AiPlugin plugin;
    ASSERT_TRUE(plugin.initialize(host).hasValue());
    ASSERT_EQ(plugin.mcpServers().size(), 1);

    std::unique_ptr<QWidget> section(plugin.createSettingsSection(QStringLiteral("tools"), QStringLiteral("mcp"), nullptr));
    ASSERT_NE(section, nullptr);
    section->resize(760, 520);
    section->show();

    auto* grid = section->findChild<QTableWidget*>(QStringLiteral("aiMcpGrid"));
    ASSERT_NE(grid, nullptr);
    ASSERT_EQ(grid->rowCount(), 1);

    bool opened = false;
    // clang-format off
    QTimer::singleShot(0, section.get(), [&section, &opened]() { if (auto* dialog = section->findChild<QDialog*>(QStringLiteral("aiMcpServerDialog")); dialog != nullptr) { opened = true; dialog->reject(); } });
    // clang-format on
    grid->selectRow(0);
    emit grid->doubleClicked(grid->model()->index(0, 0));

    EXPECT_TRUE(opened);
    EXPECT_EQ(plugin.mcpServers().size(), 1);
    section.reset();
    plugin.shutdown();
}

TEST(AiProviderSettingsViewTest, GovernsEveryPerProviderSectionFromOneSelector) {
    test::TestPluginHost host;
    host.translations = translations::english();
    AiTestsHelper::installEmptyProviderRows(host);

    AiPlugin plugin;
    ASSERT_TRUE(plugin.initialize(host).hasValue());
    std::unique_ptr<QWidget> selection(plugin.createSettingsSection(QStringLiteral("providers"), QStringLiteral("selection"), nullptr));
    std::unique_ptr<QWidget> limits(plugin.createSettingsSection(QStringLiteral("providers"), QStringLiteral("rate-limits"), nullptr));
    ASSERT_NE(selection, nullptr);
    ASSERT_NE(limits, nullptr);
    selection->show();
    limits->show();

    auto* provider = selection->findChild<QComboBox*>(QStringLiteral("aiScopeProvider"));
    auto* interval = limits->findChild<QSpinBox*>(QStringLiteral("aiRateLimitInterval"));
    ASSERT_NE(provider, nullptr);
    ASSERT_NE(interval, nullptr);
    ASSERT_GT(provider->count(), 1);

    // The limit is written against the provider the selector carries, so a value never lands on the one nobody chose.
    const QString first = provider->currentData().toString();
    interval->setValue(750);
    // clang-format off
    ASSERT_TRUE(test::waitUntil([&plugin, &first]() { const auto stored = plugin.rateLimits(); return stored.size() == 1 && stored.first().providerId == first && stored.first().minimumIntervalMs == 750; }));
    // clang-format on

    // Selecting another provider reloads every section of the group, so the fields present what that provider was given.
    provider->setCurrentIndex(provider->currentIndex() == 0 ? 1 : 0);
    const QString second = provider->currentData().toString();
    EXPECT_NE(second, first);
    EXPECT_EQ(interval->value(), 0);

    interval->setValue(120);
    // clang-format off
    ASSERT_TRUE(test::waitUntil([&plugin]() { return plugin.rateLimits().size() == 2; }));
    // clang-format on

    provider->setCurrentIndex(provider->findData(first));
    EXPECT_EQ(interval->value(), 750);
}

TEST(AiConnectionSettingsViewTest, OpensTheEditorOfTheRowThatWasDoubleClicked) {
    test::TestPluginHost host;
    host.translations = translations::english();
    AiTestsHelper::installEmptyProviderRows(host);
    host.settingsDocument = AiTestsHelper::settingsDocument({AiTestsHelper::testConnection()}, connectionKey(AiTestsHelper::testConnection()));

    AiPlugin plugin;
    ASSERT_TRUE(plugin.initialize(host).hasValue());
    std::unique_ptr<QWidget> section(plugin.createSettingsSection(QStringLiteral("connections"), QStringLiteral("general"), nullptr));
    ASSERT_NE(section, nullptr);
    section->resize(760, 520);
    section->show();

    auto* grid = section->findChild<QTableWidget*>(QStringLiteral("aiConnectionGrid"));
    ASSERT_NE(grid, nullptr);
    ASSERT_EQ(grid->rowCount(), 1);

    // The editor is modal, so it is dismissed as soon as it appears and what matters is that the row opened it.
    bool opened = false;
    // clang-format off
    QTimer::singleShot(0, section.get(), [&section, &opened]() { if (auto* dialog = section->findChild<AiConnectionDialog*>(); dialog != nullptr) { opened = true; dialog->reject(); } });
    // clang-format on
    grid->selectRow(0);
    emit grid->doubleClicked(grid->model()->index(0, 0));

    EXPECT_TRUE(opened);
    EXPECT_EQ(plugin.connections().size(), 1);
    section.reset();
    plugin.shutdown();
}

TEST(AiConnectionSettingsViewTest, RemovesAConnectionThroughTheConfirmationAndKeepsTheDefaultConfigured) {
    test::TestPluginHost host;
    host.translations = translations::english();
    ModelConnection second = AiTestsHelper::testConnection();
    second.modelId = QStringLiteral("gpt-4o-mini");
    second.displayName = QStringLiteral("Cheap reviewer");
    second.parameters = defaultParameters(*findProvider(QStringLiteral("openai")), second.modelId);
    AiTestsHelper::installEmptyProviderRows(host);
    host.settingsDocument = AiTestsHelper::settingsDocument({AiTestsHelper::testConnection(), second}, connectionKey(AiTestsHelper::testConnection()), {});

    AiPlugin plugin;
    ASSERT_TRUE(plugin.initialize(host).hasValue());
    ASSERT_EQ(plugin.connections().size(), 2);

    std::unique_ptr<QWidget> section(plugin.createSettingsSection(QStringLiteral("connections"), QStringLiteral("general"), nullptr));
    ASSERT_NE(section, nullptr);
    section->resize(760, 520);
    section->show();

    auto* grid = section->findChild<QTableWidget*>(QStringLiteral("aiConnectionGrid"));
    auto* empty = section->findChild<QLabel*>(QStringLiteral("aiConnectionEmpty"));
    auto* defaultConnection = section->findChild<QComboBox*>(QStringLiteral("aiDefaultConnection"));
    auto* remove = section->findChild<QToolButton*>(QStringLiteral("aiConnectionRemove"));
    ASSERT_NE(grid, nullptr);
    ASSERT_NE(empty, nullptr);
    ASSERT_NE(defaultConnection, nullptr);
    ASSERT_NE(remove, nullptr);
    EXPECT_TRUE(grid->isVisible());
    EXPECT_FALSE(empty->isVisible());
    EXPECT_EQ(grid->rowCount(), 2);
    EXPECT_EQ(grid->item(0, 0)->text(), QStringLiteral("openai/gpt-4o"));
    EXPECT_EQ(grid->item(1, 0)->text(), QStringLiteral("Cheap reviewer"));
    EXPECT_EQ(grid->columnCount(), 3);
    EXPECT_EQ(defaultConnection->count(), 2);
    EXPECT_EQ(defaultConnection->currentData().toString(), QStringLiteral("openai/gpt-4o"));

    // A cancelled confirmation removes nothing.
    host.confirmation = false;
    grid->selectRow(0);
    QTest::mouseClick(remove, Qt::LeftButton);
    EXPECT_EQ(plugin.connections().size(), 2);

    // Removing the default connection moves the default to one that is still configured.
    host.confirmation = true;
    grid->selectRow(0);
    QTest::mouseClick(remove, Qt::LeftButton);
    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { return plugin.connections().size() == 1; }));
    // clang-format on
    EXPECT_EQ(plugin.defaultConnectionKey(), connectionKey(second));
    EXPECT_EQ(grid->rowCount(), 1);

    grid->selectRow(0);
    QTest::mouseClick(remove, Qt::LeftButton);
    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { return plugin.connections().isEmpty(); }));
    // clang-format on
    EXPECT_TRUE(plugin.defaultConnectionKey().isEmpty());
    EXPECT_FALSE(grid->isVisible());
    EXPECT_TRUE(empty->isVisible());
    EXPECT_FALSE(defaultConnection->isEnabled());

    section.reset();
    plugin.shutdown();
}

TEST(AiPluginTest, DispatchesTheRestoredQueueAsSoonAsItLoads) {
    test::TestPluginHost host;
    host.translations = translations::english();
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const AiWorkspace workspace{QStringLiteral("workspace-1"), QStringLiteral("Product"), 0, true, now, now};
    AiTask queued = AiTestsHelper::makeTask(QStringLiteral("task-1"), workspace.id);
    queued.column = TaskColumn::Doing;
    AiTestsHelper::installAiRows(host, {workspace}, {queued}, {queued.id});

    QVector<FakeChatClient*> clients;
    // clang-format off
    AiPlugin plugin([&clients](AiRequestGate&) { auto created = std::make_unique<FakeChatClient>(); clients.append(created.get()); return created; });
    // clang-format on
    ASSERT_TRUE(plugin.initialize(host).hasValue());

    // What was waiting when the application closed reaches the model again without the user touching anything.
    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { return clients.size() == 1; }));
    // clang-format on
    EXPECT_EQ(plugin.runState(queued.id), TaskRunState::Running);
    clients.first()->deliver(QStringLiteral("done"), {1, 2}, QStringLiteral("stop"));
    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { return plugin.runState(queued.id) == TaskRunState::Idle; }));
    // clang-format on
    plugin.shutdown();
}

TEST(AiPluginTest, WritesTheFileTheAgentAsksForAndFinishesOnTheAnswerThatFollows) {
    test::TestPluginHost host;
    host.translations = translations::english();
    filesystem::FileSystemService files;
    host.useFileSystem(files);
    QTemporaryDir workdir;
    ASSERT_TRUE(workdir.isValid());
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const AiWorkspace workspace{QStringLiteral("workspace-1"), QStringLiteral("Product"), 0, true, now, now};
    AiTask task = AiTestsHelper::makeTask(QStringLiteral("task-1"), workspace.id);
    task.workdir = workdir.path();
    AiTestsHelper::installAiRows(host, {workspace}, {task}, {});

    QVector<FakeChatClient*> clients;
    // clang-format off
    AiPlugin plugin([&clients](AiRequestGate&) { auto created = std::make_unique<FakeChatClient>(); clients.append(created.get()); return created; });
    // clang-format on
    ASSERT_TRUE(plugin.initialize(host).hasValue());
    ASSERT_TRUE(test::awaitFuture(plugin.startTask(task.id)).hasValue());
    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { return clients.size() == 1; }));
    // clang-format on

    // The whole point of the agent is that a file it asks for exists afterwards, written where it asked for it.
    const QString page = QDir(QDir(workdir.path()).canonicalPath()).filePath(QStringLiteral("site/index.html"));
    FakeChatClient* agent = clients.first();
    agent->deliverToolCalls({{QStringLiteral("c1"), QStringLiteral("write_file"), QJsonObject{{QStringLiteral("path"), page}, {QStringLiteral("content"), QStringLiteral("<html>landing</html>")}}}});
    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { return agent->sendCalls == 2; }));
    // clang-format on
    ASSERT_TRUE(QFileInfo(page).isFile());
    QFile written(page);
    ASSERT_TRUE(written.open(QIODevice::ReadOnly));
    EXPECT_EQ(written.readAll(), QByteArrayLiteral("<html>landing</html>"));

    // The turn carries the result back, so the model sees the path it wrote instead of a failure.
    const QJsonArray sent = agent->sentMessages;
    ASSERT_GE(sent.size(), 4);
    EXPECT_TRUE(QJsonDocument(sent).toJson(QJsonDocument::Compact).contains(page.toUtf8()));

    agent->deliver(QStringLiteral("The landing page is written"), {10, 20}, QStringLiteral("stop"));
    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { return plugin.runState(task.id) == TaskRunState::Idle; }));
    // clang-format on
    EXPECT_EQ(plugin.lastExecutionStatus(task.id), ExecutionStatus::Succeeded);
    EXPECT_TRUE(plugin.lastError(task.id).isEmpty());
    plugin.shutdown();
}

TEST(AiPluginTest, SaysWhichToolTheRunIsCallingWhileItIsCallingIt) {
    test::TestPluginHost host;
    host.translations = translations::english();
    filesystem::FileSystemService files;
    host.useFileSystem(files);
    QTemporaryDir workdir;
    ASSERT_TRUE(workdir.isValid());
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const AiWorkspace workspace{QStringLiteral("workspace-1"), QStringLiteral("Product"), 0, true, now, now};
    AiTask task = AiTestsHelper::makeTask(QStringLiteral("task-1"), workspace.id);
    task.workdir = workdir.path();
    AiTestsHelper::installAiRows(host, {workspace}, {task}, {});

    QVector<FakeChatClient*> clients;
    // clang-format off
    AiPlugin plugin([&clients](AiRequestGate&) { auto created = std::make_unique<FakeChatClient>(); clients.append(created.get()); return created; });
    // clang-format on
    ASSERT_TRUE(plugin.initialize(host).hasValue());
    EXPECT_TRUE(plugin.executionDetail(task.id).isEmpty());

    ASSERT_TRUE(test::awaitFuture(plugin.startTask(task.id)).hasValue());
    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { return clients.size() == 1; }));
    // clang-format on

    // A long command keeps the call open, so the card can be read while the tool is still running.
    int detailChanges = 0;
    // clang-format off
    QObject::connect(&plugin, &AiPlugin::taskRunStateChanged, &plugin, [&detailChanges](const QString&) { ++detailChanges; });
    clients.first()->deliverToolCalls({{QStringLiteral("c1"), QStringLiteral("run_command"), QJsonObject{{QStringLiteral("command"), AiTestsHelper::sleepingCommand(2)}}}});
    ASSERT_TRUE(test::waitUntil([&]() { return plugin.executionDetail(task.id) == QStringLiteral("run_command"); }));
    // clang-format on
    EXPECT_EQ(plugin.executionPhase(task.id), ExecutionPhase::CallingTool);
    EXPECT_GT(detailChanges, 0);

    ASSERT_TRUE(test::awaitFuture(plugin.stopTask(task.id)).hasValue());
    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { return plugin.runState(task.id) == TaskRunState::Idle; }));
    // clang-format on
    EXPECT_TRUE(plugin.executionDetail(task.id).isEmpty());
    plugin.shutdown();
}

TEST(AiPluginTest, KeepsWhatArrivedWhenTheProviderCutTheAnswerAndNamesTheBudgetAsTheReason) {
    test::TestPluginHost host;
    host.translations = translations::english();
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const AiWorkspace workspace{QStringLiteral("workspace-1"), QStringLiteral("Product"), 0, true, now, now};
    const AiTask task = AiTestsHelper::makeTask(QStringLiteral("task-1"), workspace.id);
    AiTestsHelper::installAiRows(host, {workspace}, {task}, {});

    QVector<FakeChatClient*> clients;
    // clang-format off
    AiPlugin plugin([&clients](AiRequestGate&) { auto created = std::make_unique<FakeChatClient>(); clients.append(created.get()); return created; });
    // clang-format on
    ASSERT_TRUE(plugin.initialize(host).hasValue());
    ASSERT_TRUE(test::awaitFuture(plugin.startTask(task.id)).hasValue());
    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { return clients.size() == 1; }));
    // clang-format on

    // The model was cut before it finished, so what arrived is kept and the reason says the budget ended the run.
    const qint64 budget = outputBudget(AiTestsHelper::testConnection());
    clients.first()->deliver(QStringLiteral("I will now write the landing page"), {1972, budget}, QStringLiteral("length"));
    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { return plugin.runState(task.id) == TaskRunState::Idle; }));
    // clang-format on
    EXPECT_EQ(plugin.lastExecutionStatus(task.id), ExecutionStatus::Succeeded);
    EXPECT_EQ(plugin.lastStopReason(task.id), AgentStopReason::OutputBudget);
    EXPECT_TRUE(plugin.lastError(task.id).isEmpty());
    ASSERT_FALSE(plugin.conversation(task.id).isEmpty());
    EXPECT_EQ(plugin.conversation(task.id).last().content, QStringLiteral("I will now write the landing page"));
    EXPECT_TRUE(truncatedByOutputBudget(QStringLiteral("max_tokens")));
    EXPECT_FALSE(truncatedByOutputBudget(QStringLiteral("stop")));
    EXPECT_FALSE(truncatedByOutputBudget(QStringLiteral("end_turn")));
    plugin.shutdown();
}

TEST(AiAgentSettingsViewTest, StartsFromTheEmptyStateAndOffersTheTemplateAndTheTags) {
    test::TestPluginHost host;
    host.translations = translations::english();
    AiTestsHelper::installEmptyProviderRows(host);
    host.settingsDocument = AiTestsHelper::settingsDocument({AiTestsHelper::testConnection()}, connectionKey(AiTestsHelper::testConnection()), {});

    AiPlugin plugin;
    ASSERT_TRUE(plugin.initialize(host).hasValue());

    std::unique_ptr<QWidget> section(plugin.createSettingsSection(QStringLiteral("agents"), QStringLiteral("general"), nullptr));
    ASSERT_NE(section, nullptr);
    section->resize(760, 520);
    section->show();

    auto* grid = section->findChild<QTableWidget*>(QStringLiteral("aiAgentGrid"));
    auto* empty = section->findChild<QLabel*>(QStringLiteral("aiAgentEmpty"));
    ASSERT_NE(grid, nullptr);
    ASSERT_NE(empty, nullptr);
    EXPECT_FALSE(grid->isVisible());
    EXPECT_TRUE(empty->isVisible());

    AiAgentDialog dialog(host, {}, {}, plugin.connections(), nullptr);
    dialog.show();
    auto* prompt = dialog.findChild<QPlainTextEdit*>(QStringLiteral("aiAgentSystemPrompt"));
    auto* insertTemplate = dialog.findChild<QPushButton*>(QStringLiteral("aiAgentInsertTemplate"));
    auto* showTags = dialog.findChild<QPushButton*>(QStringLiteral("aiAgentShowTags"));
    ASSERT_NE(prompt, nullptr);
    ASSERT_NE(insertTemplate, nullptr);
    ASSERT_NE(showTags, nullptr);
    EXPECT_TRUE(prompt->toPlainText().isEmpty());

    // The template arrives with the marks the writer replaces and the tags already in place.
    QTest::mouseClick(insertTemplate, Qt::LeftButton);
    EXPECT_TRUE(prompt->toPlainText().contains(QStringLiteral("[PUT")));
    EXPECT_TRUE(prompt->toPlainText().contains(QStringLiteral("{{SYSTEM_PROMPT_DATA}}")));
    EXPECT_TRUE(unknownPromptTags(prompt->toPlainText()).isEmpty());

    // The list of tags is shown where the prompt is written, without holding the loop for an answer nobody reads.
    QTest::mouseClick(showTags, Qt::LeftButton);
    auto* tags = dialog.findChild<QDialog*>(QStringLiteral("aiAgentTagsDialog"));
    ASSERT_NE(tags, nullptr);
    auto* tagsContent = tags->findChild<QTextBrowser*>(QStringLiteral("aiAgentTagsContent"));
    ASSERT_NE(tagsContent, nullptr);
    EXPECT_TRUE(tagsContent->toPlainText().contains(QStringLiteral("{{SYSTEM_PROMPT_DATA}}")));
    QPointer<QDialog> closing = tags;
    tags->reject();
    // clang-format off
    ASSERT_TRUE(test::waitUntil([&closing]() { return closing.isNull(); }));
    // clang-format on

    // The identifier is spelled from the name while nobody has typed one, and stops following once somebody does.
    auto* identifier = dialog.findChild<QLineEdit*>(QStringLiteral("aiAgentIdentifier"));
    auto* name = dialog.findChild<QLineEdit*>(QStringLiteral("aiAgentName"));
    ASSERT_NE(identifier, nullptr);
    ASSERT_NE(name, nullptr);
    EXPECT_LT(name->mapTo(&dialog, QPoint(0, 0)).y(), identifier->mapTo(&dialog, QPoint(0, 0)).y());
    QTest::keyClicks(name, QStringLiteral("Claudinho Review Bot"));
    EXPECT_EQ(identifier->text(), QStringLiteral("claudinho-review-bot"));
    QTest::keyClicks(identifier, QStringLiteral("x"));
    QTest::keyClicks(name, QStringLiteral(" 2"));
    EXPECT_EQ(identifier->text(), QStringLiteral("claudinho-review-botx"));
    identifier->selectAll();
    QTest::keyClick(identifier, Qt::Key_Backspace);
    name->clear();
    QTest::keyClicks(name, QStringLiteral("9 Reviewer!"));
    EXPECT_EQ(identifier->text(), QStringLiteral("reviewer"));

    // A prompt carrying a tag nobody declares refuses to be saved and says which one.
    auto* validation = dialog.findChild<QLabel*>(QStringLiteral("aiTaskValidation"));
    ASSERT_NE(identifier, nullptr);
    ASSERT_NE(name, nullptr);
    ASSERT_NE(validation, nullptr);
    identifier->setText(QStringLiteral("reviewer"));
    name->setText(QStringLiteral("Reviewer"));
    prompt->setPlainText(QStringLiteral("You are {{NOT_A_TAG}}"));
    QTest::mouseClick(dialog.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Save), Qt::LeftButton);
    EXPECT_NE(dialog.result(), static_cast<int>(QDialog::Accepted));
    EXPECT_TRUE(validation->text().contains(QStringLiteral("NOT_A_TAG")));

    prompt->setPlainText(QStringLiteral("You are {{AGENT_NAME}}"));
    QTest::mouseClick(dialog.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Save), Qt::LeftButton);
    ASSERT_EQ(dialog.result(), static_cast<int>(QDialog::Accepted));
    EXPECT_EQ(dialog.agent().id, QStringLiteral("reviewer"));
    EXPECT_EQ(dialog.agent().connectionKey, connectionKey(AiTestsHelper::testConnection()));
    plugin.shutdown();
}

TEST(AiPluginTest, RefusesAConnectionAnAgentRunsOnAndStopsTheTasksOfAnAgentThatIsRemoved) {
    test::TestPluginHost host;
    host.translations = translations::english();
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const AiWorkspace workspace{QStringLiteral("workspace-1"), QStringLiteral("Product"), 0, true, now, now};
    const AiTask task = AiTestsHelper::makeTask(QStringLiteral("task-1"), workspace.id);
    AiTestsHelper::installAiRows(host, {workspace}, {task}, {});

    AiPlugin plugin;
    ASSERT_TRUE(plugin.initialize(host).hasValue());
    ASSERT_EQ(plugin.agents().size(), 1);

    // A connection an agent runs on is refused while that agent exists, and the alert names the agent that holds it.
    const auto removedConnection = test::awaitFuture(plugin.saveConnections({}, {}));
    EXPECT_EQ(removedConnection.error().code, QStringLiteral("ai_connection_in_use"));
    EXPECT_EQ(removedConnection.error().detail, AiTestsHelper::testAgent().name);
    EXPECT_EQ(plugin.connections().size(), 1);
    EXPECT_TRUE(host.translate(QStringLiteral("ai.error.connection-in-use")).arg(removedConnection.error().detail).contains(AiTestsHelper::testAgent().name));

    // An agent naming a connection nobody configured is refused where it is saved.
    AiAgent orphan = AiTestsHelper::testAgent();
    orphan.connectionKey = QStringLiteral("openai/retired-model");
    EXPECT_EQ(test::awaitFuture(plugin.saveAgents({orphan})).error().code, QStringLiteral("ai_connection_unknown"));

    // Removing the agent is allowed, and the task it was handed stops with the reason instead of waiting for nobody.
    ASSERT_TRUE(test::awaitFuture(plugin.saveAgents({})).hasValue());
    EXPECT_TRUE(plugin.agents().isEmpty());
    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { return plugin.lastError(task.id).contains(AiTestsHelper::testAgent().id); }));
    // clang-format on
    plugin.shutdown();
}

TEST(AiPluginTest, RefusesToRunATaskWhoseAgentIsNoLongerConfigured) {
    test::TestPluginHost host;
    host.translations = translations::english();
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const AiWorkspace workspace{QStringLiteral("workspace-1"), QStringLiteral("Product"), 0, true, now, now};
    AiTask orphan = AiTestsHelper::makeTask(QStringLiteral("task-1"), workspace.id);
    orphan.agentId = QStringLiteral("retired-agent");
    AiTestsHelper::installAiRows(host, {workspace}, {orphan}, {});

    QVector<FakeChatClient*> clients;
    // clang-format off
    AiPlugin plugin([&clients](AiRequestGate&) { auto created = std::make_unique<FakeChatClient>(); clients.append(created.get()); return created; });
    // clang-format on
    ASSERT_TRUE(plugin.initialize(host).hasValue());

    // The task loaded, because an agent removed later must not stop the application from opening.
    ASSERT_EQ(plugin.tasks().size(), 1);
    const auto resolved = plugin.agentForTask(plugin.tasks().first());
    EXPECT_EQ(resolved.error().code, QStringLiteral("ai_agent_unknown"));

    const auto started = test::awaitFuture(plugin.startTask(orphan.id));
    EXPECT_EQ(started.error().code, QStringLiteral("ai_agent_unknown"));
    EXPECT_TRUE(clients.isEmpty());
    EXPECT_EQ(plugin.runState(orphan.id), TaskRunState::Idle);
    plugin.shutdown();
}

TEST(AiPluginTest, FailsAQueuedTaskWhoseConnectionIsGoneInsteadOfLeavingItWaitingForever) {
    test::TestPluginHost host;
    host.translations = translations::english();
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const AiWorkspace workspace{QStringLiteral("workspace-1"), QStringLiteral("Product"), 0, true, now, now};
    AiTask orphan = AiTestsHelper::makeTask(QStringLiteral("task-1"), workspace.id);
    orphan.agentId = QStringLiteral("retired-agent");
    orphan.column = TaskColumn::Doing;
    AiTestsHelper::installAiRows(host, {workspace}, {orphan}, {orphan.id});

    QVector<FakeChatClient*> clients;
    // clang-format off
    AiPlugin plugin([&clients](AiRequestGate&) { auto created = std::make_unique<FakeChatClient>(); clients.append(created.get()); return created; });
    // clang-format on
    ASSERT_TRUE(plugin.initialize(host).hasValue());

    // The restored queue dispatches it, the run fails with the reason and the card returns to To Do.
    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { return plugin.hasLastExecution(orphan.id); }));
    // clang-format on
    EXPECT_EQ(plugin.lastExecutionStatus(orphan.id), ExecutionStatus::Failed);
    EXPECT_FALSE(plugin.lastError(orphan.id).isEmpty());
    EXPECT_TRUE(clients.isEmpty());
    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { return plugin.runState(orphan.id) == TaskRunState::Idle; }));
    // clang-format on
    plugin.shutdown();
}

TEST(AiConnectionDialogTest, KeepsTheReplacedParameterEditorsAliveUntilTheClickThatCausedItIsOver) {
    test::TestPluginHost host;
    host.translations = translations::english();

    AiConnectionDialog dialog(host, AiTestsHelper::testConnection(), {}, nullptr);
    dialog.show();
    auto* model = dialog.findChild<QComboBox*>(QStringLiteral("aiConnectionModel"));
    ASSERT_NE(model, nullptr);

    // The rebuild is reached from the editing that ends when the user clicks the very editor it replaces.
    const QPointer<QWidget> replaced = dialog.findChild<QWidget*>(QStringLiteral("aiParameter.temperature"));
    ASSERT_FALSE(replaced.isNull());
    model->setCurrentText(QStringLiteral("o3-mini"));
    emit model->lineEdit()->editingFinished();

    EXPECT_FALSE(replaced.isNull());
    EXPECT_FALSE(replaced->isVisible());
    EXPECT_NE(dialog.findChild<QWidget*>(QStringLiteral("aiParameter.reasoningEffort")), nullptr);

    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    EXPECT_TRUE(replaced.isNull());
}

TEST(AiConnectionDialogTest, ExplainsWhyAConnectionCannotBeSaved) {
    test::TestPluginHost host;
    host.translations = translations::english();

    ModelConnection stored = AiTestsHelper::testConnection();
    stored.displayName = QStringLiteral("Reviewer");
    stored.extraParameters = QJsonObject{{QStringLiteral("seed"), 7}};
    AiConnectionDialog dialog(host, stored, {QStringLiteral("anthropic/claude-opus-5")}, nullptr);
    dialog.show();

    // Opening an existing connection presents what it stored rather than what its provider declares.
    EXPECT_EQ(dialog.connection().apiKey, QStringLiteral("sk-test"));
    EXPECT_EQ(dialog.connection().displayName, QStringLiteral("Reviewer"));
    EXPECT_EQ(dialog.connection().modelId, QStringLiteral("gpt-4o"));
    EXPECT_EQ(dialog.connection().extraParameters.value(QStringLiteral("seed")).toInt(), 7);

    auto* validation = dialog.findChild<QLabel*>(QStringLiteral("aiTaskValidation"));
    auto* extraValidation = dialog.findChild<QLabel*>(QStringLiteral("aiConnectionExtraValidation"));
    auto* extra = dialog.findChild<QPlainTextEdit*>(QStringLiteral("aiConnectionExtraParameters"));
    auto* model = dialog.findChild<QComboBox*>(QStringLiteral("aiConnectionModel"));
    auto* apiKey = dialog.findChild<ui::SecretField*>(QStringLiteral("aiConnectionApiKey"));
    auto* save = dialog.findChild<QPushButton*>(QStringLiteral("primaryButton"));
    ASSERT_NE(validation, nullptr);
    ASSERT_NE(extraValidation, nullptr);
    ASSERT_NE(extra, nullptr);
    ASSERT_NE(model, nullptr);
    ASSERT_NE(apiKey, nullptr);
    ASSERT_NE(save, nullptr);

    // A provider without its credential says so instead of leaving the dialog silently unsaved.
    dialog.resize(dialog.sizeHint());
    // clang-format off
    ASSERT_TRUE(test::waitUntil([extra]() { return extra->height() > 0; }));
    // clang-format on
    const int roomBefore = extra->height();
    const int dialogBefore = dialog.height();
    apiKey->setValue(QString{});
    dialog.accept();
    EXPECT_TRUE(dialog.isVisible());
    EXPECT_TRUE(validation->isVisible());

    // The dialog grows to carry the message, so the field the message appeared under keeps its room.
    // clang-format off
    ASSERT_TRUE(test::waitUntil([&dialog, dialogBefore]() { return dialog.height() > dialogBefore; }));
    // clang-format on
    EXPECT_GE(extra->height(), roomBefore);
    EXPECT_EQ(validation->text(), QStringLiteral("Enter the API key this provider requires"));

    apiKey->setValue(QStringLiteral("sk-test"));
    model->setCurrentText(QString{});
    dialog.accept();
    EXPECT_TRUE(dialog.isVisible());
    EXPECT_EQ(validation->text(), QStringLiteral("Choose the model this provider should run"));

    // A document that cannot be sent is reported while it is typed and blocks the confirm action.
    model->setCurrentText(QStringLiteral("gpt-4o"));
    emit model->lineEdit()->editingFinished();
    extra->setPlainText(QStringLiteral("{\"seed\": }"));
    EXPECT_TRUE(extraValidation->isVisible());
    EXPECT_FALSE(save->isEnabled());

    extra->setPlainText(QStringLiteral("[1, 2]"));
    EXPECT_TRUE(extraValidation->isVisible());
    EXPECT_EQ(extraValidation->text(), QStringLiteral("The extra parameters must be a JSON object"));
    EXPECT_FALSE(save->isEnabled());

    extra->setPlainText(QStringLiteral("{\"seed\": 42}"));
    EXPECT_FALSE(extraValidation->isVisible());
    EXPECT_TRUE(save->isEnabled());
    EXPECT_EQ(dialog.connection().extraParameters.value(QStringLiteral("seed")).toInt(), 42);

    // A pair another connection already configures is refused, because one key names one configuration.
    auto* provider = dialog.findChild<QComboBox*>(QStringLiteral("aiConnectionProvider"));
    ASSERT_NE(provider, nullptr);
    provider->setCurrentIndex(provider->findData(QStringLiteral("anthropic")));
    model->setCurrentText(QStringLiteral("claude-opus-5"));
    emit model->lineEdit()->editingFinished();
    dialog.accept();
    EXPECT_TRUE(dialog.isVisible());
    EXPECT_EQ(validation->text(), QStringLiteral("This provider and model pair is already configured"));
}

TEST(AiPluginTest, RecordsEverySentAndReceivedExchangeNewestFirst) {
    test::TestPluginHost host;
    host.translations = translations::english();
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const AiWorkspace workspace{QStringLiteral("workspace-1"), QStringLiteral("Product"), 0, true, now, now};
    const AiTask task = AiTestsHelper::makeTask(QStringLiteral("task-1"), workspace.id);
    AiTestsHelper::installAiRows(host, {workspace}, {task}, {});

    FakeChatClient* client = nullptr;
    // clang-format off
    AiPlugin plugin([&client](AiRequestGate&) { auto created = std::make_unique<FakeChatClient>(); client = created.get(); return created; });
    // clang-format on
    ASSERT_TRUE(plugin.initialize(host).hasValue());
    EXPECT_EQ(plugin.executionPhase(task.id), ExecutionPhase::Idle);

    ASSERT_TRUE(test::awaitFuture(plugin.startTask(task.id)).hasValue());
    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { return client != nullptr && client->sendCalls == 1; }));
    // clang-format on
    EXPECT_EQ(plugin.executionPhase(task.id), ExecutionPhase::Sending);

    emit client->requestSent(QStringLiteral("https://api.openai.com/v1/chat/completions"), QStringLiteral("{\"model\":\"gpt-4o\"}"));
    client->deliver(QStringLiteral("answer"), {15, 390}, QStringLiteral("end_turn"));
    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { return plugin.runState(task.id) == TaskRunState::Idle; }));
    // clang-format on
    EXPECT_EQ(plugin.executionPhase(task.id), ExecutionPhase::Idle);

    QVector<ExecutionLogEntry> written;

    for (const auto& recorded : host.databaseExecutions) {
        if (!recorded.value(QStringLiteral("statement")).toString().contains(QStringLiteral("INSERT INTO ai_tasks_logs"))) {
            continue;
        }
        const QVariantList bindings = recorded.value(QStringLiteral("bindings")).toList();
        ExecutionLogEntry entry;
        entry.sequence = bindings.at(2).toLongLong();
        entry.kind = AiTaskRepository::parseExecutionLogKind(bindings.at(5).toString()).value();
        entry.detail = bindings.at(6).toString();
        written.append(entry);
    }

    QVector<ExecutionLogKind> kinds;

    for (const auto& entry : written) {
        kinds.append(entry.kind);
    }

    EXPECT_TRUE(kinds.contains(ExecutionLogKind::Started));
    EXPECT_TRUE(kinds.contains(ExecutionLogKind::RequestSent));
    EXPECT_TRUE(kinds.contains(ExecutionLogKind::FirstTokenReceived));
    EXPECT_TRUE(kinds.contains(ExecutionLogKind::ResponseReceived));
    EXPECT_TRUE(kinds.contains(ExecutionLogKind::UsageReported));
    EXPECT_TRUE(kinds.contains(ExecutionLogKind::Succeeded));

    // Everything sent and everything received is recorded verbatim next to its translatable event.
    for (const auto& entry : written) {
        if (entry.kind == ExecutionLogKind::RequestSent) {
            EXPECT_TRUE(entry.detail.contains(QStringLiteral("chat/completions")));
            EXPECT_TRUE(entry.detail.contains(QStringLiteral("gpt-4o")));
        }
        if (entry.kind == ExecutionLogKind::ResponseReceived) {
            EXPECT_EQ(entry.detail, QStringLiteral("answer"));
        }
        if (entry.kind == ExecutionLogKind::UsageReported) {
            EXPECT_TRUE(entry.detail.contains(QStringLiteral("15")));
            EXPECT_TRUE(entry.detail.contains(QStringLiteral("end_turn")));
        }
    }

    // The sequence is monotonic so the reader can present the newest entry first.
    for (qsizetype index = 1; index < written.size(); ++index) {
        EXPECT_GT(written.at(index).sequence, written.at(index - 1).sequence);
    }

    plugin.shutdown();
}

TEST(AiChatClientTest, ParsesTheRecordedAnthropicMessageStream) {
    // Recorded from the published Anthropic streaming example.
    const QByteArray stream = QByteArrayLiteral("event: message_start\n"
                                                "data: {\"type\":\"message_start\",\"message\":{\"id\":\"msg_1nZ\",\"type\":\"message\",\"role\":\"assistant\",\"content\":[],\"model\":\"claude-opus-5\",\"stop_reason\":null,\"stop_sequence\":null,\"usage\":{\"input_tokens\":25,\"output_tokens\":1}}}\n\n"
                                                "event: content_block_start\n"
                                                "data: {\"type\":\"content_block_start\",\"index\":0,\"content_block\":{\"type\":\"text\",\"text\":\"\"}}\n\n"
                                                "event: ping\n"
                                                "data: {\"type\": \"ping\"}\n\n"
                                                "event: content_block_delta\n"
                                                "data: {\"type\":\"content_block_delta\",\"index\":0,\"delta\":{\"type\":\"text_delta\",\"text\":\"Hello\"}}\n\n"
                                                "event: content_block_delta\n"
                                                "data: {\"type\":\"content_block_delta\",\"index\":0,\"delta\":{\"type\":\"text_delta\",\"text\":\"!\"}}\n\n"
                                                "event: content_block_stop\n"
                                                "data: {\"type\":\"content_block_stop\",\"index\":0}\n\n"
                                                "event: message_delta\n"
                                                "data: {\"type\":\"message_delta\",\"delta\":{\"stop_reason\":\"end_turn\",\"stop_sequence\":null},\"usage\":{\"output_tokens\":15}}\n\n"
                                                "event: message_stop\n"
                                                "data: {\"type\":\"message_stop\"}\n\n");

    RecordedStreamServer server(stream);
    ASSERT_TRUE(server.listen());

    const ProviderDescriptor* anthropic = findProvider(QStringLiteral("anthropic"));
    ASSERT_NE(anthropic, nullptr);
    const QString model = QStringLiteral("claude-3-haiku-20240307");
    const ModelConnection connection{anthropic->id, model, {}, QStringLiteral("sk-test"), {}, defaultParameters(*anthropic, model), {}};

    AiRequestGate clientGate;
    AiHttpChatClient client(clientGate);
    QStringList deltas;
    QString content;
    ChatUsage usage;
    QString finishReason;
    bool completed = false;
    // clang-format off
    QObject::connect(&client, &AiChatClient::contentReceived, &client, [&deltas](const QString& delta) { deltas.append(delta); });
    QObject::connect(&client, &AiChatClient::finished, &client, [&](const QString& text, const QVector<ToolCall>&, ChatUsage reported, const QString& reason) { content = text; usage = reported; finishReason = reason; completed = true; });
    // clang-format on
    // clang-format off
    client.send({connection, server.address(), QJsonArray{QJsonObject{{QStringLiteral("role"), QStringLiteral("user")}, {QStringLiteral("content"), QStringLiteral("hello")}}}, {}}, [](const QString& key) { return key; });
    // clang-format on

    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { return completed; }));
    // clang-format on
    EXPECT_EQ(content, QStringLiteral("Hello!"));
    EXPECT_EQ(deltas, QStringList({QStringLiteral("Hello"), QStringLiteral("!")}));
    EXPECT_EQ(usage.inputTokens, 25);
    EXPECT_EQ(usage.outputTokens, 15);
    EXPECT_EQ(finishReason, QStringLiteral("end_turn"));

    const QJsonObject sent = QJsonDocument::fromJson(server.requestBody()).object();
    EXPECT_EQ(sent.value(QStringLiteral("model")).toString(), model);
    EXPECT_TRUE(sent.value(QStringLiteral("stream")).toBool());
}

TEST(AiChatClientTest, ParsesTheRecordedOpenAiChatCompletionStream) {
    // Recorded from the published OpenAI chat completion chunk format.
    const QByteArray stream = QByteArrayLiteral("data: {\"id\":\"chatcmpl-123\",\"object\":\"chat.completion.chunk\",\"created\":1694268190,\"model\":\"gpt-4o\",\"choices\":[{\"index\":0,\"delta\":{\"role\":\"assistant\",\"content\":\"\"},\"finish_reason\":null}]}\n\n"
                                                "data: {\"id\":\"chatcmpl-123\",\"object\":\"chat.completion.chunk\",\"created\":1694268190,\"model\":\"gpt-4o\",\"choices\":[{\"index\":0,\"delta\":{\"content\":\"Hello\"},\"finish_reason\":null}]}\n\n"
                                                "data: {\"id\":\"chatcmpl-123\",\"object\":\"chat.completion.chunk\",\"created\":1694268190,\"model\":\"gpt-4o\",\"choices\":[{\"index\":0,\"delta\":{\"content\":\" world\"},\"finish_reason\":null}]}\n\n"
                                                "data: {\"id\":\"chatcmpl-123\",\"object\":\"chat.completion.chunk\",\"created\":1694268190,\"model\":\"gpt-4o\",\"choices\":[{\"index\":0,\"delta\":{},\"finish_reason\":\"stop\"}]}\n\n"
                                                "data: {\"id\":\"chatcmpl-123\",\"object\":\"chat.completion.chunk\",\"created\":1694268190,\"model\":\"gpt-4o\",\"choices\":[],\"usage\":{\"prompt_tokens\":9,\"completion_tokens\":12,\"total_tokens\":21}}\n\n"
                                                "data: [DONE]\n\n");

    RecordedStreamServer server(stream);
    ASSERT_TRUE(server.listen());

    const ProviderDescriptor* openai = findProvider(QStringLiteral("openai"));
    ASSERT_NE(openai, nullptr);
    const QString model = QStringLiteral("gpt-4o");
    const ModelConnection connection{openai->id, model, {}, QStringLiteral("sk-test"), {}, defaultParameters(*openai, model), {}};

    AiRequestGate clientGate;
    AiHttpChatClient client(clientGate);
    QString content;
    ChatUsage usage;
    QString finishReason;
    bool completed = false;
    // clang-format off
    QObject::connect(&client, &AiChatClient::finished, &client, [&](const QString& text, const QVector<ToolCall>&, ChatUsage reported, const QString& reason) { content = text; usage = reported; finishReason = reason; completed = true; });
    // clang-format on
    // clang-format off
    client.send({connection, server.address(), QJsonArray{QJsonObject{{QStringLiteral("role"), QStringLiteral("user")}, {QStringLiteral("content"), QStringLiteral("hello")}}}, {}}, [](const QString& key) { return key; });
    // clang-format on

    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { return completed; }));
    // clang-format on
    EXPECT_EQ(content, QStringLiteral("Hello world"));
    EXPECT_EQ(usage.inputTokens, 9);
    EXPECT_EQ(usage.outputTokens, 12);
    EXPECT_EQ(finishReason, QStringLiteral("stop"));
}

TEST(AiToolRegistryTest, NamesEveryCallTheWayAReaderSpellsItAndSaysWhatItIsDoing) {
    test::TestPluginHost host;
    host.translations = translations::english();
    AiToolRegistry registry(host);

    // A declared tool is named by the catalog and says the one thing that explains the call.
    const ToolPresentation search = registry.presentation(QStringLiteral("web_search"), QJsonObject{{QStringLiteral("query"), QStringLiteral("bitcoin price USD current")}, {QStringLiteral("count"), 5}});
    EXPECT_EQ(search.title, QStringLiteral("Web Search"));
    EXPECT_EQ(search.activity, QStringLiteral("Searching for: bitcoin price USD current"));
    EXPECT_FALSE(search.activity.contains(QStringLiteral("count")));

    // A tool that carries nothing worth naming shows its name alone.
    const ToolPresentation skills = registry.presentation(QStringLiteral("list_skills"), {});
    EXPECT_EQ(skills.title, QStringLiteral("List Skills"));
    EXPECT_TRUE(skills.activity.isEmpty());

    // A long argument keeps its beginning and its end, because the end of a path is what names the file.
    const QString deep = QStringLiteral("/Users/paulo/Developer/workspaces/node/tibia-lp2/assets/hero-background.svg");
    const ToolPresentation written = registry.presentation(QStringLiteral("write_file"), QJsonObject{{QStringLiteral("path"), deep}, {QStringLiteral("content"), QStringLiteral("<svg/>")}});
    EXPECT_EQ(written.title, QStringLiteral("Write File"));
    EXPECT_TRUE(written.activity.endsWith(QStringLiteral("hero-background.svg")));
    EXPECT_TRUE(written.activity.contains(QStringLiteral("…")));
    EXPECT_FALSE(written.activity.contains(QStringLiteral("<svg/>")));

    // A tool nobody declared is named the way it was spelled, with the marks that separate its words read as spaces.
    const ToolPresentation published = registry.presentation(QStringLiteral("weather.get_forecast"), {});
    EXPECT_EQ(published.title, QStringLiteral("Get Forecast"));
}

TEST(AiToolContractTest, ValidatesTheDeclaredSchemaOfEveryTool) {
    const ToolSchema valid{QStringLiteral("generate_image"), QStringLiteral("ai.tool.generate-image"), QJsonObject{{QStringLiteral("type"), QStringLiteral("object")}, {QStringLiteral("properties"), QJsonObject{{QStringLiteral("prompt"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}}}}}};
    EXPECT_TRUE(validateToolSchema(valid).hasValue());

    ToolSchema upperCase = valid;
    upperCase.name = QStringLiteral("GenerateImage");
    EXPECT_EQ(validateToolSchema(upperCase).error().code, QStringLiteral("ai_tool_invalid"));

    ToolSchema withoutDescription = valid;
    withoutDescription.descriptionKey.clear();
    EXPECT_EQ(validateToolSchema(withoutDescription).error().code, QStringLiteral("ai_tool_invalid"));

    ToolSchema scalarParameters = valid;
    scalarParameters.parameters = QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}};
    EXPECT_EQ(validateToolSchema(scalarParameters).error().code, QStringLiteral("ai_tool_invalid"));
}

TEST(AiToolContractTest, SerializesToolsTurnsAndResultsForEachWireProtocol) {
    const ToolSchema tool{QStringLiteral("get_weather"), QStringLiteral("ai.tool.get-weather"), QJsonObject{{QStringLiteral("type"), QStringLiteral("object")}, {QStringLiteral("properties"), QJsonObject{{QStringLiteral("location"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}}}}}};
    // clang-format off
    const auto translate = [](const QString& key) { return QStringLiteral("described ") + key; };
    // clang-format on

    const QJsonArray openAiTools = serializeTools(WireProtocol::OpenAiCompatible, {tool}, translate);
    ASSERT_EQ(openAiTools.size(), 1);
    EXPECT_EQ(openAiTools.first().toObject().value(QStringLiteral("type")).toString(), QStringLiteral("function"));
    EXPECT_EQ(openAiTools.first().toObject().value(QStringLiteral("function")).toObject().value(QStringLiteral("name")).toString(), tool.name);
    EXPECT_TRUE(openAiTools.first().toObject().value(QStringLiteral("function")).toObject().contains(QStringLiteral("parameters")));

    const QJsonArray anthropicTools = serializeTools(WireProtocol::Anthropic, {tool}, translate);
    ASSERT_EQ(anthropicTools.size(), 1);
    EXPECT_EQ(anthropicTools.first().toObject().value(QStringLiteral("name")).toString(), tool.name);
    EXPECT_TRUE(anthropicTools.first().toObject().contains(QStringLiteral("input_schema")));

    const ToolCall call{QStringLiteral("toolu_01"), tool.name, QJsonObject{{QStringLiteral("location"), QStringLiteral("San Francisco, CA")}}};
    const QJsonObject openAiTurn = serializeAssistantTurn(WireProtocol::OpenAiCompatible, QStringLiteral("checking"), {call});
    ASSERT_EQ(openAiTurn.value(QStringLiteral("tool_calls")).toArray().size(), 1);
    EXPECT_EQ(openAiTurn.value(QStringLiteral("tool_calls")).toArray().first().toObject().value(QStringLiteral("id")).toString(), call.id);

    const QJsonObject anthropicTurn = serializeAssistantTurn(WireProtocol::Anthropic, QStringLiteral("checking"), {call});
    const QJsonArray blocks = anthropicTurn.value(QStringLiteral("content")).toArray();
    ASSERT_EQ(blocks.size(), 2);
    EXPECT_EQ(blocks.at(0).toObject().value(QStringLiteral("type")).toString(), QStringLiteral("text"));
    EXPECT_EQ(blocks.at(1).toObject().value(QStringLiteral("type")).toString(), QStringLiteral("tool_use"));

    const ToolResult result{call.id, QStringLiteral("15 degrees"), false};
    const QVector<QJsonObject> openAiResults = serializeToolResults(WireProtocol::OpenAiCompatible, {result});
    ASSERT_EQ(openAiResults.size(), 1);
    EXPECT_EQ(openAiResults.first().value(QStringLiteral("role")).toString(), QStringLiteral("tool"));
    EXPECT_EQ(openAiResults.first().value(QStringLiteral("tool_call_id")).toString(), call.id);

    const QVector<QJsonObject> anthropicResults = serializeToolResults(WireProtocol::Anthropic, {result});
    ASSERT_EQ(anthropicResults.size(), 1);
    EXPECT_EQ(anthropicResults.first().value(QStringLiteral("role")).toString(), QStringLiteral("user"));
    EXPECT_EQ(anthropicResults.first().value(QStringLiteral("content")).toArray().first().toObject().value(QStringLiteral("tool_use_id")).toString(), call.id);

    const ToolResult failure{call.id, QStringLiteral("boom"), true};
    EXPECT_TRUE(serializeToolResults(WireProtocol::Anthropic, {failure}).first().value(QStringLiteral("content")).toArray().first().toObject().value(QStringLiteral("is_error")).toBool());
}

TEST(AiPluginTest, CarriesThePictureAToolReadIntoTheRequestThatFollowsIt) {
    QTemporaryDir workdir;
    ASSERT_TRUE(workdir.isValid());
    QImage picture(4, 4, QImage::Format_RGB32);
    picture.fill(Qt::red);
    ASSERT_TRUE(picture.save(workdir.filePath(QStringLiteral("shot.png"))));

    test::TestPluginHost host;
    host.translations = translations::english();
    filesystem::FileSystemService files;
    host.useFileSystem(files);
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const AiWorkspace workspace{QStringLiteral("workspace-1"), QStringLiteral("Product"), 0, true, now, now};
    AiTask task = AiTestsHelper::makeTask(QStringLiteral("task-1"), workspace.id);
    task.workdir = workdir.path();

    ModelConnection seeing = AiTestsHelper::testConnection();
    seeing.modelId = QStringLiteral("chatgpt-4o-latest");
    seeing.parameters = defaultParameters(*findProvider(seeing.providerId), seeing.modelId);
    AiAgent reader = AiTestsHelper::testAgent();
    reader.connectionKey = connectionKey(seeing);
    AiTestsHelper::installAiRows(host, {workspace}, {task}, {}, {seeing}, {reader});

    QVector<FakeChatClient*> clients;
    // clang-format off
    AiPlugin plugin([&clients](AiRequestGate&) { auto created = std::make_unique<FakeChatClient>(); clients.append(created.get()); return created; });
    // clang-format on
    ASSERT_TRUE(plugin.initialize(host).hasValue());
    ASSERT_TRUE(test::awaitFuture(plugin.startTask(task.id)).hasValue());
    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { return clients.size() == 1; }));
    // clang-format on

    FakeChatClient* seeingAgent = clients.first();
    seeingAgent->deliverToolCalls({{QStringLiteral("call-1"), QStringLiteral("read_image"), QJsonObject{{QStringLiteral("path"), QStringLiteral("shot.png")}}}});
    // clang-format off
    ASSERT_TRUE(test::waitUntil([seeingAgent]() { return seeingAgent->sendCalls == 2; }));
    // clang-format on

    // The picture the tool read reaches the model, because a tool result that carries only its text shows nothing.
    bool carried = false;

    for (const auto& message : seeingAgent->sentMessages) {
        for (const auto& block : message.toObject().value(QStringLiteral("content")).toArray()) {
            const QJsonObject entry = block.toObject();
            carried = carried || entry.value(QStringLiteral("type")).toString() == QStringLiteral("image_url") || entry.value(QStringLiteral("type")).toString() == QStringLiteral("image");
        }
    }

    EXPECT_TRUE(carried);

    plugin.shutdown();
}

TEST(AiToolContractTest, ShortensTheTextOfAResultThatCarriesAnImageWithoutLosingThePicture) {
    const QString huge(40000, QLatin1Char('x'));
    const QByteArray pixels = QByteArrayLiteral("-pretend-this-is-a-picture-");
    const QVector<ToolResult> results{{QStringLiteral("call-1"), QStringLiteral("head-marker") + huge + QStringLiteral("tail-marker"), false, pixels, QByteArrayLiteral("image/png")}, {QStringLiteral("call-2"), QStringLiteral("head-marker") + huge + QStringLiteral("tail-marker"), false, {}, {}}};
    QJsonArray messages;

    for (const auto& message : serializeToolResults(WireProtocol::Anthropic, results)) {
        messages.append(message);
    }

    ASSERT_GT(pruneToolResults(messages, estimateTokens(messages) / 2), 0);

    // The text of both results is shortened, and the picture the first one carries is still there.
    const QJsonArray blocks = messages.first().toObject().value(QStringLiteral("content")).toArray();
    ASSERT_EQ(blocks.size(), 2);
    const QJsonArray carried = blocks.first().toObject().value(QStringLiteral("content")).toArray();
    ASSERT_EQ(carried.size(), 2);
    const QString shortened = carried.first().toObject().value(QStringLiteral("text")).toString();
    EXPECT_TRUE(shortened.startsWith(QStringLiteral("head-marker")));
    EXPECT_TRUE(shortened.endsWith(QStringLiteral("tail-marker")));
    EXPECT_LT(shortened.size(), huge.size());
    EXPECT_EQ(carried.at(1).toObject().value(QStringLiteral("type")).toString(), QStringLiteral("image"));
    EXPECT_EQ(QByteArray::fromBase64(carried.at(1).toObject().value(QStringLiteral("source")).toObject().value(QStringLiteral("data")).toString().toUtf8()), pixels);

    const QString plain = blocks.at(1).toObject().value(QStringLiteral("content")).toString();
    EXPECT_TRUE(plain.startsWith(QStringLiteral("head-marker")));
    EXPECT_TRUE(plain.endsWith(QStringLiteral("tail-marker")));
}

TEST(AiToolContractTest, CarriesAnImageInTheShapeEachProtocolAccepts) {
    const QByteArray pixels = QByteArrayLiteral("\x89PNG\r\n\x1a\n-pretend-this-is-a-picture");
    const QVector<ToolResult> results{{QStringLiteral("call-1"), QStringLiteral("shot.png"), false, pixels, QByteArrayLiteral("image/png")}, {QStringLiteral("call-2"), QStringLiteral("done"), false, {}, {}}};

    // The Anthropic API accepts the image inside the result of the tool that read it.
    const QVector<QJsonObject> anthropic = serializeToolResults(WireProtocol::Anthropic, results);
    ASSERT_EQ(anthropic.size(), 1);
    const QJsonArray blocks = anthropic.first().value(QStringLiteral("content")).toArray();
    ASSERT_EQ(blocks.size(), 2);
    const QJsonArray carried = blocks.first().toObject().value(QStringLiteral("content")).toArray();
    ASSERT_EQ(carried.size(), 2);
    EXPECT_EQ(carried.first().toObject().value(QStringLiteral("text")).toString(), QStringLiteral("shot.png"));
    EXPECT_EQ(carried.at(1).toObject().value(QStringLiteral("type")).toString(), QStringLiteral("image"));
    const QJsonObject source = carried.at(1).toObject().value(QStringLiteral("source")).toObject();
    EXPECT_EQ(source.value(QStringLiteral("media_type")).toString(), QStringLiteral("image/png"));
    EXPECT_EQ(QByteArray::fromBase64(source.value(QStringLiteral("data")).toString().toUtf8()), pixels);
    EXPECT_TRUE(blocks.at(1).toObject().value(QStringLiteral("content")).isString());

    // The OpenAI tool message carries text alone, so the image follows it as the user turn that shows it.
    const QVector<QJsonObject> openAi = serializeToolResults(WireProtocol::OpenAiCompatible, results);
    ASSERT_EQ(openAi.size(), 3);
    EXPECT_EQ(openAi.at(0).value(QStringLiteral("role")).toString(), QStringLiteral("tool"));
    EXPECT_EQ(openAi.at(0).value(QStringLiteral("content")).toString(), QStringLiteral("shot.png"));
    EXPECT_EQ(openAi.at(1).value(QStringLiteral("role")).toString(), QStringLiteral("tool"));
    EXPECT_EQ(openAi.at(2).value(QStringLiteral("role")).toString(), QStringLiteral("user"));
    const QJsonArray images = openAi.at(2).value(QStringLiteral("content")).toArray();
    ASSERT_EQ(images.size(), 1);
    EXPECT_EQ(images.first().toObject().value(QStringLiteral("type")).toString(), QStringLiteral("image_url"));
    EXPECT_TRUE(images.first().toObject().value(QStringLiteral("image_url")).toObject().value(QStringLiteral("url")).toString().startsWith(QStringLiteral("data:image/png;base64,")));

    // A turn that read no image adds no user turn at all.
    const QVector<ToolResult> textOnly{{QStringLiteral("call-3"), QStringLiteral("done"), false, {}, {}}};
    EXPECT_EQ(serializeToolResults(WireProtocol::OpenAiCompatible, textOnly).size(), 1);
    EXPECT_EQ(serializeToolResults(WireProtocol::Anthropic, textOnly).size(), 1);
}

TEST(AiToolContractTest, ShortensOldToolResultsBeforeAnyTurnIsDropped) {
    const QString huge(40000, QLatin1Char('x'));
    QJsonArray messages{QJsonObject{{QStringLiteral("role"), QStringLiteral("system")}, {QStringLiteral("content"), QStringLiteral("instructions")}}, QJsonObject{{QStringLiteral("role"), QStringLiteral("user")}, {QStringLiteral("content"), QStringLiteral("do the task")}}, QJsonObject{{QStringLiteral("role"), QStringLiteral("assistant")}, {QStringLiteral("content"), QStringLiteral("looking")}}, QJsonObject{{QStringLiteral("role"), QStringLiteral("tool")}, {QStringLiteral("tool_call_id"), QStringLiteral("call-1")}, {QStringLiteral("content"), QStringLiteral("head-marker") + huge + QStringLiteral("tail-marker")}}};
    const qsizetype turnsBefore = messages.size();
    const qint64 limit = estimateTokens(messages) / 2;

    const qsizetype pruned = pruneToolResults(messages, limit);
    EXPECT_EQ(pruned, 1);
    EXPECT_EQ(messages.size(), turnsBefore);
    const QString shortened = messages.at(3).toObject().value(QStringLiteral("content")).toString();
    EXPECT_TRUE(shortened.startsWith(QStringLiteral("head-marker")));
    EXPECT_TRUE(shortened.endsWith(QStringLiteral("tail-marker")));
    EXPECT_LT(shortened.size(), huge.size());

    // Nothing but a tool result is touched, and a conversation that already fits is left alone.
    EXPECT_EQ(messages.at(2).toObject().value(QStringLiteral("content")).toString(), QStringLiteral("looking"));
    QJsonArray fitting{QJsonObject{{QStringLiteral("role"), QStringLiteral("tool")}, {QStringLiteral("content"), QStringLiteral("short")}}};
    EXPECT_EQ(pruneToolResults(fitting, 1000000), 0);

    // An Anthropic tool result carries its text inside its blocks and is shortened the same way.
    QJsonArray anthropic{QJsonObject{{QStringLiteral("role"), QStringLiteral("user")}, {QStringLiteral("content"), QJsonArray{QJsonObject{{QStringLiteral("type"), QStringLiteral("tool_result")}, {QStringLiteral("tool_use_id"), QStringLiteral("call-1")}, {QStringLiteral("content"), QStringLiteral("head-marker") + huge + QStringLiteral("tail-marker")}}}}}};
    EXPECT_EQ(pruneToolResults(anthropic, estimateTokens(anthropic) / 2), 1);
    const QString block = anthropic.at(0).toObject().value(QStringLiteral("content")).toArray().first().toObject().value(QStringLiteral("content")).toString();
    EXPECT_TRUE(block.startsWith(QStringLiteral("head-marker")));
    EXPECT_TRUE(block.endsWith(QStringLiteral("tail-marker")));
}

TEST(AiToolContractTest, RebuildsToolCallsFromTheRecordedAnthropicStream) {
    // Recorded from the published Anthropic tool use streaming example.
    const QStringList events{QStringLiteral(R"({"type":"content_block_start","index":1,"content_block":{"type":"tool_use","id":"toolu_01T1x1fJ34qAmk2tNTrN7Up6","name":"get_weather","input":{}}})"), QStringLiteral(R"({"type":"content_block_delta","index":1,"delta":{"type":"input_json_delta","partial_json":""}})"), QStringLiteral(R"({"type":"content_block_delta","index":1,"delta":{"type":"input_json_delta","partial_json":"{\"location\":"}})"), QStringLiteral(R"({"type":"content_block_delta","index":1,"delta":{"type":"input_json_delta","partial_json":" \"San"}})"), QStringLiteral(R"({"type":"content_block_delta","index":1,"delta":{"type":"input_json_delta","partial_json":" Francisc"}})"), QStringLiteral(R"({"type":"content_block_delta","index":1,"delta":{"type":"input_json_delta","partial_json":"o,"}})"), QStringLiteral(R"({"type":"content_block_delta","index":1,"delta":{"type":"input_json_delta","partial_json":" CA\"}"}})"), QStringLiteral(R"({"type":"content_block_stop","index":1})")};

    ToolCallAccumulator accumulator(WireProtocol::Anthropic);
    EXPECT_TRUE(accumulator.empty());

    for (const auto& event : events) {
        accumulator.consume(QJsonDocument::fromJson(event.toUtf8()).object());
    }

    const auto calls = accumulator.calls();
    ASSERT_TRUE(calls.hasValue()) << calls.error().code.toStdString();
    ASSERT_EQ(calls.value().size(), 1);
    EXPECT_EQ(calls.value().first().id, QStringLiteral("toolu_01T1x1fJ34qAmk2tNTrN7Up6"));
    EXPECT_EQ(calls.value().first().name, QStringLiteral("get_weather"));
    EXPECT_EQ(calls.value().first().arguments.value(QStringLiteral("location")).toString(), QStringLiteral("San Francisco, CA"));
}

TEST(AiToolContractTest, RebuildsToolCallsFromTheRecordedOpenAiStream) {
    // Recorded from the published OpenAI streamed tool call format.
    const QStringList events{QStringLiteral(R"({"choices":[{"index":0,"delta":{"tool_calls":[{"index":0,"id":"call_abc123","type":"function","function":{"name":"get_weather","arguments":""}}]}}]})"), QStringLiteral(R"({"choices":[{"index":0,"delta":{"tool_calls":[{"index":0,"function":{"arguments":"{\"loc"}}]}}]})"), QStringLiteral(R"({"choices":[{"index":0,"delta":{"tool_calls":[{"index":0,"function":{"arguments":"ation\":\"Paris\"}"}}]}}]})"), QStringLiteral(R"({"choices":[{"index":0,"delta":{},"finish_reason":"tool_calls"}]})")};

    ToolCallAccumulator accumulator(WireProtocol::OpenAiCompatible);

    for (const auto& event : events) {
        accumulator.consume(QJsonDocument::fromJson(event.toUtf8()).object());
    }

    const auto calls = accumulator.calls();
    ASSERT_TRUE(calls.hasValue()) << calls.error().code.toStdString();
    ASSERT_EQ(calls.value().size(), 1);
    EXPECT_EQ(calls.value().first().id, QStringLiteral("call_abc123"));
    EXPECT_EQ(calls.value().first().arguments.value(QStringLiteral("location")).toString(), QStringLiteral("Paris"));

    accumulator.clear();
    EXPECT_TRUE(accumulator.empty());
}

TEST(AiToolContractTest, AnswersAMalformedToolCallAndRefusesOneWithoutItsIdentity) {
    // A call whose arguments could not be read still reaches the model, carrying what arrived with it.
    ToolCallAccumulator truncated(WireProtocol::OpenAiCompatible);
    truncated.consume(QJsonDocument::fromJson(QByteArrayLiteral(R"({"choices":[{"index":0,"delta":{"tool_calls":[{"index":0,"id":"call_1","function":{"name":"get_weather","arguments":"{\"location\":"}}]}}]})")).object());
    const auto unreadable = truncated.calls();
    ASSERT_TRUE(unreadable.hasValue());
    ASSERT_EQ(unreadable.value().size(), 1);
    EXPECT_EQ(unreadable.value().first().name, QStringLiteral("get_weather"));
    EXPECT_EQ(unreadable.value().first().unreadableArguments, QStringLiteral("{\"location\":"));
    EXPECT_TRUE(unreadable.value().first().arguments.isEmpty());

    // A call nobody can answer has no identity to answer it with, so that one still ends the run.
    ToolCallAccumulator withoutName(WireProtocol::OpenAiCompatible);
    withoutName.consume(QJsonDocument::fromJson(QByteArrayLiteral(R"({"choices":[{"index":0,"delta":{"tool_calls":[{"index":0,"id":"call_1","function":{"arguments":"{}"}}]}}]})")).object());
    EXPECT_EQ(withoutName.calls().error().code, QStringLiteral("ai_tool_call_invalid"));

    // A tool call carrying no argument fragment is still a valid call with an empty object.
    ToolCallAccumulator withoutArguments(WireProtocol::Anthropic);
    withoutArguments.consume(QJsonDocument::fromJson(QByteArrayLiteral(R"({"type":"content_block_start","index":0,"content_block":{"type":"tool_use","id":"toolu_1","name":"list_files","input":{}}})")).object());
    const auto calls = withoutArguments.calls();
    ASSERT_TRUE(calls.hasValue());
    EXPECT_TRUE(calls.value().first().arguments.isEmpty());
}

TEST(AiCommandRunnerTest, RunsACommandAndReportsItsOutputAndExitCode) {
    QTemporaryDir workdir;
    ASSERT_TRUE(workdir.isValid());

    AiCommandRunner runner;
    QString output;
    int exitCode = -1;
    bool completed = false;
    // clang-format off
    QObject::connect(&runner, &AiCommandRunner::finished, &runner, [&](int code, const QString& text) { exitCode = code; output = text; completed = true; });
    // clang-format on
    runner.start(QStringLiteral("echo slotdeck"), workdir.path(), 30);

    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { return completed; }));
    // clang-format on
    EXPECT_EQ(exitCode, 0);
    EXPECT_TRUE(output.contains(QStringLiteral("slotdeck")));
    EXPECT_FALSE(runner.running());
}

TEST(AiCommandRunnerTest, ReportsANonZeroExitCodeAndRejectsInvalidInput) {
    QTemporaryDir workdir;
    ASSERT_TRUE(workdir.isValid());

    AiCommandRunner failing;
    int exitCode = -1;
    bool completed = false;
    // clang-format off
    QObject::connect(&failing, &AiCommandRunner::finished, &failing, [&](int code, const QString&) { exitCode = code; completed = true; });
    // clang-format on
    failing.start(QStringLiteral("exit 3"), workdir.path(), 30);
    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { return completed; }));
    // clang-format on
    EXPECT_EQ(exitCode, 3);

    AiCommandRunner invalid;
    QVector<utils::Error> failures;
    // clang-format off
    QObject::connect(&invalid, &AiCommandRunner::failed, &invalid, [&failures](const utils::Error& error) { failures.append(error); });
    // clang-format on
    invalid.start(QStringLiteral("   "), workdir.path(), 30);
    ASSERT_EQ(failures.size(), 1);
    EXPECT_EQ(failures.first().code, QStringLiteral("ai_command_invalid"));

    AiCommandRunner missingDirectory;
    QVector<utils::Error> directoryFailures;
    // clang-format off
    QObject::connect(&missingDirectory, &AiCommandRunner::failed, &missingDirectory, [&directoryFailures](const utils::Error& error) { directoryFailures.append(error); });
    // clang-format on
    missingDirectory.start(QStringLiteral("echo hi"), QDir(workdir.path()).filePath(QStringLiteral("absent")), 30);
    ASSERT_EQ(directoryFailures.size(), 1);
    EXPECT_EQ(directoryFailures.first().code, QStringLiteral("ai_command_workdir_invalid"));
}

TEST(AiCommandRunnerTest, StopsACommandThatExceedsItsTimeLimit) {
    QTemporaryDir workdir;
    ASSERT_TRUE(workdir.isValid());

    AiCommandRunner runner;
    QVector<utils::Error> failures;
    // clang-format off
    QObject::connect(&runner, &AiCommandRunner::failed, &runner, [&failures](const utils::Error& error) { failures.append(error); });
    // clang-format on
    runner.start(AiTestsHelper::sleepingCommand(5), workdir.path(), 1);

    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { return !failures.isEmpty(); }, 8000));
    // clang-format on
    EXPECT_EQ(failures.first().code, QStringLiteral("ai_command_timeout"));
    EXPECT_FALSE(runner.running());
    EXPECT_EQ(failures.size(), 1);
}

TEST(AiCommandRunnerTest, ReleasesARunningCommandWithoutBlockingTheInterface) {
    QTemporaryDir workdir;
    ASSERT_TRUE(workdir.isValid());

    QElapsedTimer elapsed;
    {
        AiCommandRunner runner;
        runner.start(AiTestsHelper::sleepingCommand(5), workdir.path(), 0);
        // clang-format off
        ASSERT_TRUE(test::waitUntil([&]() { return runner.running(); }));
        // clang-format on
        runner.cancel();
        EXPECT_FALSE(runner.running());
        elapsed.start();
    }

    // A command that ignores the graceful request must never hold the interactive thread while its runner is released.
    EXPECT_LT(elapsed.elapsed(), 2000);

    QElapsedTimer abandoned;
    {
        AiCommandRunner runner;
        runner.start(AiTestsHelper::sleepingCommand(5), workdir.path(), 0);
        // clang-format off
        ASSERT_TRUE(test::waitUntil([&]() { return runner.running(); }));
        // clang-format on
        abandoned.start();
    }
    EXPECT_LT(abandoned.elapsed(), 2000);
}

TEST(AiPluginTest, RunsTheAgentUntilItStopsAskingForToolsAndStopsAtTheIterationLimit) {
    test::TestPluginHost host;
    host.translations = translations::english();
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const AiWorkspace workspace{QStringLiteral("workspace-1"), QStringLiteral("Product"), 0, true, now, now};
    AiTask task = AiTestsHelper::makeTask(QStringLiteral("task-1"), workspace.id);
    AiAgent limitedAgent = AiTestsHelper::testAgent();
    limitedAgent.maximumIterations = 3;
    AiTestsHelper::installAiRows(host, {workspace}, {task}, {}, {AiTestsHelper::testConnection()}, {limitedAgent});

    FakeChatClient* client = nullptr;
    // clang-format off
    AiPlugin plugin([&client](AiRequestGate&) { auto created = std::make_unique<FakeChatClient>(); client = created.get(); return created; });
    // clang-format on
    ASSERT_TRUE(plugin.initialize(host).hasValue());
    ASSERT_TRUE(test::awaitFuture(plugin.startTask(task.id)).hasValue());
    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { return client != nullptr && client->sendCalls == 1; }));
    // clang-format on

    // A tool call feeds the result back and starts another iteration instead of finishing the run.
    client->deliverToolCalls({{QStringLiteral("call_1"), QStringLiteral("list_files"), QJsonObject{{QStringLiteral("path"), QStringLiteral("/tmp")}}}});
    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { return client->sendCalls == 2; }));
    // clang-format on
    EXPECT_EQ(plugin.runState(task.id), TaskRunState::Running);
    EXPECT_GE(client->sentMessages.size(), 3);

    client->deliver(QStringLiteral("done"), {5, 6}, QStringLiteral("stop"));
    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { return plugin.runState(task.id) == TaskRunState::Idle; }));
    // clang-format on
    EXPECT_EQ(plugin.lastExecutionStatus(task.id), ExecutionStatus::Succeeded);
    plugin.shutdown();
}

TEST(AiPluginTest, EndsTheAgentAtItsIterationLimitWithTheReasonRatherThanWithAFailure) {
    test::TestPluginHost host;
    host.translations = translations::english();
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const AiWorkspace workspace{QStringLiteral("workspace-1"), QStringLiteral("Product"), 0, true, now, now};
    AiTask task = AiTestsHelper::makeTask(QStringLiteral("task-1"), workspace.id);
    AiAgent limitedAgent = AiTestsHelper::testAgent();
    limitedAgent.maximumIterations = 1;
    AiTestsHelper::installAiRows(host, {workspace}, {task}, {}, {AiTestsHelper::testConnection()}, {limitedAgent});

    FakeChatClient* client = nullptr;
    // clang-format off
    AiPlugin plugin([&client](AiRequestGate&) { auto created = std::make_unique<FakeChatClient>(); client = created.get(); return created; });
    // clang-format on
    ASSERT_TRUE(plugin.initialize(host).hasValue());
    ASSERT_TRUE(test::awaitFuture(plugin.startTask(task.id)).hasValue());
    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { return client != nullptr && client->sendCalls == 1; }));
    // clang-format on

    // The turn that has no turn after it is told so, so the model answers instead of calling another tool.
    const QString firstRequest = QString::fromUtf8(QJsonDocument(client->sentMessages).toJson(QJsonDocument::Compact));
    EXPECT_TRUE(firstRequest.contains(QStringLiteral("last turn"))) << firstRequest.toStdString();

    client->deliverToolCalls({{QStringLiteral("call_1"), QStringLiteral("list_files"), QJsonObject{}}});
    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { return plugin.runState(task.id) == TaskRunState::Idle; }));
    // clang-format on
    EXPECT_EQ(client->sendCalls, 1);
    EXPECT_EQ(plugin.lastExecutionStatus(task.id), ExecutionStatus::Succeeded);
    EXPECT_EQ(plugin.lastStopReason(task.id), AgentStopReason::IterationLimit);
    EXPECT_TRUE(plugin.lastError(task.id).isEmpty());
    plugin.shutdown();
}

TEST(AiChatClientTest, WithdrawsFromTheQueueWhenTheRunIsStoppedBeforeItsTurnCame) {
    QTcpServer server;
    ASSERT_TRUE(server.listen(QHostAddress::LocalHost, 0));

    const ProviderDescriptor* openai = findProvider(QStringLiteral("openai"));
    ASSERT_NE(openai, nullptr);
    const QString model = QStringLiteral("gpt-4o");
    const ModelConnection connection{openai->id, model, {}, QStringLiteral("sk"), {}, defaultParameters(*openai, model), {}};
    const ChatRequest request{connection, QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort()), QJsonArray{QJsonObject{{QStringLiteral("role"), QStringLiteral("user")}, {QStringLiteral("content"), QStringLiteral("hello")}}}, {}};
    // clang-format off
    const auto translate = [](const QString& key) { return key; };
    // clang-format on

    // One request at a time, so the second one waits for its turn.
    AiRequestGate gate;
    gate.setLimits({{openai->id, 0, 0, 1}});
    AiHttpChatClient holding(gate);
    AiHttpChatClient waiting(gate);
    holding.send(request, translate);
    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { return gate.inFlight(openai->id) == 1; }));
    // clang-format on
    waiting.send(request, translate);
    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { return gate.waiting(openai->id) == 1; }));
    // clang-format on

    // The run is stopped before its turn came, so the place it was holding in the queue is given back.
    waiting.cancel();
    EXPECT_EQ(gate.waiting(openai->id), 0);

    // The place given back is never admitted, so the stopped run is not dispatched when the queue moves.
    holding.cancel();

    for (int turn = 0; turn < 30; ++turn) {
        QApplication::processEvents(QEventLoop::AllEvents, 5);
    }

    EXPECT_EQ(gate.waiting(openai->id), 0);
    EXPECT_EQ(gate.inFlight(openai->id), 0);
    EXPECT_FALSE(waiting.running());
}

TEST(AiToolRegistryTest, DeclaresValidSchemasAndKeepsEveryPathInsideTheWorkingDirectory) {
    test::TestPluginHost host;
    host.translations = translations::english();
    AiToolRegistry registry(host);

    ASSERT_FALSE(registry.schemas().isEmpty());

    for (const auto& schema : registry.schemas()) {
        EXPECT_TRUE(validateToolSchema(schema).hasValue()) << schema.name.toStdString();
    }

    QTemporaryDir root;
    ASSERT_TRUE(root.isValid());
    const QString sandbox = QDir(root.path()).canonicalPath();

    QVector<ToolResult> results;
    // clang-format off
    const auto collect = [&results](ToolResult result) { results.append(std::move(result)); };
    // clang-format on

    // A traversal, an absolute path and a task without a working directory are all refused.
    registry.invoke({QStringLiteral("c1"), QStringLiteral("read_file"), QJsonObject{{QStringLiteral("path"), QStringLiteral("../escape.txt")}}}, sandbox, collect);
    registry.invoke({QStringLiteral("c2"), QStringLiteral("read_file"), QJsonObject{{QStringLiteral("path"), QStringLiteral("/etc/hosts")}}}, sandbox, collect);
    registry.invoke({QStringLiteral("c3"), QStringLiteral("read_file"), QJsonObject{{QStringLiteral("path"), QStringLiteral("file.txt")}}}, QString{}, collect);
    registry.invoke({QStringLiteral("c4"), QStringLiteral("nonexistent_tool"), QJsonObject{}}, sandbox, collect);
    ASSERT_EQ(results.size(), 4);

    for (const auto& result : results) {
        EXPECT_TRUE(result.failed) << result.text.toStdString();
    }
}

TEST(AiToolRegistryTest, WritesReadsAndListsInsideTheWorkingDirectory) {
    test::TestPluginHost host;
    host.translations = translations::english();
    filesystem::FileSystemService files;
    host.useFileSystem(files);
    AiToolRegistry registry(host);

    QTemporaryDir root;
    ASSERT_TRUE(root.isValid());
    const QString sandbox = QDir(root.path()).canonicalPath();

    QVector<ToolResult> results;
    // clang-format off
    const auto collect = [&results](ToolResult result) { results.append(std::move(result)); };
    // clang-format on

    registry.invoke({QStringLiteral("w1"), QStringLiteral("write_file"), QJsonObject{{QStringLiteral("path"), QStringLiteral("notes.txt")}, {QStringLiteral("content"), QStringLiteral("slotdeck")}}}, sandbox, collect);
    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { return results.size() == 1; }));
    // clang-format on
    ASSERT_FALSE(results.first().failed) << results.first().text.toStdString();

    registry.invoke({QStringLiteral("r1"), QStringLiteral("read_file"), QJsonObject{{QStringLiteral("path"), QStringLiteral("notes.txt")}}}, sandbox, collect);
    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { return results.size() == 2; }));
    // clang-format on
    EXPECT_FALSE(results.at(1).failed);
    EXPECT_EQ(results.at(1).text, QStringLiteral("slotdeck"));

    registry.invoke({QStringLiteral("l1"), QStringLiteral("list_directory"), QJsonObject{{QStringLiteral("path"), QStringLiteral("")}}}, sandbox, collect);
    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { return results.size() == 3; }));
    // clang-format on
    EXPECT_FALSE(results.at(2).failed);
    EXPECT_TRUE(results.at(2).text.contains(QStringLiteral("notes.txt")));

    registry.invoke({QStringLiteral("w2"), QStringLiteral("write_file"), QJsonObject{{QStringLiteral("path"), QStringLiteral("bad.txt")}, {QStringLiteral("content"), 42}}}, sandbox, collect);
    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { return results.size() == 4; }));
    // clang-format on
    EXPECT_TRUE(results.at(3).failed);
}

TEST(AiChatClientTest, RetriesATransientFailureAndGivesUpOnARejection) {
    QTcpServer server;
    ASSERT_TRUE(server.listen(QHostAddress::LocalHost, 0));
    int requests = 0;
    // clang-format off
    QObject::connect(&server, &QTcpServer::newConnection, &server, [&server, &requests]() {
        QTcpSocket* socket = server.nextPendingConnection();
        QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket, &requests]() {
        if (!socket->readAll().contains(QByteArrayLiteral("\r\n\r\n"))) {
            return;
        }
        ++requests;
        const QByteArray body = QByteArrayLiteral("{\"error\":{\"message\":\"overloaded\"}}");
        socket->write(QByteArrayLiteral("HTTP/1.1 503 Service Unavailable\r\nContent-Type: application/json\r\nContent-Length: ") + QByteArray::number(body.size()) + QByteArrayLiteral("\r\nConnection: close\r\n\r\n") + body);
        socket->disconnectFromHost();
        });
    });
    // clang-format on

    const ProviderDescriptor* openai = findProvider(QStringLiteral("openai"));
    ASSERT_NE(openai, nullptr);
    const QString model = QStringLiteral("gpt-4o");
    const ModelConnection connection{openai->id, model, {}, QStringLiteral("sk"), {}, defaultParameters(*openai, model), {}};

    AiRequestGate clientGate;
    AiHttpChatClient client(clientGate);
    QVector<utils::Error> failures;
    QList<qint64> waits;
    // clang-format off
    QObject::connect(&client, &AiChatClient::failed, &client, [&failures](const utils::Error& error) { failures.append(error); });
    QObject::connect(&client, &AiChatClient::throttled, &client, [&waits](ThrottleReason reason, qint64 milliseconds) { if (reason == ThrottleReason::Retry) { waits.append(milliseconds); } });
    // clang-format on
    // clang-format off
    client.send({connection, QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort()), QJsonArray{QJsonObject{{QStringLiteral("role"), QStringLiteral("user")}, {QStringLiteral("content"), QStringLiteral("hello")}}}, {}}, [](const QString& key) { return key; });
    // clang-format on

    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { return !failures.isEmpty(); }, 10000));
    // clang-format on

    // A server side condition is retried up to the provider limit and then reported once.
    EXPECT_EQ(requests, openai->requestMaxRetries + 1);
    EXPECT_EQ(failures.size(), 1);
    EXPECT_TRUE(failures.first().message.contains(QStringLiteral("overloaded")));

    // Every attempt after the first one waited, because a rejection repeated immediately reproduces what caused it.
    ASSERT_EQ(waits.size(), openai->requestMaxRetries);
    EXPECT_GE(waits.first(), aiLimits().retryBackoffMs);
    EXPECT_GT(waits.last(), waits.first());
}

TEST(AiChatClientTest, PacesTwoRequestsOfOneProviderThroughTheSharedGate) {
    QTcpServer server;
    ASSERT_TRUE(server.listen(QHostAddress::LocalHost, 0));
    QList<qint64> arrivals;
    QElapsedTimer clock;
    clock.start();
    // clang-format off
    QObject::connect(&server, &QTcpServer::newConnection, &server, [&server, &arrivals, &clock]() {
        QTcpSocket* socket = server.nextPendingConnection();
        QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket, &arrivals, &clock]() {
        if (!socket->readAll().contains(QByteArrayLiteral("\r\n\r\n"))) {
            return;
        }
        arrivals.append(clock.elapsed());
        const QByteArray body = QByteArrayLiteral("data: {\"choices\":[{\"delta\":{\"content\":\"ok\"},\"finish_reason\":\"stop\"}]}\n\ndata: [DONE]\n\n");
        socket->write(QByteArrayLiteral("HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nContent-Length: ") + QByteArray::number(body.size()) + QByteArrayLiteral("\r\nConnection: close\r\n\r\n") + body);
        socket->disconnectFromHost();
        });
    });
    // clang-format on

    const ProviderDescriptor* openai = findProvider(QStringLiteral("openai"));
    ASSERT_NE(openai, nullptr);
    const QString model = QStringLiteral("gpt-4o");
    const ModelConnection connection{openai->id, model, {}, QStringLiteral("sk"), {}, defaultParameters(*openai, model), {}};
    const ChatRequest request{connection, QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort()), QJsonArray{QJsonObject{{QStringLiteral("role"), QStringLiteral("user")}, {QStringLiteral("content"), QStringLiteral("hello")}}}, {}};
    // clang-format off
    const auto translate = [](const QString& key) { return key; };
    // clang-format on

    // The tasks of every workspace reach one service through the same queue, so the second request waits for the declared delay.
    AiRequestGate gate;
    gate.setLimits({{openai->id, 400, 0, 0}});
    AiHttpChatClient first(gate);
    AiHttpChatClient second(gate);
    int finished = 0;
    QList<qint64> waits;
    // clang-format off
    QObject::connect(&first, &AiChatClient::finished, &first, [&finished](const QString&, const QVector<ToolCall>&, ChatUsage, const QString&) { ++finished; });
    QObject::connect(&second, &AiChatClient::finished, &second, [&finished](const QString&, const QVector<ToolCall>&, ChatUsage, const QString&) { ++finished; });
    QObject::connect(&second, &AiChatClient::throttled, &second, [&waits](ThrottleReason reason, qint64 milliseconds) { if (reason == ThrottleReason::RateLimit) { waits.append(milliseconds); } });
    // clang-format on
    first.send(request, translate);
    second.send(request, translate);

    // clang-format off
    ASSERT_TRUE(test::waitUntil([&]() { return finished == 2; }, 10000));
    // clang-format on
    ASSERT_EQ(arrivals.size(), 2);
    EXPECT_GT(arrivals.at(1), arrivals.at(0));

    // The pace is measured where it is applied, because an arrival at the socket also carries the setup of its own connection and the clock that timed it is not the one the gate waits on.
    EXPECT_EQ(waits.size(), 1);
    EXPECT_GE(waits.first(), 250);
    EXPECT_LE(waits.first(), 400);
}

} // namespace slotdeck::plugins::ai
