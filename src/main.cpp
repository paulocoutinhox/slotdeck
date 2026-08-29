#include "BuildInfo.h"
#include "app/Application.h"
#include "domain/ApplicationLanguage.h"
#include "plugins/CoreTranslations.h"
#include "ui/AppStyle.h"
#include "utils/Result.h"

#include <QApplication>
#include <QFont>
#include <QIcon>
#include <QLocale>
#include <QMessageBox>
#include <QStyleFactory>

namespace slotdeck {

class MainHelper final {
  public:
    static int reportStartupFailure(const utils::Error& error);
};

// A windowed application has no console to write to, so a failure that only reaches the log leaves the reader with a window that never opens.
int MainHelper::reportStartupFailure(const utils::Error& error) {
    qCritical().noquote() << error.message << error.detail;
    const plugins::TranslationEntries entries = plugins::coretranslations::catalog().value(domain::resolveApplicationLanguage(QLocale::system().name()));
    QMessageBox failure(QMessageBox::Critical, entries.value(QStringLiteral("slotdeck.startup.failed-title")), entries.value(QStringLiteral("slotdeck.startup.failed-message")));
    failure.setDetailedText(error.detail.isEmpty() ? error.message : error.message + QLatin1Char('\n') + error.detail);
    failure.exec();
    return 1;
}

} // namespace slotdeck

int main(int argc, char* argv[]) {
    QApplication::setAttribute(Qt::AA_DontShowIconsInMenus, false);
    QApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral(SLOTDECK_PRODUCT_NAME));
    QCoreApplication::setApplicationVersion(QStringLiteral(SLOTDECK_APP_VERSION));
    QCoreApplication::setOrganizationName(QStringLiteral(SLOTDECK_ORGANIZATION_NAME));
    QCoreApplication::setOrganizationDomain(QStringLiteral(SLOTDECK_ORGANIZATION_DOMAIN));
    application.setWindowIcon(QIcon(QStringLiteral(":/images/logo.png")));
#ifdef Q_OS_MACOS
    application.setFont(QFont(QStringLiteral(".AppleSystemUIFont")));
#endif
    QApplication::setStyle(new slotdeck::ui::AppStyle(QStyleFactory::create("Fusion")));
    QApplication::setPalette(slotdeck::ui::AppStyle::applicationPalette());
    const auto dataPath = slotdeck::app::Application::resolveDataPath(QCoreApplication::arguments());

    if (!dataPath.hasValue()) {
        return slotdeck::MainHelper::reportStartupFailure(dataPath.error());
    }

    slotdeck::app::Application slotDeckApplication(dataPath.value(), nullptr);
    const auto initialization = slotDeckApplication.initialize();

    if (!initialization.hasValue()) {
        return slotdeck::MainHelper::reportStartupFailure(initialization.error());
    }

    const auto interfaceResult = slotDeckApplication.loadInterface();

    if (!interfaceResult.hasValue()) {
        return slotdeck::MainHelper::reportStartupFailure(interfaceResult.error());
    }

    // The window is already on screen, so everything the reader waits for runs from the event loop it is being painted by.
    // clang-format off
    const auto completeStartup = [&slotDeckApplication]() {
        const auto startup = slotDeckApplication.completeStartup();
        if (!startup.hasValue()) {
            QCoreApplication::exit(slotdeck::MainHelper::reportStartupFailure(startup.error()));
        }
    };
    QMetaObject::invokeMethod(&slotDeckApplication, completeStartup, Qt::QueuedConnection);
    // clang-format on

    QObject::connect(&application, &QCoreApplication::aboutToQuit, &slotDeckApplication, &slotdeck::app::Application::shutdown);
    return application.exec();
}
