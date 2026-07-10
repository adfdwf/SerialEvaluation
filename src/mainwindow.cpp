
#include "mainwindow.h"
#include "serialclientworker.h"
#include "tcpclientworker.h"
#include "ui_mainwindow.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QMetaObject>
#include <QScrollBar>
#include <QSerialPortInfo>
#include <QTextStream>

BEGIN_NAMESPACE_CIQTEK

/**
 * @brief  MainWindow default constructor
 * @param  parent Qt parent object
 * @return void
 */
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    setupUiLogic();
    applyIndustrialTheme();
    slotRefreshSerialPorts();
    slotModeChanged(ui->comboBoxMode->currentIndex());
    setConnectionState(ConnectionState::Disconnected);
    updateStatsView();
}

/**
 * @brief  MainWindow default destructor
 * @return void
 */
MainWindow::~MainWindow()
{
    stopTest(true);
    destroyWorker();
    delete ui;
}

/**
 * @brief  setupUiLogic initializes UI business logic
 * @return void
 */
void MainWindow::setupUiLogic()
{
    m_sendTimer = new QTimer(this);
    m_timeoutTimer = new QTimer(this);
    m_timeoutTimer->setInterval(200);

    ui->spinBoxPort->setRange(1, 65535);
    ui->spinBoxPort->setValue(10160);
    ui->spinBoxInterval->setRange(1, 600000);
    ui->spinBoxInterval->setValue(1000);
    ui->spinBoxSendCount->setRange(0, 100000000);
    ui->spinBoxSendCount->setValue(0);
    ui->spinBoxTimeout->setRange(100, 600000);
    ui->spinBoxTimeout->setValue(300);

    ui->comboBoxPayloadMode->addItems({QStringLiteral("ASCII"), QStringLiteral("HEX")});
    ui->comboBoxMode->addItems({QStringLiteral("TCP Network"), QStringLiteral("Serial Port")});
    ui->comboBoxBaudRate->addItems({QStringLiteral("9600"), QStringLiteral("19200"), QStringLiteral("38400"), QStringLiteral("57600"), QStringLiteral("115200"), QStringLiteral("230400"), QStringLiteral("460800"), QStringLiteral("921600")});
    ui->comboBoxBaudRate->setCurrentText(QStringLiteral("115200"));
    ui->comboBoxDataBits->addItems({QStringLiteral("5"), QStringLiteral("6"), QStringLiteral("7"), QStringLiteral("8")});
    ui->comboBoxDataBits->setCurrentText(QStringLiteral("8"));
    ui->comboBoxStopBits->addItems({QStringLiteral("1"), QStringLiteral("1.5"), QStringLiteral("2")});
    ui->comboBoxParity->addItems({QStringLiteral("None"), QStringLiteral("Even"), QStringLiteral("Odd"), QStringLiteral("Mark"), QStringLiteral("Space")});

    ui->lineEditIp->setText(QStringLiteral("172.16.32.231"));
    ui->lineEditCommand->setText(QStringLiteral("PING"));
    ui->pushButtonStop->setEnabled(false);

    QObject::connect(ui->comboBoxMode, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::slotModeChanged);
    QObject::connect(ui->pushButtonRefreshPorts, &QPushButton::clicked, this, &MainWindow::slotRefreshSerialPorts);
    QObject::connect(ui->pushButtonConnect, &QPushButton::clicked, this, &MainWindow::slotConnectClicked);
    QObject::connect(ui->pushButtonDisconnect, &QPushButton::clicked, this, &MainWindow::slotDisconnectClicked);
    QObject::connect(ui->pushButtonStart, &QPushButton::clicked, this, &MainWindow::slotStartClicked);
    QObject::connect(ui->pushButtonStop, &QPushButton::clicked, this, &MainWindow::slotStopClicked);
    QObject::connect(m_sendTimer, &QTimer::timeout, this, &MainWindow::slotSendNextPacket);
    QObject::connect(m_timeoutTimer, &QTimer::timeout, this, &MainWindow::slotCheckTimeouts);
}

