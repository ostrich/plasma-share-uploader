#include "asyncfilecopy.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QThread>
#include <QTimer>
#include <atomic>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace {
struct CopyState {
    ~CopyState() { for (int fd : descriptors) ::close(fd); }
    std::vector<int> descriptors;
    std::shared_ptr<QTemporaryDir> directory;
    AsyncFileCopy::Result result;
    std::atomic_bool cancelled = false;
    std::atomic<qint64> copied = 0;
    qint64 total = 0;
};
class CopyTask final : public QObject {
public:
    CopyTask(QObject *parent, std::shared_ptr<CopyState> state,
             std::function<void(AsyncFileCopy::Result)> completed,
             std::function<void(qint64, qint64)> progress)
        : QObject(parent), m_state(std::move(state))
    {
        auto *thread = QThread::create([state = m_state]() {
            if (!state->result.error.isEmpty()) return;
            QByteArray buffer(256 * 1024, Qt::Uninitialized);
            for (size_t i = 0; i < state->descriptors.size(); ++i) {
                if (state->cancelled) return;
                const auto path = state->result.files.at(qsizetype(i));
                QFile input, output(path);
                if (!QDir().mkpath(QFileInfo(path).absolutePath())
                    || !input.open(state->descriptors[i], QIODevice::ReadOnly, QFileDevice::DontCloseHandle)
                    || !output.open(QIODevice::WriteOnly)) {
                    state->result.error = QStringLiteral("Could not open temporary upload copy.");
                    return;
                }
                while (!state->cancelled) {
                    const auto count = input.read(buffer.data(), buffer.size());
                    if (count < 0) { state->result.error = input.errorString(); return; }
                    if (count == 0) break;
                    qint64 written = 0;
                    while (written < count && !state->cancelled) {
                        const auto n = output.write(buffer.constData() + written, count - written);
                        if (n <= 0) { state->result.error = output.errorString(); return; }
                        written += n;
                    }
                    state->copied += written;
                }
                if (!state->cancelled && !output.flush()) { state->result.error = output.errorString(); return; }
            }
        });
        if (progress) {
            m_timer.setInterval(50);
            connect(&m_timer, &QTimer::timeout, this, [state = m_state, progress]() {
                progress(state->copied.load(), state->total);
            });
            m_timer.start();
        }
        connect(thread, &QThread::finished, thread, &QObject::deleteLater);
        connect(thread, &QThread::finished, this, [this, completed = std::move(completed)]() {
            m_timer.stop();
            auto result = m_state->result;
            auto callback = completed;
            deleteLater();
            callback(std::move(result));
        });
        thread->start();
    }
    ~CopyTask() override { m_state->cancelled = true; }
private:
    std::shared_ptr<CopyState> m_state;
    QTimer m_timer;
};
}
QObject *AsyncFileCopy::start(const QStringList &sources, std::shared_ptr<QTemporaryDir> directory,
                             QObject *context, std::function<void(Result)> completed,
                             std::function<void(qint64, qint64)> progress)
{
    auto state = std::make_shared<CopyState>();
    state->directory = std::move(directory);
    if (!state->directory || !state->directory->isValid())
        state->result.error = QStringLiteral("Could not create temporary upload directory.");
    for (const auto &source : sources) {
        if (!state->result.error.isEmpty()) break;
        const int fd = ::open(QFile::encodeName(source).constData(), O_RDONLY | O_CLOEXEC | O_NONBLOCK);
        struct stat info {};
        if (fd < 0 || ::fstat(fd, &info) != 0 || !S_ISREG(info.st_mode)) {
            if (fd >= 0) ::close(fd);
            state->result.error = QStringLiteral("Could not open local file: %1").arg(source);
            break;
        }
        state->descriptors.push_back(fd);
        state->total += info.st_size;
        state->result.files.append(state->directory->filePath(
            QString::number(state->descriptors.size() - 1) + QLatin1Char('/') + QFileInfo(source).fileName()));
    }
    return new CopyTask(context, state, std::move(completed), std::move(progress));
}
