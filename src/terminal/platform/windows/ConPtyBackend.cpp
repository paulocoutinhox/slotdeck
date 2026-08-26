#include "terminal/platform/windows/ConPtyBackend.h"

#include "terminal/TerminalDimensions.h"

#include <QDir>
#include <QFileInfo>

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace slotdeck::terminalcore {

constexpr qsizetype maximumInputQueueSize = 1024 * 1024;

class UniqueHandle final {
  public:
    UniqueHandle() = default;
    explicit UniqueHandle(HANDLE handle) : m_handle(handle) {}
    ~UniqueHandle() {
        reset();
    }
    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;
    [[nodiscard]] HANDLE get() const {
        return m_handle;
    }
    void reset(HANDLE handle = nullptr) {
        if (m_handle != nullptr && m_handle != INVALID_HANDLE_VALUE) {
            CloseHandle(m_handle);
        }
        m_handle = handle;
    }

  private:
    HANDLE m_handle{nullptr};
};

class ConPtyBackendHelper final {
  public:
    static utils::Result<void> windowsFailure(const QString& code, const QString& message, DWORD errorCode = GetLastError());
    static QString quoteWindowsArgument(const QString& argument);
};

utils::Result<void> ConPtyBackendHelper::windowsFailure(const QString& code, const QString& message, DWORD errorCode) {
    return utils::Result<void>::failure({code, message, QString::number(static_cast<qulonglong>(errorCode))});
}

QString ConPtyBackendHelper::quoteWindowsArgument(const QString& argument) {
    QString quoted = QStringLiteral("\"");
    int backslashes = 0;
    for (const QChar character : argument) {
        if (character == QLatin1Char('\\')) {
            ++backslashes;
            continue;
        }
        if (character == QLatin1Char('"')) {
            quoted += QString(2 * backslashes + 1, QLatin1Char('\\'));
            quoted += character;
            backslashes = 0;
            continue;
        }
        quoted += QString(backslashes, QLatin1Char('\\'));
        quoted += character;
        backslashes = 0;
    }
    quoted += QString(2 * backslashes, QLatin1Char('\\'));
    quoted += QLatin1Char('"');
    return quoted;
}

struct ConPtyBackend::Handles final {
    HPCON pseudoConsole{nullptr};
    UniqueHandle inputWrite;
    UniqueHandle outputRead;
    UniqueHandle process;
};

ConPtyBackend::ConPtyBackend(QObject* parent) : IPtyBackend(parent) {}

ConPtyBackend::~ConPtyBackend() {
    terminate();
}

utils::Result<void> ConPtyBackend::start(const ShellProfile& profile, const QString& workingDirectory, const QString&, int columns, int rows) {
    if (running()) {
        return utils::Result<void>::failure({"conpty_already_running", "The terminal process is already running", {}});
    }
    if (!QFileInfo(profile.executable).isExecutable()) {
        return utils::Result<void>::failure({"shell_not_executable", "The selected shell is not executable", profile.executable});
    }
    if (!QFileInfo(workingDirectory).isDir()) {
        return utils::Result<void>::failure({"terminal_working_directory_missing", "The working directory does not exist", workingDirectory});
    }
    if (!validTerminalGrid(columns, rows)) {
        return utils::Result<void>::failure({"conpty_size_invalid", "The pseudo-console dimensions are invalid", QStringLiteral("%1x%2").arg(columns).arg(rows)});
    }
    terminate();
    m_stopping = false;
    m_outputPaused = false;

    HANDLE inputRead = nullptr;
    HANDLE inputWrite = nullptr;
    HANDLE outputRead = nullptr;
    HANDLE outputWrite = nullptr;
    if (!CreatePipe(&inputRead, &inputWrite, nullptr, 0)) {
        return ConPtyBackendHelper::windowsFailure(QStringLiteral("conpty_pipe_failed"), QStringLiteral("ConPTY pipes could not be created"));
    }
    if (!CreatePipe(&outputRead, &outputWrite, nullptr, 0)) {
        const DWORD errorCode = GetLastError();
        CloseHandle(inputRead);
        CloseHandle(inputWrite);
        return ConPtyBackendHelper::windowsFailure(QStringLiteral("conpty_pipe_failed"), QStringLiteral("ConPTY pipes could not be created"), errorCode);
    }

    m_handles = std::make_unique<Handles>();
    m_handles->inputWrite.reset(inputWrite);
    m_handles->outputRead.reset(outputRead);
    const COORD size{static_cast<SHORT>(columns), static_cast<SHORT>(rows)};
    if (FAILED(CreatePseudoConsole(size, inputRead, outputWrite, 0, &m_handles->pseudoConsole))) {
        const DWORD errorCode = GetLastError();
        CloseHandle(inputRead);
        CloseHandle(outputWrite);
        m_handles.reset();
        return ConPtyBackendHelper::windowsFailure(QStringLiteral("conpty_create_failed"), QStringLiteral("The pseudo-console could not be created"), errorCode);
    }
    CloseHandle(inputRead);
    CloseHandle(outputWrite);

    SIZE_T attributeSize = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attributeSize);
    std::vector<std::byte> attributeStorage(attributeSize);
    auto* attributes = reinterpret_cast<PPROC_THREAD_ATTRIBUTE_LIST>(attributeStorage.data());
    const bool attributesInitialized = InitializeProcThreadAttributeList(attributes, 1, 0, &attributeSize) != FALSE;
    if (!attributesInitialized || !UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE, m_handles->pseudoConsole, sizeof(HPCON), nullptr, nullptr)) {
        const DWORD errorCode = GetLastError();
        if (attributesInitialized) {
            DeleteProcThreadAttributeList(attributes);
        }
        terminate();
        return ConPtyBackendHelper::windowsFailure(QStringLiteral("conpty_attributes_failed"), QStringLiteral("ConPTY process attributes could not be prepared"), errorCode);
    }

    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.lpAttributeList = attributes;
    PROCESS_INFORMATION process{};
    QString command = ConPtyBackendHelper::quoteWindowsArgument(QDir::toNativeSeparators(profile.executable));
    for (const auto& argument : profile.arguments) {
        command += QLatin1Char(' ') + ConPtyBackendHelper::quoteWindowsArgument(argument);
    }
    std::wstring commandLine = command.toStdWString();
    const std::wstring cwd = workingDirectory.toStdWString();
    const BOOL created = CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, FALSE, EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT, nullptr, cwd.c_str(), &startup.StartupInfo, &process);
    DeleteProcThreadAttributeList(attributes);
    if (!created) {
        const DWORD errorCode = GetLastError();
        terminate();
        return ConPtyBackendHelper::windowsFailure(QStringLiteral("conpty_spawn_failed"), QStringLiteral("The shell process could not be created"), errorCode);
    }

    CloseHandle(process.hThread);
    m_handles->process.reset(process.hProcess);
    m_running = true;

    // clang-format off
    m_stopRequested.store(false, std::memory_order_release);
    m_readerThread = std::thread([this]() { readOutput(); });
    m_writerThread = std::thread([this]() { writeInput(); });
    // clang-format on
    return utils::Result<void>::success();
}

