#include "BuildInfo.h"

#include <QApplication>
#include <QFontDatabase>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

#include <gtest/gtest.h>

#ifdef Q_OS_WIN
#include <fcntl.h>
#include <io.h>
#endif

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>

QString languageServerDocumentUri;

enum class LanguageServerFixture { Normal, InvalidProtocol, Crashing, Silent, SlowStart, Malformed, DeepOutline };

class TestMainHelper final {
  public:
    static bool hasArgument(int argc, char** argv, std::string_view expected);
    static void sendLanguageServerMessage(const QJsonObject& message);
    static void sendLanguageServerDiagnostic(const QString& message);
    static int runMcpServerFixture();
    static int runLanguageServerFixture(LanguageServerFixture mode, int shape = 0);
    static int argumentValue(int argc, char** argv, std::string_view expected);
    static void writeMalformedLanguageServerFrame(int shape);
    static void writeMalformedMcpLine(int shape);
};

bool TestMainHelper::hasArgument(int argc, char** argv, std::string_view expected) {
    for (int index = 1; index < argc; ++index) {
        if (argv[index] == expected) {
            return true;
        }
    }

    return false;
}

int TestMainHelper::argumentValue(int argc, char** argv, std::string_view expected) {
    for (int index = 1; index + 1 < argc; ++index) {
        if (argv[index] == expected) {
            return std::atoi(argv[index + 1]);
        }
    }

    return 0;
}

// Every shape is one way a server can frame a message wrongly, written on purpose so the transport is proven against it.
void TestMainHelper::writeMalformedLanguageServerFrame(int shape) {
    const QByteArray body = QByteArrayLiteral(R"({"jsonrpc":"2.0","method":"window/logMessage","params":{"type":3,"message":"framed"}})");
    switch (shape) {
    case 0:
        std::cout << "\r\n\r\n";
        break;
    case 1:
        std::cout << "Content-Length: 12\r\nContent-Length: 12\r\n\r\n";
        std::cout.write(body.constData(), 12);
        break;
    case 2:
        std::cout << "Content-Length: notanumber\r\n\r\n";
        break;
    case 3:
        std::cout << "Content-Length: -5\r\n\r\n";
        break;
    case 4:
        std::cout << "Content-Length: 99999999999\r\n\r\n";
        break;
    case 5:
        std::cout << "Content-Length: " << body.size() + 4096 << "\r\n\r\n";
        std::cout.write(body.constData(), body.size());
        break;
    case 6:
        std::cout << "Content-Length: 4\r\n\r\n";
        std::cout.write(body.constData(), body.size());
        break;
    case 7:
        std::cout << "Content-Length: 7\r\n\r\nnotjson";
        break;
    case 8:
        std::cout << "Content-Length: 2\r\n\r\n[]";
        break;
    case 9:
        std::cout << "Content-Length: 21\r\n\r\n{\"jsonrpc\":\"1.0\",\"a\":1}";
        break;
    case 10:
        std::cout << "Content-Length: 4\r\n\r\n";
        std::cout.write("\0\0\0\0", 4);
        break;
    default:
        std::cout << std::string(70000, 'h') << "\r\n\r\n";
        break;
    }
    std::cout.flush();
}

void TestMainHelper::writeMalformedMcpLine(int shape) {
    switch (shape) {
    case 0:
        std::cout << "\n";
        break;
    case 1:
        std::cout << "not json at all\n";
        break;
    case 2:
        std::cout << "[]\n";
        break;
    case 3:
        std::cout << "{}\n";
        break;
    case 4:
        std::cout << R"({"jsonrpc":"1.0","id":1,"result":{}})" << "\n";
        break;
    case 5:
        std::cout << R"({"jsonrpc":"2.0","id":9999,"result":{}})" << "\n";
        break;
    case 6:
        std::cout.write("\0\0\0\0", 4);
        std::cout << "\n";
        break;
    default:
        std::cout << std::string(2000000, 'x') << "\n";
        break;
    }
    std::cout.flush();
}

