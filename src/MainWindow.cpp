#include "MainWindow.h"

#include <QtWidgets>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("XOR File Processor"));
    setCentralWidget(buildUi());
    resize(760, 620);

    // Обработчик в отдельном потоке — интерфейс не «замирает» на больших файлах.
    m_processor = new FileProcessor;
    m_processor->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_processor, &QObject::deleteLater);

    connect(this, &MainWindow::submit, m_processor, &FileProcessor::process);
    connect(m_processor, &FileProcessor::started,  this, &MainWindow::onProcStarted);
    connect(m_processor, &FileProcessor::progress, this, &MainWindow::onProcProgress);
    connect(m_processor, &FileProcessor::statusChanged, this, &MainWindow::onProcStatus);
    connect(m_processor, &FileProcessor::finished, this, &MainWindow::onProcFinished);
    connect(m_processor, &FileProcessor::failed,   this, &MainWindow::onProcFailed);
    connect(m_processor, &FileProcessor::canceled, this, &MainWindow::onProcCanceled);
    connect(m_processor, &FileProcessor::idle,     this, &MainWindow::onProcIdle);
    m_thread.start();

    m_pollTimer = new QTimer(this);
    connect(m_pollTimer, &QTimer::timeout, this, &MainWindow::onPollTimer);

    setRunningState(false);
}

MainWindow::~MainWindow()
{
    stopWorker();
}

QWidget *MainWindow::buildUi()
{
    auto *central = new QWidget;
    auto *root = new QVBoxLayout(central);

    auto *form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignRight);

    m_maskEdit = new QLineEdit(QStringLiteral("*.txt;*.bin"));
    m_maskEdit->setToolTip(QStringLiteral("Маски через ';' или пробел, напр.: *.txt;testFile.bin"));
    form->addRow(QStringLiteral("Маска входных файлов:"), m_maskEdit);

    auto *inRow = new QHBoxLayout;
    m_inputDirEdit = new QLineEdit;
    auto *inBtn = new QPushButton(QStringLiteral("Обзор…"));
    connect(inBtn, &QPushButton::clicked, this, &MainWindow::onBrowseInput);
    inRow->addWidget(m_inputDirEdit);
    inRow->addWidget(inBtn);
    form->addRow(QStringLiteral("Папка поиска файлов:"), inRow);

    auto *outRow = new QHBoxLayout;
    m_outputDirEdit = new QLineEdit;
    auto *outBtn = new QPushButton(QStringLiteral("Обзор…"));
    connect(outBtn, &QPushButton::clicked, this, &MainWindow::onBrowseOutput);
    outRow->addWidget(m_outputDirEdit);
    outRow->addWidget(outBtn);
    form->addRow(QStringLiteral("Папка результатов:"), outRow);

    m_removeInputBox = new QCheckBox(QStringLiteral("Удалять входные файлы после обработки"));
    form->addRow(QString(), m_removeInputBox);

    m_conflictBox = new QComboBox;
    m_conflictBox->addItem(QStringLiteral("Добавить счётчик к имени"), int(NameConflictPolicy::AddCounter));
    m_conflictBox->addItem(QStringLiteral("Перезаписать"), int(NameConflictPolicy::Overwrite));
    form->addRow(QStringLiteral("Если имя выходного файла занято:"), m_conflictBox);

    auto *modeRow = new QHBoxLayout;
    m_modeSingle = new QRadioButton(QStringLiteral("Разовый запуск"));
    m_modeTimer  = new QRadioButton(QStringLiteral("По таймеру"));
    m_modeSingle->setChecked(true);
    modeRow->addWidget(m_modeSingle);
    modeRow->addWidget(m_modeTimer);
    modeRow->addStretch();
    form->addRow(QStringLiteral("Режим работы:"), modeRow);

    m_intervalSpin = new QSpinBox;
    m_intervalSpin->setRange(1, 86400);
    m_intervalSpin->setValue(5);
    m_intervalSpin->setSuffix(QStringLiteral(" с"));
    m_intervalSpin->setEnabled(false);
    connect(m_modeTimer, &QRadioButton::toggled, m_intervalSpin, &QWidget::setEnabled);
    form->addRow(QStringLiteral("Период опроса:"), m_intervalSpin);

    m_keyEdit = new QLineEdit(QStringLiteral("1234567890ABCDEF"));
    m_keyEdit->setInputMask(QStringLiteral("HHHHHHHHHHHHHHHH"));  // ровно 16 hex-символов
    m_keyEdit->setToolTip(QStringLiteral("8 байт в hex, напр.: 1234567890ABCDEF"));
    form->addRow(QStringLiteral("Ключ XOR (8 байт, hex):"), m_keyEdit);

    root->addLayout(form);

    auto *btnRow = new QHBoxLayout;
    m_startBtn = new QPushButton(QStringLiteral("Старт"));
    m_pauseBtn = new QPushButton(QStringLiteral("Пауза"));
    m_stopBtn  = new QPushButton(QStringLiteral("Стоп"));
    connect(m_startBtn, &QPushButton::clicked, this, &MainWindow::onStartClicked);
    connect(m_pauseBtn, &QPushButton::clicked, this, &MainWindow::onPauseResumeClicked);
    connect(m_stopBtn,  &QPushButton::clicked, this, &MainWindow::onStopClicked);
    btnRow->addWidget(m_startBtn);
    btnRow->addWidget(m_pauseBtn);
    btnRow->addWidget(m_stopBtn);
    btnRow->addStretch();
    root->addLayout(btnRow);

    m_progress = new QProgressBar;
    m_progress->setRange(0, 100);
    m_progress->setValue(0);
    root->addWidget(m_progress);

    m_fileLabel   = new QLabel(QStringLiteral("Файл: —"));
    m_statusLabel = new QLabel(QStringLiteral("Статус: ожидание"));
    m_speedLabel  = new QLabel(QStringLiteral("Скорость: —"));
    root->addWidget(m_fileLabel);
    root->addWidget(m_statusLabel);
    root->addWidget(m_speedLabel);

    m_logView = new QPlainTextEdit;
    m_logView->setReadOnly(true);
    root->addWidget(m_logView, 1);

    return central;
}

