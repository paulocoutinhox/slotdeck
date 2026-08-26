#include "BuildInfo.h"
#include "app/Application.h"
#include "ui/AppStyle.h"

#include <QApplication>
#include <QFont>
#include <QIcon>
#include <QStyleFactory>

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
    slotdeck::app::Application slotDeckApplication;
    const auto initialization = slotDeckApplication.initialize();

    if (!initialization.hasValue()) {
        qCritical().noquote() << initialization.error().message << initialization.error().detail;
        return 1;
    }

    const auto interfaceResult = slotDeckApplication.loadInterface();

    if (!interfaceResult.hasValue()) {
        qCritical().noquote() << interfaceResult.error().message << interfaceResult.error().detail;
        return 1;
    }

    // The window is already on screen, so everything the reader waits for runs from the event loop it is being painted by.
    // clang-format off
    const auto completeStartup = [&slotDeckApplication]() {
        const auto startup = slotDeckApplication.completeStartup();
        if (!startup.hasValue()) {
            qCritical().noquote() << startup.error().message << startup.error().detail;
            QCoreApplication::exit(1);
        }
    };
    QMetaObject::invokeMethod(&slotDeckApplication, completeStartup, Qt::QueuedConnection);
    // clang-format on

    QObject::connect(&application, &QCoreApplication::aboutToQuit, &slotDeckApplication, &slotdeck::app::Application::shutdown);
    return application.exec();
}