void TestMainHelper::sendLanguageServerMessage(const QJsonObject& message) {
    const QByteArray payload = QJsonDocument(message).toJson(QJsonDocument::Compact);
    std::cout << "Content-Length: " << payload.size() << "\r\n\r\n";
    std::cout.write(payload.constData(), static_cast<std::streamsize>(payload.size()));
    std::cout.flush();
}

void TestMainHelper::sendLanguageServerDiagnostic(const QString& message) {
    const QJsonObject range{{QStringLiteral("start"), QJsonObject{{QStringLiteral("line"), 0}, {QStringLiteral("character"), 0}}}, {QStringLiteral("end"), QJsonObject{{QStringLiteral("line"), 0}, {QStringLiteral("character"), 3}}}};
    TestMainHelper::sendLanguageServerMessage({{QStringLiteral("jsonrpc"), QStringLiteral("2.0")}, {QStringLiteral("method"), QStringLiteral("textDocument/publishDiagnostics")}, {QStringLiteral("params"), QJsonObject{{QStringLiteral("uri"), languageServerDocumentUri}, {QStringLiteral("diagnostics"), QJsonArray{QJsonObject{{QStringLiteral("range"), range}, {QStringLiteral("severity"), 2}, {QStringLiteral("message"), message}}}}}}});
}

