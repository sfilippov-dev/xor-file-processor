#ifndef FILEPROCESSOR_H
#define FILEPROCESSOR_H

#include <QObject>
#include <QString>
#include <QFileInfo>
#include <QMutex>
#include <QWaitCondition>
#include <QElapsedTimer>
#include <atomic>
#include <cstdint>
#include <QMetaType>

// Сравнение путей с учётом регистра файловой системы: Windows регистр
// не различает (C:\Files и c:\files - одно и то же), Linux и macOS различают.
// Обычное сравнение строк дало бы на Windows ложное "разные файлы".
inline bool isSamePath(const QString &left, const QString &right)
{
#ifdef Q_OS_WIN
    const Qt::CaseSensitivity sensitivity = Qt::CaseInsensitive;
#else
    const Qt::CaseSensitivity sensitivity = Qt::CaseSensitive;
#endif
    return QString::compare(QFileInfo(left).absoluteFilePath(),
                            QFileInfo(right).absoluteFilePath(),
                            sensitivity) == 0;
}

// Политика поведения при совпадении имени выходного файла.
enum class NameConflictPolicy {
    Overwrite,      // перезаписать существующий файл
    AddCounter      // добавить счётчик к имени: name.bin -> name (1).bin
};

// Задание на обработку одного файла.
struct ProcessTask {
    QString inputPath;
    QString outputDir;
    quint64 key = 0;                    // 8-байтное значение для XOR
    bool removeInput = false;           // удалять входной файл после успеха
    NameConflictPolicy conflict = NameConflictPolicy::AddCounter;
};

// Обработчик файлов. Живёт в отдельном потоке (moveToThread),
// поэтому тяжёлый ввод-вывод не блокирует интерфейс.
//
// Управление извне (из GUI-потока) — потокобезопасными методами
// requestPause / requestResume / requestCancel: они лишь выставляют
// флаги, а сам цикл обработки читает их между блоками данных.
class FileProcessor : public QObject
{
    Q_OBJECT
public:
    explicit FileProcessor(QObject *parent = nullptr);

    // Потокобезопасные команды управления (вызываются из GUI-потока).
    void requestPause();
    void requestResume();
    void requestCancel();
    bool isPaused() const { return m_paused.load(); }

public slots:
    // Точка входа обработки. Вызывается в рабочем потоке.
    void process(const ProcessTask &task);

signals:
    void started(const QString &inputPath, const QString &outputPath);
    void progress(qint64 bytesDone, qint64 bytesTotal, double megabytesPerSecond);
    void statusChanged(const QString &status);
    void finished(const QString &outputPath);        // файл успешно обработан
    void failed(const QString &inputPath, const QString &error);
    void canceled(const QString &inputPath);         // отменён пользователем
    void idle();                                     // очередь команд завершена

private:
    // Возвращает путь выходного файла с учётом политики конфликтов.
    QString resolveOutputPath(const ProcessTask &task) const;
    // Ожидает снятия паузы. Возвращает false, если во время паузы отменили.
    bool waitWhilePaused();

    std::atomic_bool m_paused{false};
    std::atomic_bool m_cancel{false};

    mutable QMutex m_mutex;              // защищает условие паузы
    QWaitCondition m_pauseCond;
};

Q_DECLARE_METATYPE(ProcessTask)

#endif // FILEPROCESSOR_H
