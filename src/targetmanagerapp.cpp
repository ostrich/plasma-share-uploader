#include "targetmanagercontroller.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QQmlApplicationEngine>
#include <QtQml/QQmlExtensionPlugin>

Q_IMPORT_QML_PLUGIN(ShareUploaderPlugin)

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("plasma-share-uploader"));
    QCoreApplication::setApplicationName(QStringLiteral("plasma-share-uploader-config"));
    QApplication::setApplicationDisplayName(QObject::tr("Plasma Share Uploader Settings"));
    QCoreApplication::setApplicationVersion(QStringLiteral(PLASMA_SHARE_UPLOADER_VERSION));
    QApplication::setDesktopFileName(QStringLiteral("org.kde.plasma-share-uploader.config"));
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Configure Plasma Share Uploader"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({ QStringLiteral("targets-dir"), QStringLiteral("Use an alternate active target directory"),
        QStringLiteral("directory") });
    parser.addOption({ QStringLiteral("presets-dir"), QStringLiteral("Use an alternate packaged preset directory"),
        QStringLiteral("directory") });
    parser.process(app);
    TargetManagerController controller(
        parser.value(QStringLiteral("presets-dir")), parser.value(QStringLiteral("targets-dir")));
    QQmlApplicationEngine engine;
    engine.setInitialProperties({ { QStringLiteral("controller"), QVariant::fromValue(&controller) } });
    engine.loadFromModule(QStringLiteral("ShareUploader"), QStringLiteral("ManagerWindow"));
    if (engine.rootObjects().isEmpty())
        return 1;
    return app.exec();
}