int TestMainHelper::runMcpServerFixture() {
    std::string line;

    while (std::getline(std::cin, line)) {
        if (line.empty()) {
            continue;
        }

        const QJsonObject message = QJsonDocument::fromJson(QByteArray::fromStdString(line)).object();
        const QString method = message.value(QStringLiteral("method")).toString();
        if (!message.contains(QStringLiteral("id"))) {
            continue;
        }

        QJsonObject result;
        if (method == QStringLiteral("initialize")) {
            result = QJsonObject{{QStringLiteral("protocolVersion"), QStringLiteral("2025-06-18")}, {QStringLiteral("capabilities"), QJsonObject{{QStringLiteral("tools"), QJsonObject{{QStringLiteral("listChanged"), true}}}, {QStringLiteral("resources"), QJsonObject{}}}}, {QStringLiteral("serverInfo"), QJsonObject{{QStringLiteral("name"), QStringLiteral("fixture")}, {QStringLiteral("version"), QStringLiteral("1.0")}}}};
        } else if (method == QStringLiteral("tools/list")) {
            const QJsonObject schema{{QStringLiteral("type"), QStringLiteral("object")}, {QStringLiteral("properties"), QJsonObject{{QStringLiteral("city"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}}}}};
            result = QJsonObject{{QStringLiteral("tools"), QJsonArray{QJsonObject{{QStringLiteral("name"), QStringLiteral("get_weather")}, {QStringLiteral("description"), QStringLiteral("Weather for a city")}, {QStringLiteral("inputSchema"), schema}}}}};
        } else if (method == QStringLiteral("tools/call")) {
            const QString city = message.value(QStringLiteral("params")).toObject().value(QStringLiteral("arguments")).toObject().value(QStringLiteral("city")).toString();
            result = QJsonObject{{QStringLiteral("content"), QJsonArray{QJsonObject{{QStringLiteral("type"), QStringLiteral("text")}, {QStringLiteral("text"), QStringLiteral("sunny in ") + city}}}}, {QStringLiteral("isError"), false}};
        } else if (method == QStringLiteral("resources/list")) {
            result = QJsonObject{{QStringLiteral("resources"), QJsonArray{QJsonObject{{QStringLiteral("uri"), QStringLiteral("file:///fixture/readme.md")}, {QStringLiteral("name"), QStringLiteral("readme")}, {QStringLiteral("description"), QStringLiteral("Fixture readme")}}}}};
        } else if (method == QStringLiteral("resources/read")) {
            const QString uri = message.value(QStringLiteral("params")).toObject().value(QStringLiteral("uri")).toString();
            result = QJsonObject{{QStringLiteral("contents"), QJsonArray{QJsonObject{{QStringLiteral("uri"), uri}, {QStringLiteral("mimeType"), QStringLiteral("text/markdown")}, {QStringLiteral("text"), QStringLiteral("the fixture readme body")}}}}};
        } else if (method == QStringLiteral("prompts/list")) {
            result = QJsonObject{{QStringLiteral("prompts"), QJsonArray{QJsonObject{{QStringLiteral("name"), QStringLiteral("review")}, {QStringLiteral("description"), QStringLiteral("Review a change")}}}}};
        } else if (method == QStringLiteral("prompts/get")) {
            const QString name = message.value(QStringLiteral("params")).toObject().value(QStringLiteral("name")).toString();
            result = QJsonObject{{QStringLiteral("messages"), QJsonArray{QJsonObject{{QStringLiteral("role"), QStringLiteral("user")}, {QStringLiteral("content"), QJsonObject{{QStringLiteral("type"), QStringLiteral("text")}, {QStringLiteral("text"), QStringLiteral("run the ") + name}}}}}}};
        } else if (method == QStringLiteral("ping")) {
            result = QJsonObject{};
        } else if (method == QStringLiteral("probe/client")) {
            std::cout << QJsonDocument(QJsonObject{{QStringLiteral("jsonrpc"), QStringLiteral("2.0")}, {QStringLiteral("method"), QStringLiteral("notifications/progress")}, {QStringLiteral("params"), QJsonObject{{QStringLiteral("progressToken"), QStringLiteral("t1")}, {QStringLiteral("progress"), 40.0}, {QStringLiteral("total"), 100.0}, {QStringLiteral("message"), QStringLiteral("halfway")}}}}).toJson(QJsonDocument::Compact).toStdString() << std::endl;
            std::cout << QJsonDocument(QJsonObject{{QStringLiteral("jsonrpc"), QStringLiteral("2.0")}, {QStringLiteral("id"), 9001}, {QStringLiteral("method"), QStringLiteral("roots/list")}}).toJson(QJsonDocument::Compact).toStdString() << std::endl;
            std::cout << QJsonDocument(QJsonObject{{QStringLiteral("jsonrpc"), QStringLiteral("2.0")}, {QStringLiteral("id"), 9002}, {QStringLiteral("method"), QStringLiteral("sampling/createMessage")}, {QStringLiteral("params"), QJsonObject{{QStringLiteral("maxTokens"), 64}}}}).toJson(QJsonDocument::Compact).toStdString() << std::endl;
            result = QJsonObject{};
        } else {
            const QJsonObject error{{QStringLiteral("code"), -32601}, {QStringLiteral("message"), QStringLiteral("Method not found")}};
            std::cout << QJsonDocument(QJsonObject{{QStringLiteral("jsonrpc"), QStringLiteral("2.0")}, {QStringLiteral("id"), message.value(QStringLiteral("id"))}, {QStringLiteral("error"), error}}).toJson(QJsonDocument::Compact).toStdString() << std::endl;
            continue;
        }
        std::cout << QJsonDocument(QJsonObject{{QStringLiteral("jsonrpc"), QStringLiteral("2.0")}, {QStringLiteral("id"), message.value(QStringLiteral("id"))}, {QStringLiteral("result"), result}}).toJson(QJsonDocument::Compact).toStdString() << std::endl;
    }

    return 0;
}

