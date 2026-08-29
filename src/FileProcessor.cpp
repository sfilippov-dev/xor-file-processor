#include "FileProcessor.h"

#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QByteArray>
#include <QThread>

namespace {
// Размер блока ввода-вывода. Файл читается и пишется частями по 4 МБ:
// потребление памяти не зависит от размера файла (работает и с 10 ГБ),
// а между блоками можно проверить паузу/отмену.
constexpr qint64 kChunkSize = 4 * 1024 * 1024;
}

FileProcessor::FileProcessor(QObject *parent)
    : QObject(parent)
{
}

void FileProcessor::requestPause()
{
    m_paused.store(true);
}

void FileProcessor::requestResume()
{
    m_paused.store(false);
    QMutexLocker locker(&m_mutex);
    m_pauseCond.wakeAll();          // разбудить цикл, ждущий на условии
}

void FileProcessor::requestCancel()
{
    m_cancel.store(true);
    m_paused.store(false);         // снять паузу, чтобы цикл дошёл до проверки отмены
    QMutexLocker locker(&m_mutex);
    m_pauseCond.wakeAll();
}

bool FileProcessor::waitWhilePaused()
{
    QMutexLocker locker(&m_mutex);
    while (m_paused.load() && !m_cancel.load()) {
        emit statusChanged(QStringLiteral("Пауза"));
        m_pauseCond.wait(&m_mutex);
    }
    return !m_cancel.load();
}

QString FileProcessor::resolveOutputPath(const ProcessTask &task) const
{
    QFileInfo inputInfo(task.inputPath);
    QDir outDir(task.outputDir);
    const QString base = inputInfo.completeBaseName();      // имя без последнего расширения
    const QString suffix = inputInfo.suffix();

    auto compose = [&](int counter) {
        QString name = base;
        if (counter > 0)
            name += QStringLiteral(" (%1)").arg(counter);
        if (!suffix.isEmpty())
            name += QLatin1Char('.') + suffix;
        return outDir.absoluteFilePath(name);
    };

    QString candidate = compose(0);
    if (task.conflict == NameConflictPolicy::Overwrite)
        return candidate;

    // Политика «счётчик»: ищем первое свободное имя.
    int counter = 1;
    while (QFile::exists(candidate)) {
        candidate = compose(counter++);
    }
    return candidate;
}

void FileProcessor::process(const ProcessTask &task)
{
    m_cancel.store(false);
    m_paused.store(false);

    QFile input(task.inputPath);
    if (!input.open(QIODevice::ReadOnly)) {
        emit failed(task.inputPath, QStringLiteral("не удалось открыть входной файл: %1")
                        .arg(input.errorString()));
        emit idle();
        return;
    }

    QDir().mkpath(task.outputDir);
    const QString outputPath = resolveOutputPath(task);

    // Выход может совпасть со входом: та же папка + политика «Перезаписать».
    // Тогда входной файл удалять нельзя - это и есть результат.
    const bool outputReplacesInput =
        QFileInfo(outputPath).absoluteFilePath() == QFileInfo(task.inputPath).absoluteFilePath();

    // Пишем во временный файл; переименовываем только при полном успехе,
    // чтобы отмена/сбой не оставляли частично обработанный файл под целевым именем.
    const QString tempPath = outputPath + QStringLiteral(".part");
    QFile::remove(tempPath);
    QFile output(tempPath);
    if (!output.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        emit failed(task.inputPath, QStringLiteral("не удалось создать выходной файл: %1")
                        .arg(output.errorString()));
        emit idle();
        return;
    }

    const qint64 total = input.size();
    emit started(task.inputPath, outputPath);
    emit statusChanged(QStringLiteral("Обработка"));

    // Ключ раскладываем в 8 байт: старший байт hex-значения — key[0].
    unsigned char keyBytes[8];
    for (int i = 0; i < 8; ++i)
        keyBytes[i] = static_cast<unsigned char>((task.key >> (56 - 8 * i)) & 0xFF);

    QByteArray buffer;
    buffer.resize(kChunkSize);
    qint64 done = 0;
    int keyPos = 0;                    // позиция в 8-байтном ключе, сквозная по файлу

    // Скорость считаем от точки последнего старта таймера, а не от начала файла:
    // после паузы таймер перезапускается, и без этой опорной точки скорость
    // получилась бы завышенной (весь объём делился бы на время после резюма).
    QElapsedTimer timer;
    qint64 doneAtTimerStart = 0;
    timer.start();

    auto abortCleanup = [&]() {
        output.close();
        QFile::remove(tempPath);
    };

    while (done < total) {
        if (m_cancel.load()) {
            abortCleanup();
            emit canceled(task.inputPath);
            emit idle();
            return;
        }
        if (m_paused.load()) {
            if (!waitWhilePaused()) {          // во время паузы отменили
                abortCleanup();
                emit canceled(task.inputPath);
                emit idle();
                return;
            }
            emit statusChanged(QStringLiteral("Обработка"));
            timer.restart();                   // не искажать скорость временем паузы
            doneAtTimerStart = done;
        }

        const qint64 read = input.read(buffer.data(), kChunkSize);
        if (read < 0) {
            abortCleanup();
            emit failed(task.inputPath, QStringLiteral("ошибка чтения: %1").arg(input.errorString()));
            emit idle();
            return;
        }
        if (read == 0)
            break;

        // XOR блока с ключом.
        unsigned char *data = reinterpret_cast<unsigned char *>(buffer.data());
        for (qint64 i = 0; i < read; ++i) {
            data[i] ^= keyBytes[keyPos];
            keyPos = (keyPos + 1) & 7;         // (keyPos + 1) % 8
        }

        if (output.write(buffer.constData(), read) != read) {
            abortCleanup();
            emit failed(task.inputPath, QStringLiteral("ошибка записи: %1").arg(output.errorString()));
            emit idle();
            return;
        }

        done += read;
        const double seconds = timer.elapsed() / 1000.0;
        const double mbps = seconds > 0
            ? ((done - doneAtTimerStart) / (1024.0 * 1024.0)) / seconds
            : 0.0;
        emit progress(done, total, mbps);
    }

    if (!output.flush()) {
        abortCleanup();
        emit failed(task.inputPath, QStringLiteral("ошибка сброса на диск: %1").arg(output.errorString()));
        emit idle();
        return;
    }
    output.close();
    input.close();

    // Атомарно ставим готовый файл на место.
    if (task.conflict == NameConflictPolicy::Overwrite)
        QFile::remove(outputPath);
    if (!QFile::rename(tempPath, outputPath)) {
        QFile::remove(tempPath);
        emit failed(task.inputPath, QStringLiteral("не удалось переименовать результат в %1").arg(outputPath));
        emit idle();
        return;
    }

    if (task.removeInput && outputReplacesInput) {
        emit statusChanged(QStringLiteral(
            "Входной файл не удалён: результат записан на его место"));
    } else if (task.removeInput) {
        if (!QFile::remove(task.inputPath))
            emit statusChanged(QStringLiteral("Внимание: не удалён входной файл %1").arg(task.inputPath));
    }

    emit progress(total, total, 0.0);
    emit finished(outputPath);
    emit idle();
}