utils::Result<void> ConPtyBackend::write(const QByteArray& bytes) {
    if (!running()) {
        return utils::Result<void>::failure({"conpty_not_running", "The terminal process is not running", {}});
    }
    {
        const std::lock_guard lock(m_inputMutex);
        if (bytes.size() > maximumInputQueueSize - m_pendingInput.size()) {
            return utils::Result<void>::failure({"conpty_input_queue_full", "The terminal input queue is full", {}});
        }
        m_pendingInput.append(bytes);
    }
    m_inputCondition.notify_one();
    return utils::Result<void>::success();
}

utils::Result<void> ConPtyBackend::resize(int columns, int rows, int, int) {
    if (!running() || m_handles == nullptr || m_handles->pseudoConsole == nullptr) {
        return utils::Result<void>::failure({"conpty_not_running", "The terminal process is not running", {}});
    }
    if (!validTerminalGrid(columns, rows)) {
        return utils::Result<void>::failure({"conpty_size_invalid", "The pseudo-console dimensions are invalid", QStringLiteral("%1x%2").arg(columns).arg(rows)});
    }

    const COORD size{static_cast<SHORT>(columns), static_cast<SHORT>(rows)};
    if (FAILED(ResizePseudoConsole(m_handles->pseudoConsole, size))) {
        return ConPtyBackendHelper::windowsFailure(QStringLiteral("conpty_resize_failed"), QStringLiteral("The pseudo-console could not be resized"));
    }
    return utils::Result<void>::success();
}