void MainWindow::log(const QString &message)
{
    m_logView->appendPlainText(QStringLiteral("[%1] %2")
        .arg(QTime::currentTime().toString(QStringLiteral("HH:mm:ss")), message));
}

void MainWindow::onBrowseInput()
{
    const QString dir = QFileDialog::getExistingDirectory(this, QStringLiteral("Папка поиска файлов"),
                                                          m_inputDirEdit->text());
    if (!dir.isEmpty())
        m_inputDirEdit->setText(dir);
}

void MainWindow::onBrowseOutput()
{
    const QString dir = QFileDialog::getExistingDirectory(this, QStringLiteral("Папка результатов"),
                                                          m_outputDirEdit->text());
    if (!dir.isEmpty())
        m_outputDirEdit->setText(dir);
}

quint64 MainWindow::parseKey(bool *ok) const
{
    QString text = m_keyEdit->text().trimmed();
    text.remove(QLatin1Char(' '));
    if (text.size() != 16) {                 // 8 байт == 16 hex-символов
        if (ok) *ok = false;
        return 0;
    }
    return text.toULongLong(ok, 16);
}

bool MainWindow::validateInputs(QString *error) const
{
    if (m_inputDirEdit->text().trimmed().isEmpty() || !QDir(m_inputDirEdit->text()).exists()) {
        *error = QStringLiteral("Укажите существующую папку поиска файлов.");
        return false;
    }
    if (m_outputDirEdit->text().trimmed().isEmpty()) {
        *error = QStringLiteral("Укажите папку для результатов.");
        return false;
    }
    if (m_maskEdit->text().trimmed().isEmpty()) {
        *error = QStringLiteral("Укажите маску входных файлов.");
        return false;
    }
    bool ok = false;
    parseKey(&ok);
    if (!ok) {
        *error = QStringLiteral("Ключ должен содержать ровно 16 hex-символов (8 байт).");
        return false;
    }
    // Совпадение папок + «Перезаписать» означает запись результата поверх входного
    // файла. Вместе с «удалять входные» это уничтожило бы и результат.
    const bool sameDir = isSamePath(m_inputDirEdit->text(), m_outputDirEdit->text());
    const bool overwrite = NameConflictPolicy(m_conflictBox->currentData().toInt())
                        == NameConflictPolicy::Overwrite;
    if (sameDir && overwrite && m_removeInputBox->isChecked()) {
        *error = QStringLiteral(
            "Папки входа и результата совпадают при политике «Перезаписать»: "
            "результат будет записан поверх входного файла, поэтому его нельзя "
            "удалять. Снимите флажок удаления или выберите другую папку.");
        return false;
    }
    return true;
}