int TestMainHelper::runLanguageServerFixture(LanguageServerFixture mode, int shape) {
#ifdef Q_OS_WIN
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif

    while (std::cin.good()) {
        std::string header;
        qsizetype contentLength = -1;
        while (std::getline(std::cin, header) && header != "\r") {
            constexpr std::string_view prefix = "Content-Length:";
            if (header.starts_with(prefix)) {
                contentLength = static_cast<qsizetype>(std::stoll(header.substr(prefix.size())));
            }
        }
        if (contentLength < 0) {
            return 2;
        }

        QByteArray payload(contentLength, Qt::Uninitialized);
        std::cin.read(payload.data(), static_cast<std::streamsize>(contentLength));
        const QJsonObject message = QJsonDocument::fromJson(payload).object();
        const QString method = message.value(QStringLiteral("method")).toString();
        if (method.isEmpty()) {
            if (message.value(QStringLiteral("id")).toInt() == 1 && message.contains(QStringLiteral("result"))) {
                TestMainHelper::sendLanguageServerDiagnostic(QStringLiteral("server request answered"));
            }
            continue;
        }
        if (method == QStringLiteral("initialize")) {
            if (mode == LanguageServerFixture::Malformed) {
                TestMainHelper::writeMalformedLanguageServerFrame(shape);
                continue;
            }

            if (mode == LanguageServerFixture::Silent) {
                continue;
            }
            if (mode == LanguageServerFixture::InvalidProtocol) {
                std::cout << "Invalid: response\r\n\r\n{}";
                std::cout.flush();
                continue;
            }
            // A server that answers after the analysis debounce proves that what a document asked for too early is asked for again.
            if (mode == LanguageServerFixture::SlowStart) {
                std::this_thread::sleep_for(std::chrono::milliseconds(800));
            }
            std::cerr << "I[07:00:06.485] ASTWorker building file main.cpp version 1" << std::endl;
            const QJsonObject synchronization{{QStringLiteral("openClose"), true}, {QStringLiteral("change"), 2}, {QStringLiteral("save"), QJsonObject{{QStringLiteral("includeText"), true}}}};
            const QJsonObject completion{{QStringLiteral("triggerCharacters"), QJsonArray{QStringLiteral(".")}}};
            const QJsonObject semanticTokens{{QStringLiteral("legend"), QJsonObject{{QStringLiteral("tokenTypes"), QJsonArray{QStringLiteral("namespace"), QStringLiteral("function")}}, {QStringLiteral("tokenModifiers"), QJsonArray{}}}}, {QStringLiteral("full"), true}};
            const QJsonObject capabilities{{QStringLiteral("textDocumentSync"), synchronization}, {QStringLiteral("completionProvider"), completion}, {QStringLiteral("definitionProvider"), true}, {QStringLiteral("declarationProvider"), true}, {QStringLiteral("typeDefinitionProvider"), true}, {QStringLiteral("implementationProvider"), true}, {QStringLiteral("referencesProvider"), true}, {QStringLiteral("hoverProvider"), true}, {QStringLiteral("signatureHelpProvider"), QJsonObject{{QStringLiteral("triggerCharacters"), QJsonArray{QStringLiteral("(")}}}}, {QStringLiteral("documentHighlightProvider"), true}, {QStringLiteral("documentSymbolProvider"), true}, {QStringLiteral("workspaceSymbolProvider"), true}, {QStringLiteral("semanticTokensProvider"), semanticTokens}, {QStringLiteral("diagnosticProvider"), QJsonObject{{QStringLiteral("interFileDependencies"), false}}}};
            TestMainHelper::sendLanguageServerMessage({{QStringLiteral("jsonrpc"), QStringLiteral("2.0")}, {QStringLiteral("id"), message.value(QStringLiteral("id"))}, {QStringLiteral("result"), QJsonObject{{QStringLiteral("capabilities"), capabilities}}}});
        } else if (method == QStringLiteral("initialized")) {
            TestMainHelper::sendLanguageServerMessage({{QStringLiteral("jsonrpc"), QStringLiteral("2.0")}, {QStringLiteral("id"), 1}, {QStringLiteral("method"), QStringLiteral("window/workDoneProgress/create")}, {QStringLiteral("params"), QJsonObject{{QStringLiteral("token"), QStringLiteral("indexing")}}}});
            TestMainHelper::sendLanguageServerMessage({{QStringLiteral("jsonrpc"), QStringLiteral("2.0")}, {QStringLiteral("id"), 900}, {QStringLiteral("method"), QStringLiteral("workspace/configuration")}, {QStringLiteral("params"), QJsonObject{{QStringLiteral("items"), QJsonArray{QJsonObject{{QStringLiteral("section"), QStringLiteral("fixture")}}}}}}});
        } else if (method == QStringLiteral("textDocument/didChange")) {
            TestMainHelper::sendLanguageServerDiagnostic(QString::fromUtf8(QJsonDocument(message.value(QStringLiteral("params")).toObject().value(QStringLiteral("contentChanges")).toArray()).toJson(QJsonDocument::Compact)));
        } else if (method == QStringLiteral("textDocument/didSave")) {
            TestMainHelper::sendLanguageServerMessage({{QStringLiteral("jsonrpc"), QStringLiteral("2.0")}, {QStringLiteral("method"), QStringLiteral("window/logMessage")}, {QStringLiteral("params"), QJsonObject{{QStringLiteral("type"), 3}, {QStringLiteral("message"), QStringLiteral("saved ") + message.value(QStringLiteral("params")).toObject().value(QStringLiteral("text")).toString()}}}});
            TestMainHelper::sendLanguageServerMessage({{QStringLiteral("jsonrpc"), QStringLiteral("2.0")}, {QStringLiteral("method"), QStringLiteral("window/showMessage")}, {QStringLiteral("params"), QJsonObject{{QStringLiteral("type"), 1}, {QStringLiteral("message"), QStringLiteral("fixture failure")}}}});
        } else if (method == QStringLiteral("textDocument/definition")) {
            const QJsonObject range{{QStringLiteral("start"), QJsonObject{{QStringLiteral("line"), 7}, {QStringLiteral("character"), 2}}}, {QStringLiteral("end"), QJsonObject{{QStringLiteral("line"), 7}, {QStringLiteral("character"), 6}}}};
            TestMainHelper::sendLanguageServerMessage({{QStringLiteral("jsonrpc"), QStringLiteral("2.0")}, {QStringLiteral("id"), message.value(QStringLiteral("id"))}, {QStringLiteral("result"), QJsonArray{QJsonObject{{QStringLiteral("uri"), message.value(QStringLiteral("params")).toObject().value(QStringLiteral("textDocument")).toObject().value(QStringLiteral("uri"))}, {QStringLiteral("range"), range}}}}});
        } else if (method == QStringLiteral("textDocument/references")) {
            const QJsonObject range{{QStringLiteral("start"), QJsonObject{{QStringLiteral("line"), 3}, {QStringLiteral("character"), 1}}}, {QStringLiteral("end"), QJsonObject{{QStringLiteral("line"), 3}, {QStringLiteral("character"), 5}}}};
            TestMainHelper::sendLanguageServerMessage({{QStringLiteral("jsonrpc"), QStringLiteral("2.0")}, {QStringLiteral("id"), message.value(QStringLiteral("id"))}, {QStringLiteral("result"), QJsonArray{QJsonObject{{QStringLiteral("uri"), languageServerDocumentUri}, {QStringLiteral("range"), range}}}}});
        } else if (method == QStringLiteral("textDocument/documentSymbol")) {
            if (mode == LanguageServerFixture::DeepOutline) {
                const QJsonObject range{{QStringLiteral("start"), QJsonObject{{QStringLiteral("line"), 0}, {QStringLiteral("character"), 0}}}, {QStringLiteral("end"), QJsonObject{{QStringLiteral("line"), 0}, {QStringLiteral("character"), 1}}}};
                QJsonObject nested{{QStringLiteral("name"), QStringLiteral("leaf")}, {QStringLiteral("kind"), 13}, {QStringLiteral("range"), range}, {QStringLiteral("selectionRange"), range}};
                for (int level = 0; level < 400; ++level) {
                    nested = QJsonObject{{QStringLiteral("name"), QStringLiteral("level")}, {QStringLiteral("kind"), 12}, {QStringLiteral("range"), range}, {QStringLiteral("selectionRange"), range}, {QStringLiteral("children"), QJsonArray{nested}}};
                }
                TestMainHelper::sendLanguageServerMessage({{QStringLiteral("jsonrpc"), QStringLiteral("2.0")}, {QStringLiteral("id"), message.value(QStringLiteral("id"))}, {QStringLiteral("result"), QJsonArray{nested}}});
                continue;
            }

            const QJsonObject range{{QStringLiteral("start"), QJsonObject{{QStringLiteral("line"), 0}, {QStringLiteral("character"), 4}}}, {QStringLiteral("end"), QJsonObject{{QStringLiteral("line"), 0}, {QStringLiteral("character"), 8}}}};
            const QJsonObject child{{QStringLiteral("name"), QStringLiteral("value")}, {QStringLiteral("kind"), 13}, {QStringLiteral("range"), range}, {QStringLiteral("selectionRange"), range}};
            TestMainHelper::sendLanguageServerMessage({{QStringLiteral("jsonrpc"), QStringLiteral("2.0")}, {QStringLiteral("id"), message.value(QStringLiteral("id"))}, {QStringLiteral("result"), QJsonArray{QJsonObject{{QStringLiteral("name"), QStringLiteral("main")}, {QStringLiteral("detail"), QStringLiteral("int ()")}, {QStringLiteral("kind"), 12}, {QStringLiteral("range"), range}, {QStringLiteral("selectionRange"), range}, {QStringLiteral("children"), QJsonArray{child}}}}}});
        } else if (method == QStringLiteral("workspace/symbol")) {
            const QJsonObject range{{QStringLiteral("start"), QJsonObject{{QStringLiteral("line"), 5}, {QStringLiteral("character"), 2}}}, {QStringLiteral("end"), QJsonObject{{QStringLiteral("line"), 5}, {QStringLiteral("character"), 6}}}};
            TestMainHelper::sendLanguageServerMessage({{QStringLiteral("jsonrpc"), QStringLiteral("2.0")}, {QStringLiteral("id"), message.value(QStringLiteral("id"))}, {QStringLiteral("result"), QJsonArray{QJsonObject{{QStringLiteral("name"), QStringLiteral("main")}, {QStringLiteral("containerName"), QStringLiteral("fixture")}, {QStringLiteral("kind"), 12}, {QStringLiteral("location"), QJsonObject{{QStringLiteral("uri"), languageServerDocumentUri}, {QStringLiteral("range"), range}}}}}}});
        } else if (method == QStringLiteral("textDocument/documentHighlight")) {
            const QJsonObject range{{QStringLiteral("start"), QJsonObject{{QStringLiteral("line"), 0}, {QStringLiteral("character"), 4}}}, {QStringLiteral("end"), QJsonObject{{QStringLiteral("line"), 0}, {QStringLiteral("character"), 8}}}};
            TestMainHelper::sendLanguageServerMessage({{QStringLiteral("jsonrpc"), QStringLiteral("2.0")}, {QStringLiteral("id"), message.value(QStringLiteral("id"))}, {QStringLiteral("result"), QJsonArray{QJsonObject{{QStringLiteral("range"), range}, {QStringLiteral("kind"), 1}}}}});
        } else if (method == QStringLiteral("textDocument/semanticTokens/full")) {
            TestMainHelper::sendLanguageServerMessage({{QStringLiteral("jsonrpc"), QStringLiteral("2.0")}, {QStringLiteral("id"), message.value(QStringLiteral("id"))}, {QStringLiteral("result"), QJsonObject{{QStringLiteral("data"), QJsonArray{0, 4, 4, 1, 0, 1, 2, 3, 0, 0}}}}});
        } else if (method == QStringLiteral("textDocument/diagnostic")) {
            const QJsonObject range{{QStringLiteral("start"), QJsonObject{{QStringLiteral("line"), 0}, {QStringLiteral("character"), 0}}}, {QStringLiteral("end"), QJsonObject{{QStringLiteral("line"), 0}, {QStringLiteral("character"), 3}}}};
            TestMainHelper::sendLanguageServerMessage({{QStringLiteral("jsonrpc"), QStringLiteral("2.0")}, {QStringLiteral("id"), message.value(QStringLiteral("id"))}, {QStringLiteral("result"), QJsonObject{{QStringLiteral("kind"), QStringLiteral("full")}, {QStringLiteral("items"), QJsonArray{QJsonObject{{QStringLiteral("range"), range}, {QStringLiteral("severity"), 1}, {QStringLiteral("message"), QStringLiteral("pulled diagnostic")}}}}}}});
        } else if (method == QStringLiteral("textDocument/signatureHelp")) {
            TestMainHelper::sendLanguageServerMessage({{QStringLiteral("jsonrpc"), QStringLiteral("2.0")}, {QStringLiteral("id"), message.value(QStringLiteral("id"))}, {QStringLiteral("result"), QJsonObject{{QStringLiteral("signatures"), QJsonArray{QJsonObject{{QStringLiteral("label"), QStringLiteral("main(int argc, char** argv)")}}}}, {QStringLiteral("activeSignature"), 0}, {QStringLiteral("activeParameter"), 1}}}});
        } else if (method == QStringLiteral("textDocument/hover")) {
            TestMainHelper::sendLanguageServerMessage({{QStringLiteral("jsonrpc"), QStringLiteral("2.0")}, {QStringLiteral("id"), message.value(QStringLiteral("id"))}, {QStringLiteral("result"), QJsonObject{{QStringLiteral("contents"), QJsonObject{{QStringLiteral("kind"), QStringLiteral("plaintext")}, {QStringLiteral("value"), QStringLiteral("int main()")}}}}}});
        } else if (method == QStringLiteral("textDocument/didOpen")) {
            if (mode == LanguageServerFixture::Crashing) {
                return 3;
            }
            const QString uri = message.value(QStringLiteral("params")).toObject().value(QStringLiteral("textDocument")).toObject().value(QStringLiteral("uri")).toString();
            languageServerDocumentUri = uri;
            // A real server reports on the files it pulled in as well, so the fixture names one nobody opened.
            const QString includedUri = uri.left(uri.lastIndexOf(QLatin1Char('/'))) + QStringLiteral("/included.h");
            TestMainHelper::sendLanguageServerMessage({{QStringLiteral("jsonrpc"), QStringLiteral("2.0")}, {QStringLiteral("method"), QStringLiteral("textDocument/publishDiagnostics")}, {QStringLiteral("params"), QJsonObject{{QStringLiteral("uri"), includedUri}, {QStringLiteral("diagnostics"), QJsonArray{QJsonObject{{QStringLiteral("range"), QJsonObject{{QStringLiteral("start"), QJsonObject{{QStringLiteral("line"), 6}, {QStringLiteral("character"), 0}}}, {QStringLiteral("end"), QJsonObject{{QStringLiteral("line"), 6}, {QStringLiteral("character"), 3}}}}}, {QStringLiteral("severity"), 1}, {QStringLiteral("message"), QStringLiteral("Fixture header diagnostic")}}}}}}});
            TestMainHelper::sendLanguageServerMessage({{QStringLiteral("jsonrpc"), QStringLiteral("2.0")}, {QStringLiteral("method"), QStringLiteral("textDocument/publishDiagnostics")}, {QStringLiteral("params"), QJsonObject{{QStringLiteral("uri"), uri}, {QStringLiteral("diagnostics"), QJsonArray{QJsonObject{{QStringLiteral("range"), QJsonObject{{QStringLiteral("start"), QJsonObject{{QStringLiteral("line"), 0}, {QStringLiteral("character"), 0}}}, {QStringLiteral("end"), QJsonObject{{QStringLiteral("line"), 0}, {QStringLiteral("character"), 3}}}}}, {QStringLiteral("severity"), 2}, {QStringLiteral("message"), QStringLiteral("Fixture diagnostic")}}}}}}});
        } else if (method == QStringLiteral("textDocument/completion")) {
            const QJsonObject range{{QStringLiteral("start"), QJsonObject{{QStringLiteral("line"), 0}, {QStringLiteral("character"), 4}}}, {QStringLiteral("end"), QJsonObject{{QStringLiteral("line"), 0}, {QStringLiteral("character"), 6}}}};
            const QJsonObject edited{{QStringLiteral("label"), QStringLiteral("push_back(const value_type &value)")}, {QStringLiteral("sortText"), QStringLiteral("0000")}, {QStringLiteral("textEdit"), QJsonObject{{QStringLiteral("range"), range}, {QStringLiteral("newText"), QStringLiteral("push_back")}}}};
            const QJsonObject plain{{QStringLiteral("label"), QStringLiteral("completionItem")}, {QStringLiteral("sortText"), QStringLiteral("0001")}, {QStringLiteral("insertText"), QStringLiteral("completion_item")}};
            TestMainHelper::sendLanguageServerMessage({{QStringLiteral("jsonrpc"), QStringLiteral("2.0")}, {QStringLiteral("id"), message.value(QStringLiteral("id"))}, {QStringLiteral("result"), QJsonArray{plain, edited}}});
        } else if (method == QStringLiteral("shutdown")) {
            TestMainHelper::sendLanguageServerMessage({{QStringLiteral("jsonrpc"), QStringLiteral("2.0")}, {QStringLiteral("id"), message.value(QStringLiteral("id"))}, {QStringLiteral("result"), QJsonValue::Null}});
        } else if (method == QStringLiteral("exit")) {
            return 0;
        }
    }

    return 0;
}

