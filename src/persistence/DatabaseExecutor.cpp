#include "persistence/DatabaseExecutor.h"

#include "domain/ApplicationSettings.h"
#include "persistence/ConfigurationTransfer.h"
#include "persistence/StateStore.h"

#include <QMetaObject>
#include <QPromise>

#include <functional>
#include <utility>

namespace slotdeck::persistence {

class DatabaseWorker final : public QObject {
  public:
    explicit DatabaseWorker(QString filePath) : m_filePath(std::move(filePath)) {}

    [[nodiscard]] utils::Result<StateStore*> openedStore() {
        if (m_stateStore != nullptr) {
            return utils::Result<StateStore*>::success(m_stateStore.get());
        }

        auto stateStore = std::make_unique<StateStore>(m_filePath);
        const auto result = stateStore->initialize();
        if (!result.hasValue()) {
            return utils::Result<StateStore*>::failure(result.error());
        }

        m_stateStore = std::move(stateStore);
        return utils::Result<StateStore*>::success(m_stateStore.get());
    }

    void close() {
        m_stateStore.reset();
    }

  private:
    QString m_filePath;
    std::unique_ptr<StateStore> m_stateStore;
};

class DatabaseExecutorHelper final {
  public:
    template <typename T> static QFuture<utils::Result<T>> submit(DatabaseWorker* worker, std::function<utils::Result<T>(StateStore&)> operation);
};

template <typename T> QFuture<utils::Result<T>> DatabaseExecutorHelper::submit(DatabaseWorker* worker, std::function<utils::Result<T>(StateStore&)> operation) {
    auto promise = std::make_shared<QPromise<utils::Result<T>>>();
    promise->start();
    const QFuture<utils::Result<T>> future = promise->future();

    // clang-format off
    const bool submitted = QMetaObject::invokeMethod(worker, [worker, operation = std::move(operation), promise]() mutable {
        const auto opened = worker->openedStore();
        if (!opened.hasValue()) {
            promise->addResult(utils::Result<T>::failure(opened.error()));
            promise->finish();
            return;
        }
        promise->addResult(operation(*opened.value()));
        promise->finish();
    }, Qt::QueuedConnection);
    // clang-format on
    if (!submitted) {
        promise->addResult(utils::Result<T>::failure({"database_executor_unavailable", "The database executor is unavailable", {}}));
        promise->finish();
    }
    return future;
}

template <> QFuture<utils::Result<void>> DatabaseExecutorHelper::submit(DatabaseWorker* worker, std::function<utils::Result<void>(StateStore&)> operation) {
    auto promise = std::make_shared<QPromise<utils::Result<void>>>();
    promise->start();
    const QFuture<utils::Result<void>> future = promise->future();

    // clang-format off
    const bool submitted = QMetaObject::invokeMethod(worker, [worker, operation = std::move(operation), promise]() mutable {
        const auto opened = worker->openedStore();
        if (!opened.hasValue()) {
            promise->addResult(utils::Result<void>::failure(opened.error()));
            promise->finish();
            return;
        }
        promise->addResult(operation(*opened.value()));
        promise->finish();
    }, Qt::QueuedConnection);
    // clang-format on
    if (!submitted) {
        promise->addResult(utils::Result<void>::failure({"database_executor_unavailable", "The database executor is unavailable", {}}));
        promise->finish();
    }
    return future;
}

DatabaseExecutor::DatabaseExecutor(QString filePath, QObject* parent) : QObject(parent), m_worker(new DatabaseWorker(std::move(filePath))) {
    m_worker->moveToThread(&m_workerThread);
    m_workerThread.setObjectName(QStringLiteral("slotdeckDatabase"));
    m_workerThread.start();
}

DatabaseExecutor::~DatabaseExecutor() {
    if (!m_workerThread.isRunning()) {
        delete m_worker;
        return;
    }
    QThread* ownerThread = thread();
    // clang-format off
    QMetaObject::invokeMethod(m_worker, [this, ownerThread]() {
        m_worker->close();
        m_worker->moveToThread(ownerThread);
        m_workerThread.quit();
    }, Qt::QueuedConnection);
    // clang-format on
    m_workerThread.wait();
    delete m_worker;
}

QFuture<utils::Result<void>> DatabaseExecutor::saveSettings(const QString& ownerId, const QJsonObject& document) {
    // clang-format off
    return DatabaseExecutorHelper::submit<void>(m_worker, [ownerId, document](StateStore& stateStore) { return stateStore.saveSettings(ownerId, document); });
    // clang-format on
}

QFuture<utils::Result<void>> DatabaseExecutor::exportConfiguration(const QString& destinationPath) {
    // clang-format off
    return DatabaseExecutorHelper::submit<void>(m_worker, [destinationPath](StateStore& stateStore) { return ConfigurationTransfer::exportDatabaseNow(stateStore.filePath(), destinationPath); });
    // clang-format on
}

QFuture<utils::Result<void>> DatabaseExecutor::executePluginDatabase(const QString& pluginId, const QString& statement, const QVariantList& bindings) {
    // clang-format off
    return DatabaseExecutorHelper::submit<void>(m_worker, [pluginId, statement, bindings](StateStore& stateStore) { return stateStore.executePluginDatabase(pluginId, statement, bindings); });
    // clang-format on
}

QFuture<utils::Result<void>> DatabaseExecutor::executePluginDatabaseTransaction(const QString& pluginId, const QVector<DatabaseStatement>& statements) {
    // clang-format off
    return DatabaseExecutorHelper::submit<void>(m_worker, [pluginId, statements](StateStore& stateStore) { return stateStore.executePluginDatabaseTransaction(pluginId, statements); });
    // clang-format on
}

QFuture<utils::Result<DatabaseRows>> DatabaseExecutor::queryPluginDatabase(const QString& pluginId, const QString& statement, const QVariantList& bindings) {
    // clang-format off
    return DatabaseExecutorHelper::submit<DatabaseRows>(m_worker, [pluginId, statement, bindings](StateStore& stateStore) { return stateStore.queryPluginDatabase(pluginId, statement, bindings); });
    // clang-format on
}

} // namespace slotdeck::persistence
