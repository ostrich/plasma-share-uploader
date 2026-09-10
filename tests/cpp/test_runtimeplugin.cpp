#include "quicktestutils.h"
#include "httpcaptureserver.h"
#include "testutils.h"

#include <Purpose/AlternativesModel>
#include <Purpose/Job>
#include <QApplication>
#include <QQuickWindow>
#include <QQuickItem>
#include <QJsonDocument>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QtTest>
#include <memory>

// Exercise the built module through the same controller used by Purpose clients.
// No production objects are linked into this executable.
class RuntimePluginTest final : public QObject {
    Q_OBJECT
private slots:
    void controllerLifecycle_data();
    void controllerLifecycle();
};

void RuntimePluginTest::controllerLifecycle_data()
{
    QTest::addColumn<bool>("cancelPicker");
    QTest::addColumn<bool>("linkedPreset");
    QTest::addColumn<bool>("startEmpty");
    QTest::newRow("cancel") << true << false << false;
    QTest::newRow("upload") << false << false << false;
    QTest::newRow("upload-preset-link") << false << true << false;
    QTest::newRow("empty-cancel") << true << false << true;
    QTest::newRow("empty-reload-upload") << false << false << true;
}

void RuntimePluginTest::controllerLifecycle()
{
    QFETCH(bool, cancelPicker);
    QFETCH(bool, linkedPreset);
    QFETCH(bool, startEmpty);
    HttpCaptureServer server;
    QVERIFY(server.start());
    server.enqueueResponse({ 200, "OK", "text/plain", { }, "https://files.example/plugin" });
    QTemporaryDir dir;
    const QString source = writeTempFile(dir, QStringLiteral("input.txt"), "plugin body");
    const QString icon = writeTempFile(dir, QStringLiteral("icon.png"), tinyPng());
    auto config = rawTarget(server.url());
    config.insert(QStringLiteral("icon"), icon);
    const QString configRoot = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
        + QStringLiteral("/plasma-share-uploader");
    QVERIFY(QDir().mkpath(configRoot + QStringLiteral("/targets")));
    const QString activeFile = configRoot + QStringLiteral("/targets/raw.json");
    QFile::remove(activeFile);
    const QString presetFile = dir.filePath(QStringLiteral("preset.json"));
    QFile target(linkedPreset ? presetFile : activeFile);
    QVERIFY(target.open(QIODevice::WriteOnly));
    target.write(QJsonDocument(config).toJson());
    target.close();
    if (linkedPreset) {
        QVERIFY(QFile::link(presetFile, activeFile));
    }
    if (startEmpty)
        QVERIFY(QFile::remove(activeFile));

    Purpose::AlternativesModel model;
    model.setPluginType(QStringLiteral("ShareUrl"));
    model.setInputData(QJsonObject { { QStringLiteral("urls"), QJsonArray { QUrl::fromLocalFile(source).toString() } },
        { QStringLiteral("title"), QStringLiteral("Plugin test") },
        { QStringLiteral("mimeType"), QStringLiteral("text/plain") } });
    int pluginRow = -1;
    for (int row = 0; row < model.rowCount(); ++row) {
        if (model.data(model.index(row), Purpose::AlternativesModel::PluginIdRole).toString()
            == QStringLiteral("runtimeuploadplugin")) {
            pluginRow = row;
            break;
        }
    }
    QVERIFY(pluginRow >= 0);

    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.setData(R"(
        import org.kde.purpose 1.0
        PurposeJobController {
            property string observedStates: ""
            onStateChanged: observedStates += state + ","
        }
    )",
        QUrl());
    std::unique_ptr<QObject> controller(component.create());
    QVERIFY2(controller, qPrintable(component.errorString()));
    QVERIFY(controller->setProperty("model", QVariant::fromValue(&model)));
    QVERIFY(controller->setProperty("index", pluginRow));
    QVERIFY(QMetaObject::invokeMethod(controller.get(), "configure"));
    auto* job = controller->property("job").value<Purpose::Job*>();
    QVERIFY(job);
    QString errorText;
    QJsonObject output;
    connect(job, &KJob::result, this, [&](KJob* completed) {
        errorText = completed->errorText();
        output = job->output();
    });
    QCOMPARE(controller->property("state").toInt(), 2); // Running
    // The caller is free to discard shared temporary files after configure/start.
    QVERIFY(QFile::remove(source));
    auto findPicker = []() -> QQuickWindow* {
        for (auto* window : QGuiApplication::allWindows())
            if (window->isVisible() && window->objectName() == QLatin1StringView("targetPicker"))
                return qobject_cast<QQuickWindow*>(window);
        return nullptr;
    };
    QTRY_VERIFY(findPicker());
    auto* picker = findPicker();
    auto click = [](QQuickWindow* window, const QString& name) {
        auto* item = findQuickItem(window, name);
        QVERIFY(item);
        QVERIFY(item->isEnabled());
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
            item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint());
    };
    QVERIFY(picker->findChild<QQuickItem*>(QStringLiteral("configureTargets"))->isEnabled());
    if (startEmpty && !cancelPicker) {
        QFile added(activeFile);
        QVERIFY(added.open(QIODevice::WriteOnly));
        added.write(QJsonDocument(config).toJson());
        added.close();
        click(picker, QStringLiteral("reloadTargets"));
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QTRY_VERIFY(findPicker());
        picker = findPicker();
    }
    QTest::qWait(50);
    click(picker, cancelPicker ? QStringLiteral("cancelPicker") : QStringLiteral("pickerTarget0"));
    QTRY_VERIFY(controller->property("state").toInt() != 2);
    QVERIFY2(controller->property("state").toInt() == 3, qPrintable(errorText)); // Finished
    QCOMPARE(controller->property("observedStates").toString(), QStringLiteral("2,3,"));
    QCOMPARE(server.requests().size(), cancelPicker ? 0 : 1);
    if (!cancelPicker) {
        QCOMPARE(server.requests().first().body, QByteArray("plugin body"));
        QCOMPARE(output.value(QStringLiteral("url")).toString(), QStringLiteral("https://files.example/plugin"));
    }
}

int main(int argc, char** argv)
{
    QTemporaryDir isolated;
    qputenv("XDG_CONFIG_HOME", isolated.filePath(QStringLiteral("config")).toUtf8());
    qputenv("XDG_CACHE_HOME", isolated.filePath(QStringLiteral("cache")).toUtf8());
    QApplication app(argc, argv);
    const QString pluginRoot = isolated.filePath(QStringLiteral("plugins"));
    const QString purposeDir = pluginRoot + QStringLiteral("/kf6/purpose");
    if (!QDir().mkpath(purposeDir)
        || !QFile::copy(qEnvironmentVariable("IMSHARE_TEST_PLUGIN_PATH", QStringLiteral(IMSHARE_TEST_PLUGIN_PATH)),
            purposeDir + QStringLiteral("/runtimeuploadplugin.so"))) {
        return 1;
    }
    QCoreApplication::addLibraryPath(pluginRoot);
    RuntimePluginTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_runtimeplugin.moc"
