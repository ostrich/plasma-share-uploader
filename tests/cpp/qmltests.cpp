#include "uifixture.h"
#include "quicktestutils.h"
#include "targetmanagercontroller.h"
#include "httpcaptureserver.h"
#include "testutils.h"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QInputMethodEvent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QQuickStyle>
#include <QTemporaryDir>
#include <QtQuickTest/quicktest.h>
#include <QtQml/QQmlExtensionPlugin>

QQuickItem* UiFixture::findItem(QQuickWindow* window, const QString& name) { return findQuickItem(window, name); }
QUrl UiFixture::transferFile() const { return QUrl::fromLocalFile(m_files->filePath(QStringLiteral("export.json"))); }
QString UiFixture::exported() const
{
    QFile file(transferFile().toLocalFile());
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString { };
}
void UiFixture::reset()
{
    m_manager.reset();
    m_files = std::make_unique<QTemporaryDir>();
    const auto active = m_files->filePath(QStringLiteral("active"));
    QDir().mkpath(active);
    auto target = rawTarget(QUrl(QStringLiteral("https://example.invalid/upload")));
    target.insert(QStringLiteral("displayName"), QStringLiteral("Catbox"));
    target.insert(QStringLiteral("description"), QStringLiteral("A custom upload service"));
    target.insert(QStringLiteral("future"), QJsonArray { true, 42 });
    write(QStringLiteral("a-valid.json"), QJsonDocument(target).toJson());
    target.insert(QStringLiteral("id"), QStringLiteral("broken"));
    target.insert(QStringLiteral("displayName"), QStringLiteral("Broken fields"));
    target.insert(QStringLiteral("accept"),
        QJsonObject { { QStringLiteral("extensions"), QJsonArray { QStringLiteral("invalid/extension") } } });
    write(QStringLiteral("b-fields.json"), QJsonDocument(target).toJson());
    target.insert(QStringLiteral("displayName"),
        QStringLiteral("A very long name ").repeated(30) + QStringLiteral("\n<br>second line"));
    write(QStringLiteral("c-long.json"), QJsonDocument(target).toJson());
    write(QStringLiteral("d-json.json"), QByteArray("{broken"));
    m_manager = std::make_unique<TargetManagerController>(m_files->filePath(QStringLiteral("presets")), active);
    emit changed();
}

QString UiFixture::path(const QString& name) const { return m_files->filePath(QStringLiteral("active/") + name); }

QString UiFixture::saved(const QString& name) const
{
    QFile file(path(name));
    if (!file.open(QIODevice::ReadOnly))
        return { };
    return QString::fromUtf8(file.readAll());
}

void UiFixture::externalChange()
{
    write(QStringLiteral("a-valid.json"), saved(QStringLiteral("a-valid.json")).toUtf8() + QByteArray("\n"));
}

void UiFixture::pressKey(QQuickItem* item, int key)
{
    item->forceActiveFocus();
    QTest::keyClick(item->window(), Qt::Key(key));
}
void UiFixture::typeText(QQuickItem* item, const QString& text)
{
    if (!item)
        return;
    item->forceActiveFocus();
    QInputMethodEvent event;
    event.setCommitString(text);
    QCoreApplication::sendEvent(item, &event);
}

bool UiFixture::screenshot(QQuickWindow* window, const QString& name)
{
    const auto directory = qEnvironmentVariable("IMSHARE_SCREENSHOTS");
    if (directory.isEmpty())
        return true;
    QDir().mkpath(directory);
    return window && window->grabWindow().save(QDir(directory).filePath(name + QStringLiteral(".png")));
}

bool UiFixture::redIcon(QQuickWindow* window)
{
    const auto image = window->grabWindow();
    const auto scale = qreal(image.width()) / window->width();
    int count = 0;
    for (int y = int(140 * scale); y < int(200 * scale) && y < image.height(); ++y)
        for (int x = int(8 * scale); x < int(45 * scale) && x < image.width(); ++x) {
            const auto c = image.pixelColor(x, y);
            if (c.red() > 150 && c.green() < 130 && c.blue() < 160)
                ++count;
        }
    return count > 5;
}

void UiFixture::startServer()
{
    m_server = std::make_unique<HttpCaptureServer>();
    m_server->start();
    m_server->enqueueResponse({ 200, "OK", "application/json", { }, R"({"url":"https://files.example/qml-test"})" });
    m_manager->draft()->setValue({ QStringLiteral("request"), QStringLiteral("url") }, m_server->url().toString());
    m_manager->draft()->setField({ QStringLiteral("response"), QStringLiteral("url") },
        QJsonObject { { QStringLiteral("type"), QStringLiteral("json_pointer") },
            { QStringLiteral("pointer"), QStringLiteral("/url") } });
}

int UiFixture::requestCount() const { return m_server ? m_server->requests().size() : 0; }

void UiFixture::clear()
{
    m_manager.reset();
    m_server.reset();
    m_files.reset();
    emit changed();
}

void UiFixture::write(const QString& name, const QByteArray& bytes)
{
    QFile file(path(name));
    if (file.open(QIODevice::WriteOnly))
        file.write(bytes);
}

class Setup : public QObject {
    Q_OBJECT
public slots:
    void applicationAvailable()
    {
        QCoreApplication::setOrganizationName(QStringLiteral("imshare-tests"));
        QCoreApplication::setApplicationName(QStringLiteral("imshare-qml-tests"));
        qmlRegisterSingletonInstance("ShareUploaderTests", 1, 0, "Fixture", &fixture);
    }

private:
    UiFixture fixture;
};
int main(int argc, char** argv)
{
    QTemporaryDir cache;
    qputenv("XDG_CACHE_HOME", cache.path().toUtf8());
    QApplication app(argc, argv);
    if (qEnvironmentVariableIsEmpty("QT_QUICK_CONTROLS_STYLE"))
        QQuickStyle::setStyle(QStringLiteral("org.kde.desktop"));
    Setup setup;
    return quick_test_main_with_setup(argc, argv, "imshare_qml", QUICK_TEST_SOURCE_DIR, &setup);
}
#include "qmltests.moc"

Q_IMPORT_QML_PLUGIN(ShareUploaderPlugin)