int main(int argc, char** argv) {
    if (TestMainHelper::hasArgument(argc, argv, "--slotdeck-test-await-stdin")) {
        char input{};
        return std::cin.get(input) ? 1 : 0;
    }

    if (TestMainHelper::hasArgument(argc, argv, "--slotdeck-test-hang")) {
        std::this_thread::sleep_for(std::chrono::seconds(30));
        return 0;
    }

    if (TestMainHelper::hasArgument(argc, argv, "--slotdeck-test-mcp")) {
        return TestMainHelper::runMcpServerFixture();
    }
    if (TestMainHelper::hasArgument(argc, argv, "--slotdeck-test-lsp")) {
        return TestMainHelper::runLanguageServerFixture(LanguageServerFixture::Normal);
    }
    if (TestMainHelper::hasArgument(argc, argv, "--slotdeck-test-lsp-invalid")) {
        return TestMainHelper::runLanguageServerFixture(LanguageServerFixture::InvalidProtocol);
    }
    if (TestMainHelper::hasArgument(argc, argv, "--slotdeck-test-lsp-crash")) {
        return TestMainHelper::runLanguageServerFixture(LanguageServerFixture::Crashing);
    }
    if (TestMainHelper::hasArgument(argc, argv, "--slotdeck-test-lsp-silent")) {
        return TestMainHelper::runLanguageServerFixture(LanguageServerFixture::Silent);
    }
    if (TestMainHelper::hasArgument(argc, argv, "--slotdeck-test-lsp-slow-start")) {
        return TestMainHelper::runLanguageServerFixture(LanguageServerFixture::SlowStart);
    }
    if (TestMainHelper::hasArgument(argc, argv, "--slotdeck-test-lsp-deep-outline")) {
        return TestMainHelper::runLanguageServerFixture(LanguageServerFixture::DeepOutline);
    }
    if (TestMainHelper::hasArgument(argc, argv, "--slotdeck-test-lsp-malformed")) {
        return TestMainHelper::runLanguageServerFixture(LanguageServerFixture::Malformed, TestMainHelper::argumentValue(argc, argv, "--slotdeck-test-lsp-malformed"));
    }
    if (TestMainHelper::hasArgument(argc, argv, "--slotdeck-test-mcp-malformed")) {
        TestMainHelper::writeMalformedMcpLine(TestMainHelper::argumentValue(argc, argv, "--slotdeck-test-mcp-malformed"));
        std::this_thread::sleep_for(std::chrono::seconds(5));
        return 0;
    }

    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
    }

    QStandardPaths::setTestModeEnabled(true);
    QApplication application(argc, argv);
    QCoreApplication::setApplicationVersion(QStringLiteral(SLOTDECK_APP_VERSION));
#ifdef Q_OS_MACOS
    application.setFont(QFont(QStringLiteral(".AppleSystemUIFont")));
#else
    application.setFont(QFontDatabase::systemFont(QFontDatabase::GeneralFont));
#endif
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