// The reader waits on this under its own lock, so the change is made under that lock and never only announced beside it.
void ConPtyBackend::setOutputPaused(bool paused) {
    {
        const std::lock_guard lock(m_outputMutex);
        m_outputPaused = paused;
    }
    if (!paused) {
        m_outputCondition.notify_all();
    }
}

void ConPtyBackend::terminate() {
    m_stopping = true;
    m_running = false;
    {
        const std::lock_guard lock(m_outputMutex);
        m_outputPaused = false;
        m_stopRequested.store(true, std::memory_order_release);
    }
    m_outputCondition.notify_all();
    if (m_handles == nullptr) {
        return;
    }
    {
        const std::lock_guard lock(m_inputMutex);
        m_stopRequested.store(true, std::memory_order_release);
    }
    m_inputCondition.notify_all();
    if (m_writerThread.joinable()) {
        CancelSynchronousIo(m_writerThread.native_handle());
    }
    if (m_handles->process.get() != nullptr) {
        TerminateProcess(m_handles->process.get(), 1);
    }
    if (m_handles->pseudoConsole != nullptr) {
        ClosePseudoConsole(m_handles->pseudoConsole);
        m_handles->pseudoConsole = nullptr;
    }

    if (m_readerThread.joinable()) {
        CancelSynchronousIo(m_readerThread.native_handle());
        m_readerThread.join();
    }
    if (m_writerThread.joinable()) {
        m_writerThread.join();
    }
    {
        const std::lock_guard lock(m_inputMutex);
        m_pendingInput.clear();
    }
    m_handles.reset();
}

bool ConPtyBackend::running() const {
    return m_running;
}

void ConPtyBackend::readOutput() {
    std::array<char, 64 * 1024> buffer{};
    while (m_running && !m_stopRequested.load(std::memory_order_acquire)) {
        {
            std::unique_lock lock(m_outputMutex);
            // clang-format off
            m_outputCondition.wait(lock, [this]() { return m_stopRequested.load(std::memory_order_acquire) || !m_outputPaused; });
            // clang-format on
        }
        if (m_stopRequested.load(std::memory_order_acquire) || !m_running) {
            break;
        }
        DWORD count = 0;
        if (!ReadFile(m_handles->outputRead.get(), buffer.data(), static_cast<DWORD>(buffer.size()), &count, nullptr)) {
            break;
        }
        if (count > 0) {
            emit outputReady(QByteArray(buffer.data(), static_cast<qsizetype>(count)));
        }
    }

    if (m_handles == nullptr || m_handles->process.get() == nullptr) {
        return;
    }
    WaitForSingleObject(m_handles->process.get(), INFINITE);
    DWORD exitCode = 0;
    GetExitCodeProcess(m_handles->process.get(), &exitCode);
    m_running = false;
    m_inputCondition.notify_all();
    if (!m_stopping) {
        emit processExited(static_cast<int>(exitCode));
    }
}

void ConPtyBackend::writeInput() {
    for (;;) {
        QByteArray batch;
        {
            std::unique_lock lock(m_inputMutex);
            // clang-format off
            m_inputCondition.wait(lock, [this]() { return m_stopRequested.load(std::memory_order_acquire) || !m_running || !m_pendingInput.isEmpty(); });
            // clang-format on
            if (m_stopRequested.load(std::memory_order_acquire) || !m_running) {
                return;
            }
            batch = m_pendingInput;
        }

        qsizetype offset = 0;
        while (offset < batch.size() && !m_stopRequested.load(std::memory_order_acquire)) {
            DWORD written = 0;
            if (!WriteFile(m_handles->inputWrite.get(), batch.constData() + offset, static_cast<DWORD>(batch.size() - offset), &written, nullptr) || written == 0) {
                const DWORD errorCode = GetLastError();
                if (!m_stopping && m_running.exchange(false)) {
                    m_stopping = true;
                    TerminateProcess(m_handles->process.get(), 1);
                    emit backendError(QStringLiteral("Terminal input could not be written: %1").arg(errorCode));
                }
                return;
            }
            offset += static_cast<qsizetype>(written);
        }

        const std::lock_guard lock(m_inputMutex);
        m_pendingInput.remove(0, std::min(offset, m_pendingInput.size()));
    }
}

} // namespace slotdeck::terminalcore