QStringList MainWindow::scanInputFiles() const
{
    QStringList masks;
    const QString raw = m_maskEdit->text();
    for (const QString &part : raw.split(QRegularExpression(QStringLiteral("[;\\s]+")), Qt::SkipEmptyParts))
        masks << part;

    QDir dir(m_inputDirEdit->text());
    const QFileInfoList entries = dir.entryInfoList(masks, QDir::Files | QDir::Readable, QDir::Name);
    QStringList result;
    for (const QFileInfo &info : entries)
        result << info.absoluteFilePath();
    return result;
}

void MainWindow::enqueueNewFiles()
{
    const QStringList files = scanInputFiles();
    int added = 0;
    for (const QString &path : files) {
        if (!m_seen.contains(path)) {
            m_seen.insert(path);
            m_queue.enqueue(path);
            ++added;
        }
    }
    if (added > 0)
        log(QStringLiteral("Добавлено файлов в очередь: %1").arg(added));
}

void MainWindow::onStartClicked()
{
    QString error;
    if (!validateInputs(&error)) {
        QMessageBox::warning(this, QStringLiteral("Проверьте параметры"), error);
        return;
    }

    m_running = true;
    m_timerMode = m_modeTimer->isChecked();
    m_seen.clear();
    m_queue.clear();
    setRunningState(true);
    log(QStringLiteral("Запуск. Режим: %1")
            .arg(m_timerMode ? QStringLiteral("по таймеру") : QStringLiteral("разовый")));

    enqueueNewFiles();
    if (m_timerMode)
        m_pollTimer->start(m_intervalSpin->value() * 1000);

    if (m_queue.isEmpty() && !m_timerMode)
        log(QStringLiteral("По заданной маске файлов не найдено."));

    startNextIfPossible();
}

void MainWindow::startNextIfPossible()
{
    if (m_busy || !m_running)
        return;

    // Пропускаем исчезнувшие файлы циклом, а не рекурсией:
    // при длинной очереди рекурсия могла бы переполнить стек.
    QString path;
    while (!m_queue.isEmpty()) {
        const QString candidate = m_queue.dequeue();
        if (QFile::exists(candidate)) {
            path = candidate;
            break;
        }
    }
    if (path.isEmpty())
        return;

    bool ok = false;
    ProcessTask task;
    task.inputPath = path;
    task.outputDir = m_outputDirEdit->text();
    task.key = parseKey(&ok);
    task.removeInput = m_removeInputBox->isChecked();
    task.conflict = NameConflictPolicy(m_conflictBox->currentData().toInt());

    m_busy = true;
    m_pauseBtn->setEnabled(true);
    emit submit(task);
}

void MainWindow::onPauseResumeClicked()
{
    if (!m_busy)
        return;
    if (m_processor->isPaused()) {
        m_processor->requestResume();
        m_pauseBtn->setText(QStringLiteral("Пауза"));
    } else {
        m_processor->requestPause();
        m_pauseBtn->setText(QStringLiteral("Возобновить"));
    }
}

void MainWindow::onStopClicked()
{
    log(QStringLiteral("Остановка по запросу пользователя."));
    m_running = false;
    m_pollTimer->stop();
    m_queue.clear();
    if (m_busy)
        m_processor->requestCancel();
    else
        setRunningState(false);
}

