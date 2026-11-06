#pragma once
#include <QObject>
#include <QStringList>
#include <QTemporaryDir>
#include <functional>
#include <memory>

namespace AsyncFileCopy {
struct Result {
    QStringList files;
    QString error;
};
// Pins regular input files before returning. The worker retains the destination
// directory until all I/O stops, even if the owning context is destroyed.
QObject *start(const QStringList &sources, std::shared_ptr<QTemporaryDir> directory,
               QObject *context, std::function<void(Result)> completed,
               std::function<void(qint64, qint64)> progress = {});
}
