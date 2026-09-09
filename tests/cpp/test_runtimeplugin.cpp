#include "httpcaptureserver.h"
#include "testutils.h"

#include <Purpose/AlternativesModel>
#include <Purpose/Job>
#include <QApplication>
#include <QDialog>
#include <QJsonDocument>
#include <QLabel>
#include <QPushButton>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QtTest>
#include <memory>

// Exercise the built module through the same controller used by Purpose clients.
// No production objects are linked into this executable.
class RuntimePluginTest final : public QObject
{
    Q_OBJECT
private slots:
    void controllerLifecycle_data();
    void controllerLifecycle();
};

void RuntimePluginTest::controllerLifecycle_data()
{
    QTest::addColumn<bool>("cancelPicker");
    QTest::newRow("cancel") << true;
    QTest::newRow("upload") << false;
}

void RuntimePluginTest::controllerLifecycle()
{
    QFETCH(bool, cancelPicker);
    HttpCaptureServer server;
    QVERIFY(server.start());
    server.enqueueResponse({200, "OK", "text/plain", {}, "https://files.example/plugin"});
    QTemporaryDir dir;
    const QString source = writeTempFile(dir, QStringLiteral("input.txt"), "plugin body");
    const QString icon = writeTempFile(dir, QStringLiteral("icon.png"), tinyPng());
    auto config = rawTarget(server.url());
    config.insert(QStringLiteral("icon"), icon);
    const QString configRoot = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
        + QStringLiteral("/plasma-share-uploader");
    QVERIFY(QDir().mkpath(configRoot + QStringLiteral("/targets")));
    QFile target(configRoot + QStringLiteral("/targets/raw.json"));
    QVERIFY(target.open(QIODevice::WriteOnly));
    target.write(QJsonDocument(config).toJson());
    target.close();
    QFile state(configRoot + QStringLiteral("/state.json"));
    QVERIFY(state.open(QIODevice::WriteOnly));
    state.write(R"({"disabledBundledTargets":["catbox","uguu"]})");
    state.close();

    Purpose::AlternativesModel model;
    model.setPluginType(QStringLiteral("ShareUrl"));
    model.setInputData(QJsonObject{
        {QStringLiteral("urls"), QJsonArray{QUrl::fromLocalFile(source).toString()}},
        {QStringLiteral("title"), QStringLiteral("Plugin test")},
        {QStringLiteral("mimeType"), QStringLiteral("text/plain")}});
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
    )", QUrl());
    std::unique_ptr<QObject> controller(component.create());
    QVERIFY2(controller, qPrintable(component.errorString()));
    QVERIFY(controller->setProperty("model", QVariant::fromValue(&model)));
    QVERIFY(controller->setProperty("index", pluginRow));
    QVERIFY(QMetaObject::invokeMethod(controller.get(), "configure"));
    auto *job = controller->property("job").value<Purpose::Job *>();
    QVERIFY(job);
    QString errorText;
    QJsonObject output;
    connect(job, &KJob::result, this, [&](KJob *completed) {
        errorText = completed->errorText();
        output = job->output();
    });
    QCOMPARE(controller->property("state").toInt(), 2); // Running
    // The caller is free to discard shared temporary files after configure/start.
    QVERIFY(QFile::remove(source));
    QTRY_VERIFY(qobject_cast<QDialog *>(QApplication::activeModalWidget()));
    auto *picker = qobject_cast<QDialog *>(QApplication::activeModalWidget());
    if (cancelPicker) {
        picker->reject();
    } else {
        QPushButton *targetButton = nullptr;
        for (auto *button : picker->findChildren<QPushButton *>()) {
            for (auto *label : button->findChildren<QLabel *>()) {
                if (label->text() == QStringLiteral("Raw Target")) {
                    targetButton = button;
                }
            }
        }
        QVERIFY(targetButton);
        targetButton->click();
    }
    QTRY_VERIFY(controller->property("state").toInt() != 2);
    QVERIFY2(controller->property("state").toInt() == 3, qPrintable(errorText)); // Finished
    QCOMPARE(controller->property("observedStates").toString(), QStringLiteral("2,3,"));
    QCOMPARE(server.requests().size(), cancelPicker ? 0 : 1);
    if (!cancelPicker) {
        QCOMPARE(server.requests().first().body, QByteArray("plugin body"));
        QCOMPARE(output.value(QStringLiteral("url")).toString(), QStringLiteral("https://files.example/plugin"));
    }
}

int main(int argc, char **argv)
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