void MainWindow::onPollTimer()
{
    if (m_running) {
        enqueueNewFiles();
        startNextIfPossible();
    }
}

void MainWindow::onProcStarted(const QString &inputPath, const QString &outputPath)
{
    m_progress->setValue(0);
    m_fileLabel->setText(QStringLiteral("Файл: %1  →  %2")
                             .arg(QFileInfo(inputPath).fileName(), QFileInfo(outputPath).fileName()));
    log(QStringLiteral("Начата обработка: %1").arg(QFileInfo(inputPath).fileName()));
}

void MainWindow::onProcProgress(qint64 done, qint64 total, double mbps)
{
    const int percent = total > 0 ? int((done * 100) / total) : 100;
    m_progress->setValue(percent);
    m_speedLabel->setText(QStringLiteral("Скорость: %1 МБ/с   (%2 / %3 МБ)")
        .arg(mbps, 0, 'f', 1)
        .arg(done / (1024.0 * 1024.0), 0, 'f', 1)
        .arg(total / (1024.0 * 1024.0), 0, 'f', 1));
}

void MainWindow::onProcStatus(const QString &status)
{
    m_statusLabel->setText(QStringLiteral("Статус: %1").arg(status));
}

void MainWindow::onProcFinished(const QString &outputPath)
{
    log(QStringLiteral("Готово: %1").arg(QFileInfo(outputPath).fileName()));
}

void MainWindow::onProcFailed(const QString &inputPath, const QString &error)
{
    log(QStringLiteral("Ошибка (%1): %2").arg(QFileInfo(inputPath).fileName(), error));
}

void MainWindow::onProcCanceled(const QString &inputPath)
{
    log(QStringLiteral("Отменено: %1").arg(QFileInfo(inputPath).fileName()));
}

void MainWindow::onProcIdle()
{
    // Обработчик освободился: сбрасываем состояние и берём следующий файл.
    m_busy = false;
    m_pauseBtn->setText(QStringLiteral("Пауза"));
    m_pauseBtn->setEnabled(false);
    m_statusLabel->setText(QStringLiteral("Статус: ожидание"));

    if (!m_running) {
        setRunningState(false);
        return;
    }
    if (!m_queue.isEmpty()) {
        startNextIfPossible();
    } else if (!m_timerMode) {
        log(QStringLiteral("Все файлы обработаны."));
        m_running = false;
        setRunningState(false);
    }
    // В режиме таймера остаёмся в работе и ждём новые файлы.
}

void MainWindow::setRunningState(bool running)
{
    m_startBtn->setEnabled(!running);
    m_stopBtn->setEnabled(running);
    m_pauseBtn->setEnabled(false);

    const QList<QWidget*> inputs = {m_maskEdit, m_inputDirEdit, m_outputDirEdit,
                                    m_removeInputBox, m_conflictBox, m_modeSingle,
                                    m_modeTimer, m_keyEdit};
    for (QWidget *w : inputs)
        w->setEnabled(!running);
    m_intervalSpin->setEnabled(!running && m_modeTimer->isChecked());

    if (!running) {
        m_progress->setValue(0);
        m_fileLabel->setText(QStringLiteral("Файл: —"));
        m_speedLabel->setText(QStringLiteral("Скорость: —"));
    }
}

void MainWindow::stopWorker()
{
    if (m_thread.isRunning()) {
        if (m_busy)
            m_processor->requestCancel();      // прервать текущий файл
        m_thread.quit();
        m_thread.wait();                       // дождаться реального завершения потока
    }
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (m_busy) {
        const auto answer = QMessageBox::question(
            this, QStringLiteral("Идёт обработка"),
            QStringLiteral("Файл ещё обрабатывается. Прервать и выйти?"),
            QMessageBox::Yes | QMessageBox::No);
        if (answer != QMessageBox::Yes) {
            event->ignore();
            return;
        }
    }
    m_running = false;
    m_pollTimer->stop();
    stopWorker();                              // корректно останавливаем поток и ждём
    event->accept();
}
