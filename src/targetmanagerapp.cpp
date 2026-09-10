#include "targetmanagerwindow.h"
#include <QApplication>
#include <QCommandLineParser>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("plasma-share-uploader"));
    QCoreApplication::setApplicationName(QStringLiteral("plasma-share-uploader-config"));
    QCoreApplication::setApplicationVersion(QStringLiteral(PLASMA_SHARE_UPLOADER_VERSION));
    QApplication::setDesktopFileName(QStringLiteral("org.kde.plasma-share-uploader.config"));
    QCommandLineParser parser; parser.setApplicationDescription(QStringLiteral("Configure Plasma Share upload targets"));
    parser.addHelpOption(); parser.addVersionOption();
    parser.addOption({QStringLiteral("targets-dir"), QStringLiteral("Use an alternate active target directory"), QStringLiteral("directory")});
    parser.addOption({QStringLiteral("presets-dir"), QStringLiteral("Use an alternate packaged preset directory"), QStringLiteral("directory")});
    parser.process(app);
    TargetManagerWindow window(parser.value(QStringLiteral("presets-dir")), parser.value(QStringLiteral("targets-dir")));
    window.show(); return app.exec();
}
