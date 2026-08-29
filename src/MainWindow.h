#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QQueue>
#include <QSet>
#include <QThread>
#include "FileProcessor.h"

class QLineEdit;
class QCheckBox;
class QComboBox;
class QSpinBox;
class QRadioButton;
class QPushButton;
class QProgressBar;
class QLabel;
class QPlainTextEdit;
class QTimer;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    // Корректное завершение: если идёт обработка — останавливаем поток и ждём.
    void closeEvent(QCloseEvent *event) override;

private slots:
    void onStartClicked();
    void onPauseResumeClicked();
    void onStopClicked();
    void onBrowseInput();
    void onBrowseOutput();
    void onPollTimer();

    // Реакция на сигналы обработчика (приходят из рабочего потока).
    void onProcStarted(const QString &inputPath, const QString &outputPath);
    void onProcProgress(qint64 done, qint64 total, double mbps);
    void onProcStatus(const QString &status);
    void onProcFinished(const QString &outputPath);
    void onProcFailed(const QString &inputPath, const QString &error);
    void onProcCanceled(const QString &inputPath);
    void onProcIdle();

private:
    QWidget *buildUi();
    void log(const QString &message);
    void setRunningState(bool running);
    bool validateInputs(QString *error) const;
    quint64 parseKey(bool *ok) const;
    QStringList scanInputFiles() const;      // файлы во входной папке по маске
    void enqueueNewFiles();                  // добавить в очередь неначатые файлы
    void startNextIfPossible();              // запустить следующий файл из очереди
    void stopWorker();                       // остановить поток и дождаться

    // --- элементы формы ---
    QLineEdit   *m_maskEdit       = nullptr;
    QLineEdit   *m_inputDirEdit   = nullptr;
    QLineEdit   *m_outputDirEdit  = nullptr;
    QCheckBox   *m_removeInputBox = nullptr;
    QComboBox   *m_conflictBox    = nullptr;
    QRadioButton*m_modeSingle     = nullptr;
    QRadioButton*m_modeTimer      = nullptr;
    QSpinBox    *m_intervalSpin   = nullptr;
    QLineEdit   *m_keyEdit        = nullptr;

    QPushButton *m_startBtn       = nullptr;
    QPushButton *m_pauseBtn       = nullptr;
    QPushButton *m_stopBtn        = nullptr;

    QProgressBar *m_progress      = nullptr;
    QLabel       *m_statusLabel   = nullptr;
    QLabel       *m_fileLabel     = nullptr;
    QLabel       *m_speedLabel    = nullptr;
    QPlainTextEdit *m_logView     = nullptr;

    // --- обработка ---
    QThread        m_thread;
    FileProcessor *m_processor = nullptr;
    QTimer        *m_pollTimer = nullptr;

    QQueue<QString> m_queue;              // ожидающие файлы
    QSet<QString>   m_seen;               // уже поставленные в очередь/обработанные
    bool m_running = false;               // сессия запущена (Start нажат)
    bool m_busy = false;                  // обработчик занят файлом
    bool m_timerMode = false;

signals:
    void submit(const ProcessTask &task); // запуск обработки в рабочем потоке
};

#endif // MAINWINDOW_H
