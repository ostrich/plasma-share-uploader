#include "targetpickerdialog.h"
#include "sharejob.h"
#include "quicktestutils.h"
#include "testutils.h"
#include "httpcaptureserver.h"
#include <QApplication>
#include <QProcess>
#include <QQuickStyle>
#include <QSignalSpy>
#include <QtTest>

class PickerTest : public QObject {
    Q_OBJECT
private slots:
    void closingShareMenuKeepsWidgetHost()
    {
        QTemporaryDir files;
        const auto input = writeTempFile(files, QStringLiteral("input.txt"), QByteArray("body"));
        QWidget host;
        host.show();
        QVERIFY(QTest::qWaitForWindowExposed(&host));
        QQuickWindow menu;
        menu.setFlags(Qt::Popup);
        menu.setTransientParent(host.windowHandle());
        menu.show();
        menu.requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(&menu));
        menu.close();

        ShareJob job({ });
        job.setAutoDelete(false);
        job.setData({ { QStringLiteral("url"), QUrl::fromLocalFile(input).toString() } });
        job.start();
        QTRY_VERIFY(job.findChild<TargetPickerDialog*>());
        QCOMPARE(job.findChild<TargetPickerDialog*>()->window()->transientParent(), host.windowHandle());
    }
    void repeatedWindowsKeepHostSettingsAndCancelOnce()
    {
        const auto name = QCoreApplication::applicationName();
        const auto organization = QCoreApplication::organizationName();
        const auto paths = QCoreApplication::libraryPaths();
        QQuickWindow host;
        host.show();
        QVERIFY(QTest::qWaitForWindowExposed(&host));
        for (int i = 0; i < 8; ++i) {
            TargetPickerDialog picker({ }, { }, &host);
            QSignalSpy finished(&picker, &TargetPickerDialog::finished);
            QSignalSpy errors(&picker, &TargetPickerDialog::loadFailed);
            picker.open();
            QCOMPARE(errors.size(), 0);
            QVERIFY(picker.window());
            QPointer<QQuickWindow> window = picker.window();
            QCOMPARE(window->transientParent(), &host);
            QCOMPARE(window->modality(), Qt::WindowModal);
            QCOMPARE(window->type(), Qt::Dialog);
            QVERIFY(QTest::qWaitForWindowExposed(window));
            if (i % 2)
                QTest::keyClick(window, Qt::Key_Escape);
            else
                window->close();
            picker.reject();
            QCOMPARE(finished.size(), 1);
            QCOMPARE(finished.first().first().toBool(), false);
            QCOMPARE(QCoreApplication::applicationName(), name);
            QCOMPARE(QCoreApplication::organizationName(), organization);
            QCOMPARE(QCoreApplication::libraryPaths(), paths);
            const auto directory = qEnvironmentVariable("IMSHARE_SCREENSHOTS");
            if (i == 0 && !directory.isEmpty()) {
                window->show();
                QTest::qWait(30);
                QDir().mkpath(directory);
                QVERIFY(window->grabWindow().save(QDir(directory).filePath(QStringLiteral("picker-empty.png"))));
            }
        }
    }
    void deletingJobAndReloadingKeepWindowOwnership()
    {
        QTemporaryDir files;
        const auto input = writeTempFile(files, QStringLiteral("input.txt"), QByteArray("body"));
        QQuickWindow host;
        host.show();
        host.requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(&host));
        auto* job = new ShareJob({ });
        job->setAutoDelete(false);
        job->setData({ { QStringLiteral("url"), QUrl::fromLocalFile(input).toString() } });
        QSignalSpy finished(job, &KJob::result);
        job->start();
        QTRY_VERIFY(job->findChild<TargetPickerDialog*>());
        auto* picker = job->findChild<TargetPickerDialog*>();
        QPointer<QQuickWindow> oldWindow = picker->window();
        QCOMPARE(oldWindow->transientParent(), &host);
        picker->findChild<PickerController*>()->reload();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(oldWindow.isNull());
        QCOMPARE(finished.size(), 0);
        picker = job->findChild<TargetPickerDialog*>();
        QVERIFY(picker);
        QPointer<QQuickWindow> window = picker->window();
        QCOMPARE(window->transientParent(), &host);
        delete job;
        QVERIFY(window.isNull());
    }
    void iconCacheAndCallbackLifetime()
    {
        HttpCaptureServer server;
        QVERIFY(server.start());
        server.enqueueResponse({ 200, "OK", "image/png", { }, tinyPng() });
        QTemporaryDir files;
        auto object = rawTarget(server.url());
        object.insert(QStringLiteral("icon"), server.url(QStringLiteral("/icon.png")).toString());
        TargetDefinition definition;
        QVERIFY(TargetConfigParser::parse(object, &definition.target));
        TargetIconProvider provider(nullptr, files.filePath(QStringLiteral("system")),
            files.filePath(QStringLiteral("user")), files.filePath(QStringLiteral("cache")));
        QObject consumer;
        QStringList sources;
        provider.requestIcon(definition, &consumer, [&](const QString& source) { sources.append(source); });
        QTRY_COMPARE(sources.size(), 2);
        QVERIFY(QUrl(sources.last()).isLocalFile());
        QCOMPARE(server.requests().size(), 1);
        sources.clear();
        provider.requestIcon(definition, &consumer, [&](const QString& source) { sources.append(source); });
        QVERIFY(QUrl(sources.last()).isLocalFile());
        QCOMPARE(server.requests().size(), 1);
        object.insert(QStringLiteral("icon"), server.url(QStringLiteral("/other.png")).toString());
        QVERIFY(TargetConfigParser::parse(object, &definition.target));
        server.enqueueResponse({ 200, "OK", "image/png", { }, tinyPng() });
        auto* temporary = new QObject;
        int calls = 0;
        provider.requestIcon(definition, temporary, [&](const QString&) { ++calls; });
        delete temporary;
        QTRY_COMPARE(server.requests().size(), 2);
        QTest::qWait(20);
        QCOMPARE(calls, 1); // Only the synchronous fallback before destruction.
    }
    void graphicsFailureIsReportedOnce()
    {
        TargetPickerDialog picker({ });
        QSignalSpy errors(&picker, &TargetPickerDialog::loadFailed);
        picker.open();
        QVERIFY(picker.window());
        picker.window()->sceneGraphError(QQuickWindow::ContextNotAvailable, QStringLiteral("Test context unavailable"));
        QCOMPARE(errors.size(), 1);
        QVERIFY(errors.first().first().toString().contains(QStringLiteral("Test context unavailable")));
        QVERIFY(!picker.window()->isVisible());
        picker.window()->sceneGraphError(QQuickWindow::ContextNotAvailable, QStringLiteral("Repeated failure"));
        QCOMPARE(errors.size(), 1);
    }
    void brokenQmlIsANormalJobError()
    {
        QProcess process;
        auto env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("QT_QUICK_CONTROLS_STYLE"), QStringLiteral("MissingImshareTestStyle"));
        process.setProcessEnvironment(env);
        process.start(QCoreApplication::applicationFilePath(), { QStringLiteral("--broken-ui") });
        QVERIFY(process.waitForFinished(10000));
        QCOMPARE(process.exitStatus(), QProcess::NormalExit);
        QCOMPARE(process.exitCode(), 0);
    }
};
int main(int argc, char** argv)
{
    QTemporaryDir isolated;
    qputenv("XDG_CONFIG_HOME", isolated.filePath(QStringLiteral("config")).toUtf8());
    qputenv("XDG_CACHE_HOME", isolated.filePath(QStringLiteral("cache")).toUtf8());
    QDir().mkpath(isolated.filePath(QStringLiteral("config/plasma-share-uploader/targets")));
    QApplication app(argc, argv);
    if (app.arguments().contains(QStringLiteral("--broken-ui"))) {
        const auto path = writeTempFile(isolated, QStringLiteral("input.txt"), QByteArray("body"));
        ShareJob job({ });
        job.setAutoDelete(false);
        job.setData({ { QStringLiteral("url"), QUrl::fromLocalFile(path).toString() } });
        QObject::connect(&job, &KJob::result, &app, [&]() {
            app.exit(
                job.error() && job.errorText().contains(QStringLiteral("Could not load upload target picker")) ? 0 : 1);
        });
        QTimer::singleShot(5000, &app, [&]() { app.exit(2); });
        job.start();
        return app.exec();
    }
    PickerTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "test_picker.moc"