/**
 * @brief  applyIndustrialTheme applies the industrial UI theme
 * @return void
 */
void MainWindow::applyIndustrialTheme()
{
    setWindowTitle(QStringLiteral("CommBench Pro - Industrial Communication Benchmark"));
    resize(1280, 780);

    setStyleSheet(QStringLiteral(R"(
        QMainWindow { background: #151a1f; }
        QWidget { color: #d9e2ec; font-family: "Microsoft YaHei", "Segoe UI"; font-size: 10pt; }
        QGroupBox { border: 1px solid #36424f; border-radius: 8px; margin-top: 12px; padding: 12px; background: #1d242c; font-weight: 600; }
        QGroupBox::title { subcontrol-origin: margin; left: 12px; padding: 0 6px; color: #9fb3c8; }
        QLineEdit, QSpinBox, QComboBox, QTextEdit { background: #0f1419; border: 1px solid #3d4b59; border-radius: 6px; padding: 6px; selection-background-color: #2f80ed; }
        QTextEdit { font-family: "Cascadia Mono", "Consolas", monospace; }
        QPushButton { background: #2a3642; border: 1px solid #4b5f73; border-radius: 7px; padding: 8px 14px; font-weight: 600; }
        QPushButton:hover { background: #344457; }
        QPushButton:pressed { background: #1f8f63; }
        QPushButton:disabled { color: #697886; background: #202830; border-color: #2d3844; }
        QPushButton#pushButtonStart { background: #0f8a5f; border-color: #19b982; color: white; }
        QPushButton#pushButtonStop { background: #a23b3b; border-color: #d05a5a; color: white; }
        QLabel#labelTotalSentValue, QLabel#labelSuccessValue, QLabel#labelLostValue, QLabel#labelRateValue,
        QLabel#labelAverageValue, QLabel#labelMaxValue, QLabel#labelMinValue { font-size: 18pt; font-weight: 800; color: #f8fafc; }
        QFrame#frameLed { border-radius: 8px; min-width: 16px; min-height: 16px; max-width: 16px; max-height: 16px; }
    )"));
}

/**
 * @brief  slotModeChanged handles communication mode changes
 * @param  index Current mode index
 * @return void
 */
void MainWindow::slotModeChanged(int index)
{
    const bool serialMode = index == 1;
    ui->widgetTcpConfig->setVisible(!serialMode);
    ui->widgetSerialConfig->setVisible(serialMode);
    destroyWorker();
    setConnectionState(ConnectionState::Disconnected);
}

/**
 * @brief  slotRefreshSerialPorts refreshes the serial port list
 * @return void
 */
void MainWindow::slotRefreshSerialPorts()
{
    const QString previous = ui->comboBoxSerialPort->currentText();
    ui->comboBoxSerialPort->clear();
    const QList<QSerialPortInfo> ports = QSerialPortInfo::availablePorts();
    for (const QSerialPortInfo &info : ports) {
        ui->comboBoxSerialPort->addItem(info.portName());
    }
    if (!previous.isEmpty()) {
        const int index = ui->comboBoxSerialPort->findText(previous);
        if (index >= 0) {
            ui->comboBoxSerialPort->setCurrentIndex(index);
        }
    }
}

/**
 * @brief  slotConnectClicked handles the connect button
 * @return void
 */
void MainWindow::slotConnectClicked()
{
    destroyWorker();
    createWorker();
    if (!m_workerObject) {
        return;
    }

    setConnectionState(ConnectionState::Connecting);
    appendLog(LogLevel::Info, QStringLiteral("正在连接：%1").arg(currentConfigDescription()));
    QMetaObject::invokeMethod(m_workerObject, "connect", Qt::QueuedConnection);
}

/**
 * @brief  slotDisconnectClicked handles the disconnect button
 * @return void
 */
void MainWindow::slotDisconnectClicked()
{
    stopTest(true);
    destroyWorker();
    setConnectionState(ConnectionState::Disconnected);
    appendLog(LogLevel::Info, QStringLiteral("连接已断开"));
}

/**
 * @brief  slotStartClicked starts the benchmark test
 * @return void
 */
void MainWindow::slotStartClicked()
{
    if (!m_connected) {
        appendLog(LogLevel::Error, QStringLiteral("请先建立连接后再开始测试"));
        return;
    }

    const QByteArray payload = buildPayload();
    if (payload.isEmpty()) {
        appendLog(LogLevel::Error, QStringLiteral("发送指令为空或 HEX 格式无效"));
        return;
    }

    ui->textEditLog->clear();
    m_statistics.reset();
    m_testRunning = true;
    m_finishingAfterLimit = false;
    ui->pushButtonStart->setEnabled(false);
    ui->pushButtonStop->setEnabled(true);
    ui->pushButtonConnect->setEnabled(false);
    ui->comboBoxMode->setEnabled(false);
    m_sendTimer->setInterval(ui->spinBoxInterval->value());
    m_timeoutTimer->start();

    appendLog(LogLevel::Info, QStringLiteral("测试开始：%1，间隔 %2 ms，次数 %3")
                                 .arg(currentModeDescription())
                                 .arg(ui->spinBoxInterval->value())
                                 .arg(ui->spinBoxSendCount->value() == 0 ? QStringLiteral("无限") : QString::number(ui->spinBoxSendCount->value())));
    slotSendNextPacket();
    if (m_testRunning && !m_finishingAfterLimit) {
        m_sendTimer->start();
    }
}

/**
 * @brief  slotStopClicked stops the benchmark test
 * @return void
 */
void MainWindow::slotStopClicked()
{
    stopTest(true);
}

/**
 * @brief  slotSendNextPacket sends the next packet on timer
 * @return void
 */
void MainWindow::slotSendNextPacket()
{
    if (!m_testRunning || !m_workerObject) {
        return;
    }

    const int targetCount = ui->spinBoxSendCount->value();
    if (targetCount > 0 && m_statistics.snapshot().totalSent >= static_cast<quint64>(targetCount)) {
        m_sendTimer->stop();
        m_finishingAfterLimit = true;
        checkFinishAfterLimit();
        return;
    }

    const QByteArray payload = buildPayload();
    if (payload.isEmpty()) {
        appendLog(LogLevel::Error, QStringLiteral("发送指令为空或 HEX 格式无效，测试终止"));
        stopTest(true);
        return;
    }

    const PacketInfo packet = m_statistics.recordSend(payload);
    appendLog(LogLevel::Tx, QStringLiteral("#%1 %2").arg(packet.id).arg(payloadToDisplay(payload)));
    QMetaObject::invokeMethod(m_workerObject, "sendData", Qt::QueuedConnection, Q_ARG(QByteArray, payload));
    updateStatsView();

    if (targetCount > 0 && packet.id >= static_cast<quint64>(targetCount)) {
        m_sendTimer->stop();
        m_finishingAfterLimit = true;
        appendLog(LogLevel::Info, QStringLiteral("已达到发送次数，等待剩余回包或超时"));
        checkFinishAfterLimit();
    }
}

/**
 * @brief  slotCheckTimeouts checks pending packets for timeout
 * @return void
 */
void MainWindow::slotCheckTimeouts()
{
    if (!m_testRunning) {
        return;
    }

    const QVector<PacketInfo> timedOut = m_statistics.markTimeouts(ui->spinBoxTimeout->value());
    for (const PacketInfo &packet : timedOut) {
        appendLog(LogLevel::Error, QStringLiteral("#%1 超时/丢包").arg(packet.id), packet.elapsedMs);
    }
    if (!timedOut.isEmpty()) {
        updateStatsView();
    }
    checkFinishAfterLimit();
}

/**
 * @brief  slotWorkerConnected handles worker connected signal
 * @return void
 */
void MainWindow::slotWorkerConnected()
{
    m_connected = true;
    setConnectionState(ConnectionState::Connected);
    appendLog(LogLevel::Info, QStringLiteral("连接成功"));
}

/**
 * @brief  slotWorkerDisconnected handles worker disconnected signal
 * @return void
 */
void MainWindow::slotWorkerDisconnected()
{
    m_connected = false;
    if (m_testRunning) {
        stopTest(true);
    }
    setConnectionState(ConnectionState::Disconnected);
    appendLog(LogLevel::Info, QStringLiteral("远端已断开连接"));
}

/**
 * @brief  slotWorkerDataReceived handles data received from worker
 * @param  data Received bytes
 * @return void
 */
void MainWindow::slotWorkerDataReceived(const QByteArray &data)
{
    PacketInfo packet;
    if (m_statistics.recordReceive(data, &packet)) {
        appendLog(LogLevel::Rx, QStringLiteral("#%1 %2").arg(packet.id).arg(payloadToDisplay(data)), packet.elapsedMs);
    } else {
        appendLog(LogLevel::Rx, QStringLiteral("未匹配回包 %1").arg(payloadToDisplay(data)));
    }
    updateStatsView();
    checkFinishAfterLimit();
}

/**
 * @brief  slotWorkerError handles worker error message
 * @param  message Error text
 * @return void
 */
void MainWindow::slotWorkerError(const QString &message)
{
    appendLog(LogLevel::Error, message);
}

/**
 * @brief  setConnectionState updates connection state and UI
 * @param  state New connection state
 * @return void
 */
void MainWindow::setConnectionState(ConnectionState state)
{
    QString text;
    QString color;
    switch (state) {
    case ConnectionState::Disconnected:
        m_connected = false;
        text = QStringLiteral("Disconnected");
        color = QStringLiteral("#b64d4d");
        ui->pushButtonConnect->setEnabled(true);
        ui->pushButtonDisconnect->setEnabled(false);
        break;
    case ConnectionState::Connecting:
        text = QStringLiteral("Connecting");
        color = QStringLiteral("#d7a72f");
        ui->pushButtonConnect->setEnabled(false);
        ui->pushButtonDisconnect->setEnabled(true);
        break;
    case ConnectionState::Connected:
        m_connected = true;
        text = QStringLiteral("Connected");
        color = QStringLiteral("#19b982");
        ui->pushButtonConnect->setEnabled(false);
        ui->pushButtonDisconnect->setEnabled(true);
        break;
    }

    ui->labelConnectionState->setText(text);
    ui->frameLed->setStyleSheet(QStringLiteral("background:%1; border:1px solid rgba(255,255,255,0.35); border-radius:8px;").arg(color));
}

/**
 * @brief  createWorker creates communication worker for current mode
 * @return void
 */
void MainWindow::createWorker()
{
    m_commThread = new QThread(this);

    if (ui->comboBoxMode->currentIndex() == 0) {
        auto *worker = new TcpClientWorker(ui->lineEditIp->text().trimmed(), static_cast<quint16>(ui->spinBoxPort->value()));
        m_workerObject = worker;
        m_commInterface = worker;
    } else {
        if (ui->comboBoxSerialPort->currentText().isEmpty()) {
            appendLog(LogLevel::Error, QStringLiteral("没有可用串口，请刷新后重试"));
            delete m_commThread;
            m_commThread = nullptr;
            return;
        }
        SerialSettings settings;
        settings.portName = ui->comboBoxSerialPort->currentText();
        settings.baudRate = ui->comboBoxBaudRate->currentText().toInt();
        settings.dataBits = ui->comboBoxDataBits->currentText().toInt() == 7 ? QSerialPort::Data7 :
                            ui->comboBoxDataBits->currentText().toInt() == 6 ? QSerialPort::Data6 :
                            ui->comboBoxDataBits->currentText().toInt() == 5 ? QSerialPort::Data5 : QSerialPort::Data8;
        settings.stopBits = ui->comboBoxStopBits->currentText() == QStringLiteral("2") ? QSerialPort::TwoStop :
                            ui->comboBoxStopBits->currentText() == QStringLiteral("1.5") ? QSerialPort::OneAndHalfStop : QSerialPort::OneStop;
        settings.parity = ui->comboBoxParity->currentText() == QStringLiteral("Even") ? QSerialPort::EvenParity :
                          ui->comboBoxParity->currentText() == QStringLiteral("Odd") ? QSerialPort::OddParity :
                          ui->comboBoxParity->currentText() == QStringLiteral("Mark") ? QSerialPort::MarkParity :
                          ui->comboBoxParity->currentText() == QStringLiteral("Space") ? QSerialPort::SpaceParity : QSerialPort::NoParity;

        auto *worker = new SerialClientWorker(settings);
        m_workerObject = worker;
        m_commInterface = worker;
    }
    // Move the worker to the communication thread so queued I/O runs off the UI thread.
    m_workerObject->moveToThread(m_commThread);
    QObject::connect(m_commThread, &QThread::finished, m_workerObject, &QObject::deleteLater);
    QObject::connect(m_workerObject, SIGNAL(signalConnected()), this, SLOT(slotWorkerConnected()));
    QObject::connect(m_workerObject, SIGNAL(signalDisconnected()), this, SLOT(slotWorkerDisconnected()));
    QObject::connect(m_workerObject, SIGNAL(signalDataReceived(QByteArray)), this, SLOT(slotWorkerDataReceived(QByteArray)));
    QObject::connect(m_workerObject, SIGNAL(signalErrorOccurred(QString)), this, SLOT(slotWorkerError(QString)));
    m_commThread->start();
}

/**
 * @brief  destroyWorker releases communication worker and thread
 * @return void
 */
void MainWindow::destroyWorker()
{
    if (!m_commThread) {
        m_workerObject = nullptr;
        m_commInterface = nullptr;
        return;
    }

    if (m_workerObject) {
        QMetaObject::invokeMethod(m_workerObject, "disconnect", Qt::BlockingQueuedConnection);
    }

    m_commThread->quit();
    m_commThread->wait(3000);
    delete m_commThread;
    m_commThread = nullptr;
    m_workerObject = nullptr;
    m_commInterface = nullptr;
    m_connected = false;
}

/**
 * @brief  updateStatsView refreshes statistic labels
 * @return void
 */
void MainWindow::updateStatsView()
{
    const StatisticsSnapshot snap = m_statistics.snapshot();
    ui->labelTotalSentValue->setText(QString::number(snap.totalSent));
    ui->labelSuccessValue->setText(QString::number(snap.successReceived));
    ui->labelLostValue->setText(QString::number(snap.lostPackets));
    ui->labelRateValue->setText(QStringLiteral("%1%").arg(snap.successRate, 0, 'f', 2));
    ui->labelAverageValue->setText(QStringLiteral("%1 ms").arg(snap.averageElapsedMs, 0, 'f', 2));
    ui->labelMaxValue->setText(QStringLiteral("%1 ms").arg(snap.maxElapsedMs));
    ui->labelMinValue->setText(QStringLiteral("%1 ms").arg(snap.minElapsedMs));
}

/**
 * @brief  appendLog appends one log entry to the log view
 * @param  level Log level
 * @param  text Log text
 * @param  elapsedMs Optional elapsed time in milliseconds
 * @return void
 */
void MainWindow::appendLog(LogLevel level, const QString &text, qint64 elapsedMs)
{
    QString tag;
    QString color;
    switch (level) {
    case LogLevel::Tx:
        tag = QStringLiteral("TX");
        color = QStringLiteral("#5fa8ff");
        break;
    case LogLevel::Rx:
        tag = QStringLiteral("RX");
        color = QStringLiteral("#42d392");
        break;
    case LogLevel::Error:
        tag = QStringLiteral("ERR");
        color = QStringLiteral("#ff6b6b");
        break;
    case LogLevel::Info:
        tag = QStringLiteral("INFO");
        color = QStringLiteral("#d7a72f");
        break;
    }

    const QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"));
    const QString elapsed = elapsedMs >= 0 ? QStringLiteral(" [耗时: %1 ms]").arg(elapsedMs) : QString();
    ui->textEditLog->append(QStringLiteral("<span style='color:#7b8794'>[%1]</span> <span style='color:%2'>[%3]</span> %4%5")
                                .arg(timestamp, color, tag, htmlEscape(text), elapsed));
    ui->textEditLog->verticalScrollBar()->setValue(ui->textEditLog->verticalScrollBar()->maximum());
}

/**
 * @brief  stopTest stops current test workflow
 * @param  manualStop True when stopped by user
 * @return void
 */
void MainWindow::stopTest(bool manualStop)
{
    if (!m_testRunning) {
        return;
    }

    m_sendTimer->stop();
    m_timeoutTimer->stop();
    m_testRunning = false;
    m_finishingAfterLimit = false;

    if (manualStop) {
        const QVector<PacketInfo> lost = m_statistics.markAllPendingLost();
        for (const PacketInfo &packet : lost) {
            appendLog(LogLevel::Error, QStringLiteral("#%1 手动停止，标记为丢包").arg(packet.id));
        }
    }

    updateStatsView();
    finalizeTestReport();
    ui->pushButtonStart->setEnabled(true);
    ui->pushButtonStop->setEnabled(false);
    ui->pushButtonConnect->setEnabled(!m_connected);
    ui->comboBoxMode->setEnabled(true);
    appendLog(LogLevel::Info, QStringLiteral("测试结束"));
}

/**
 * @brief  finalizeTestReport writes the test report file
 * @return void
 */
void MainWindow::finalizeTestReport()
{
    const QString fileName = QStringLiteral("CommReport_%1.txt").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss")));
    QDir logDir(QDir::current().filePath(QStringLiteral("Log")));
    if (!logDir.exists() && !logDir.mkpath(QStringLiteral("."))) {
        appendLog(LogLevel::Error, QStringLiteral("日志目录创建失败：%1").arg(logDir.absolutePath()));
        return;
    }

    const QString filePath = logDir.filePath(fileName);
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        appendLog(LogLevel::Error, QStringLiteral("报告写入失败：%1").arg(file.errorString()));
        return;
    }

    QTextStream out(&file);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    out.setCodec("UTF-8");
#endif
    const StatisticsSnapshot snap = m_statistics.snapshot();
    out << "CommBench Pro Test Report" << Qt::endl;
    out << "Generated: " << QDateTime::currentDateTime().toString(Qt::ISODateWithMs) << Qt::endl;
    out << "Mode: " << currentModeDescription() << Qt::endl;
    out << "Config: " << currentConfigDescription() << Qt::endl << Qt::endl;
    out << "Summary" << Qt::endl;
    out << "Total Sent: " << snap.totalSent << Qt::endl;
    out << "Success Received: " << snap.successReceived << Qt::endl;
    out << "Lost Packets: " << snap.lostPackets << Qt::endl;
    out << "Success Rate: " << QString::number(snap.successRate, 'f', 2) << "%" << Qt::endl;
    out << "Average Elapsed: " << QString::number(snap.averageElapsedMs, 'f', 2) << " ms" << Qt::endl;
    out << "Min Elapsed: " << snap.minElapsedMs << " ms" << Qt::endl;
    out << "Max Elapsed: " << snap.maxElapsedMs << " ms" << Qt::endl << Qt::endl;
    out << "Packet Details" << Qt::endl;
    out << "ID,SentAt,ReceivedAt,ElapsedMs,Status,TX,RX" << Qt::endl;

    for (const PacketInfo &packet : m_statistics.packets()) {
        const QString status = packet.status == PacketInfo::Status::Success ? QStringLiteral("Success") :
                               packet.status == PacketInfo::Status::Timeout ? QStringLiteral("Timeout/Lost") : QStringLiteral("Pending");
        out << packet.id << ','
            << packet.sentAt.toString(Qt::ISODateWithMs) << ','
            << packet.receivedAt.toString(Qt::ISODateWithMs) << ','
            << packet.elapsedMs << ','
            << status << ','
            << QString::fromLatin1(packet.txPayload.toHex(' ')) << ','
            << QString::fromLatin1(packet.rxPayload.toHex(' ')) << Qt::endl;
    }

    appendLog(LogLevel::Info, QStringLiteral("报告已生成：%1").arg(filePath));
}

/**
 * @brief  checkFinishAfterLimit finishes after target count and pending packets are done
 * @return void
 */
void MainWindow::checkFinishAfterLimit()
{
    if (m_testRunning && m_finishingAfterLimit && !m_statistics.hasPendingPackets()) {
        stopTest(false);
    }
}

/**
 * @brief  currentModeDescription returns current communication mode text
 * @return QString Mode text
 */
QString MainWindow::currentModeDescription() const
{
    return ui->comboBoxMode->currentIndex() == 0 ? QStringLiteral("TCP Network") : QStringLiteral("Serial Port");
}

/**
 * @brief  currentConfigDescription returns current communication configuration text
 * @return QString Configuration text
 */
QString MainWindow::currentConfigDescription() const
{
    if (ui->comboBoxMode->currentIndex() == 0) {
        return QStringLiteral("%1:%2").arg(ui->lineEditIp->text().trimmed()).arg(ui->spinBoxPort->value());
    }

    return QStringLiteral("%1, %2 bps, %3 data, %4 stop, %5 parity")
        .arg(ui->comboBoxSerialPort->currentText(), ui->comboBoxBaudRate->currentText(), ui->comboBoxDataBits->currentText(), ui->comboBoxStopBits->currentText(), ui->comboBoxParity->currentText());
}

/**
 * @brief  buildPayload builds payload from UI input
 * @return QByteArray Payload bytes
 */
QByteArray MainWindow::buildPayload() const
{
    const QString text = ui->lineEditCommand->text();
    if (ui->comboBoxPayloadMode->currentText() == QStringLiteral("HEX")) {
        QString compact = text;
        compact.remove(' ');
        compact.remove('\t');
        compact.remove('\r');
        compact.remove('\n');
        if (compact.isEmpty() || compact.size() % 2 != 0) {
            return {};
        }
        for (const QChar ch : compact) {
            const ushort c = ch.toLower().unicode();
            if (!ch.isDigit() && (c < 'a' || c > 'f')) {
                return {};
            }
        }
        return QByteArray::fromHex(compact.toLatin1());
    }
    return text.toUtf8();
}

/**
 * @brief  payloadToDisplay converts payload to log display text
 * @param  payload Communication payload
 * @return QString Display text
 */
QString MainWindow::payloadToDisplay(const QByteArray &payload) const
{
    if (ui->comboBoxPayloadMode->currentText() == QStringLiteral("HEX")) {
        return QString::fromLatin1(payload.toHex(' ').toUpper());
    }
    return QString::fromUtf8(payload);
}

/**
 * @brief  htmlEscape escapes HTML special characters for log output
 * @param  value Raw text
 * @return QString Escaped text
 */
QString MainWindow::htmlEscape(const QString &value) const
{
    QString escaped = value;
    escaped.replace('&', QStringLiteral("&amp;"));
    escaped.replace('<', QStringLiteral("&lt;"));
    escaped.replace('>', QStringLiteral("&gt;"));
    escaped.replace('"', QStringLiteral("&quot;"));
    return escaped;
}

END_NAMESPACE_CIQTEK
