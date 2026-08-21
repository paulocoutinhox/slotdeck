#pragma once

#include <QEventLoop>
#include <QFuture>
#include <QFutureWatcher>
#include <QTest>

namespace slotdeck::test {

template <typename T> T awaitFuture(const QFuture<T>& future) {
    if (future.isFinished()) {
        return future.result();
    }

    QEventLoop eventLoop;
    QFutureWatcher<T> watcher;
    QObject::connect(&watcher, &QFutureWatcher<T>::finished, &eventLoop, &QEventLoop::quit);
    watcher.setFuture(future);
    eventLoop.exec();
    return future.result();
}

// A condition can drive a child process through a complete handshake, which a loaded machine takes far longer to finish, and an instrumented build longer still.
#ifdef SLOTDECK_SANITIZERS_ENABLED
inline constexpr int defaultWaitTimeoutMilliseconds = 240000;
#else
inline constexpr int defaultWaitTimeoutMilliseconds = 30000;
#endif

// Qt Test owns the event loop aware waiting and reports the outcome, so an expired condition fails the surrounding GoogleTest assertion.
template <typename Predicate> bool waitUntil(Predicate predicate, int timeoutMilliseconds = defaultWaitTimeoutMilliseconds) {
    return QTest::qWaitFor(predicate, timeoutMilliseconds);
}

} // namespace slotdeck::test
