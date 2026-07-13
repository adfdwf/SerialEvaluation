#include "mainwindow.h"
#include "serialclientworker.h"
#include "tcpclientworker.h"
#include "ui_mainwindow.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMetaObject>
#include <QScrollBar>
#include <QSerialPortInfo>
#include <QTableWidget>
#include <QRadioButton>
#include <QTextStream>
#include <QVBoxLayout>

BEGIN_NAMESPACE_CIQTEK

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    setupUiLogic();
    applyIndustrialTheme();
    slotRefreshSerialPorts();
    slotModeChanged(ui->comboBoxMode->currentIndex());
    setConnectionState(ConnectionState::Disconnected);
    scheduleStatsRefresh();
}

MainWindow::~MainWindow()
{
    stopTest(true);
    destroyWorker();
    delete ui;
}

void MainWindow::setupUiLogic()
{
    m_sendTimer = new QTimer(this);
    m_timeoutTimer = new QTimer(this);
    m_timeoutTimer->setInterval(200);
    m_statsTimer = new QTimer(this);
    m_statsTimer->setInterval(100);
    QObject::connect(m_statsTimer, &QTimer::timeout, this, &MainWindow::updateStatsView);

    ui->spinBoxPort->setRange(1, 65535);
    ui->spinBoxPort->setValue(10160);
    ui->spinBoxInterval->setRange(1, 600000);
    ui->spinBoxInterval->setValue(1000);
    ui->spinBoxSendCount->setRange(0, 100000000);
    ui->spinBoxSendCount->setValue(0);
    ui->spinBoxTimeout->setRange(100, 600000);
    ui->spinBoxTimeout->setValue(300);

    ui->comboBoxMode->addItems({QStringLiteral("TCP Network"), QStringLiteral("Serial Port")});
    ui->comboBoxBaudRate->addItems({QStringLiteral("9600"), QStringLiteral("19200"), QStringLiteral("38400"), QStringLiteral("57600"), QStringLiteral("115200"), QStringLiteral("230400"), QStringLiteral("460800"), QStringLiteral("921600")});
    ui->comboBoxBaudRate->setCurrentText(QStringLiteral("115200"));
    ui->comboBoxDataBits->addItems({QStringLiteral("5"), QStringLiteral("6"), QStringLiteral("7"), QStringLiteral("8")});
    ui->comboBoxDataBits->setCurrentText(QStringLiteral("8"));
    ui->comboBoxStopBits->addItems({QStringLiteral("1"), QStringLiteral("1.5"), QStringLiteral("2")});
    ui->comboBoxParity->addItems({QStringLiteral("None"), QStringLiteral("Even"), QStringLiteral("Odd"), QStringLiteral("Mark"), QStringLiteral("Space")});

    ui->lineEditIp->setText(QStringLiteral("172.16.32.231"));

    setupCommandTable();
    setupStatsLabels();

    ui->pushButtonStop->setEnabled(false);

    QObject::connect(ui->comboBoxMode, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::slotModeChanged);
    QObject::connect(ui->pushButtonRefreshPorts, &QPushButton::clicked, this, &MainWindow::slotRefreshSerialPorts);
    QObject::connect(ui->pushButtonConnect, &QPushButton::clicked, this, &MainWindow::slotConnectClicked);
    QObject::connect(ui->pushButtonDisconnect, &QPushButton::clicked, this, &MainWindow::slotDisconnectClicked);
    QObject::connect(ui->pushButtonStart, &QPushButton::clicked, this, &MainWindow::slotStartClicked);
    QObject::connect(ui->pushButtonStop, &QPushButton::clicked, this, &MainWindow::slotStopClicked);
    QObject::connect(ui->pushButtonSendAll, &QPushButton::clicked, this, &MainWindow::slotSendAllClicked);
    QObject::connect(ui->pushButtonAddCommand, &QPushButton::clicked, this, &MainWindow::slotAddCommandRow);
    QObject::connect(ui->pushButtonRemoveCommand, &QPushButton::clicked, this, &MainWindow::slotRemoveCommandRow);
    QObject::connect(m_sendTimer, &QTimer::timeout, this, &MainWindow::slotSendNextPacket);
    QObject::connect(m_timeoutTimer, &QTimer::timeout, this, &MainWindow::slotCheckTimeouts);
}

void MainWindow::setupCommandTable()
{
    QStringList headers;
    headers << QStringLiteral("#")
            << QStringLiteral("Command")
            << QStringLiteral("ASCII")
            << QStringLiteral("HEX");

    ui->tableCommands->setColumnCount(4);
    ui->tableCommands->setHorizontalHeaderLabels(headers);
    ui->tableCommands->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    ui->tableCommands->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    ui->tableCommands->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    ui->tableCommands->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    ui->tableCommands->verticalHeader()->setVisible(false);
    ui->tableCommands->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->tableCommands->setSelectionMode(QAbstractItemView::SingleSelection);

    slotAddCommandRow();
}

void MainWindow::slotAddCommandRow()
{
    const int row = ui->tableCommands->rowCount();
    ui->tableCommands->insertRow(row);

    QTableWidgetItem *numItem = new QTableWidgetItem(QString::number(row + 1));
    numItem->setFlags(numItem->flags() & ~Qt::ItemIsEditable);
    numItem->setTextAlignment(Qt::AlignCenter);
    ui->tableCommands->setItem(row, 0, numItem);

    QLineEdit *commandEdit = new QLineEdit();
    commandEdit->setPlaceholderText(QStringLiteral("A0 81 01 00 00 00 00 00 22"));
    commandEdit->setStyleSheet(QStringLiteral("background: #ffffff; color: #000000; border: 1px solid #cccccc; border-radius: 4px; padding: 4px;"));
    QObject::connect(commandEdit, &QLineEdit::textChanged, this, &MainWindow::slotCommandTextChanged);
    ui->tableCommands->setCellWidget(row, 1, commandEdit);

    QRadioButton *asciiRadio = new QRadioButton(QStringLiteral("ASCII"));
    asciiRadio->setChecked(true);
    QRadioButton *hexRadio = new QRadioButton(QStringLiteral("HEX"));
    QObject::connect(asciiRadio, &QRadioButton::toggled, this, &MainWindow::slotHexModeToggled);
    QObject::connect(hexRadio, &QRadioButton::toggled, this, &MainWindow::slotHexModeToggled);

    QWidget *modeWidget = new QWidget();
    QHBoxLayout *modeLayout = new QHBoxLayout(modeWidget);
    modeLayout->setContentsMargins(4, 0, 4, 0);
    modeLayout->setSpacing(4);
    modeLayout->addWidget(asciiRadio);
    modeLayout->addWidget(hexRadio);
    modeLayout->addStretch();
    ui->tableCommands->setCellWidget(row, 2, modeWidget);

    QWidget *sendWidget = new QWidget();
    QHBoxLayout *sendLayout = new QHBoxLayout(sendWidget);
    sendLayout->setContentsMargins(4, 0, 4, 0);
    QPushButton *sendOneBtn = new QPushButton(QStringLiteral("Send"));
    sendOneBtn->setStyleSheet(QStringLiteral("QPushButton { background: #2a3642; color: white; border-radius: 4px; padding: 4px 12px; }"));
    QObject::connect(sendOneBtn, &QPushButton::clicked, this, [this]() {
        QPushButton *btn = qobject_cast<QPushButton *>(sender());
        if (!btn) return;
        slotSendAllClicked();
    });
    sendLayout->addWidget(sendOneBtn);
    ui->tableCommands->setCellWidget(row, 3, sendWidget);
}

void MainWindow::slotRemoveCommandRow()
{
    const int row = ui->tableCommands->currentRow();
    if (row < 0) {
        if (ui->tableCommands->rowCount() > 1) {
            ui->tableCommands->removeRow(ui->tableCommands->rowCount() - 1);
        }
        return;
    }
    if (ui->tableCommands->rowCount() <= 1) {
        return;
    }
    ui->tableCommands->removeRow(row);
    for (int i = row; i < ui->tableCommands->rowCount(); ++i) {
        QTableWidgetItem *numItem = ui->tableCommands->item(i, 0);
        if (numItem) {
            numItem->setText(QString::number(i + 1));
        }
    }
}

void MainWindow::slotCommandTextChanged()
{
    QLineEdit *edit = qobject_cast<QLineEdit *>(sender());
    if (!edit) return;

    for (int row = 0; row < ui->tableCommands->rowCount(); ++row) {
        if (ui->tableCommands->cellWidget(row, 1) == edit) {
            const QString text = edit->text();
            QWidget *modeWidget = ui->tableCommands->cellWidget(row, 2);
            if (!modeWidget) break;
            QRadioButton *hexRadio = modeWidget->findChild<QRadioButton *>(QString(), Qt::FindChildrenRecursively);
            if (!hexRadio) break;

            QRadioButton *asciiRadio = nullptr;
            for (auto *rb : modeWidget->findChildren<QRadioButton *>()) {
                if (rb->text() == QStringLiteral("ASCII")) {
                    asciiRadio = rb;
                }
            }

            bool hexMode = hexRadio && hexRadio->isChecked();
            bool valid = true;

            if (hexMode) {
                valid = validateHexSyntax(text);
            }

            if (valid) {
                edit->setStyleSheet(QStringLiteral("background: #ffffff; color: #000000; border: 1px solid #cccccc; border-radius: 4px; padding: 4px;"));
            } else {
                edit->setStyleSheet(QStringLiteral("background: #ffe0e0; color: #cc0000; border: 1px solid #ff6666; border-radius: 4px; padding: 4px;"));
            }
            break;
        }
    }
}

void MainWindow::slotHexModeToggled()
{
    QRadioButton *rb = qobject_cast<QRadioButton *>(sender());
    if (!rb) return;
    for (int row = 0; row < ui->tableCommands->rowCount(); ++row) {
        QWidget *modeWidget = ui->tableCommands->cellWidget(row, 2);
        if (!modeWidget) continue;
        for (auto *foundRb : modeWidget->findChildren<QRadioButton *>()) {
            if (foundRb == rb || foundRb->parentWidget() == modeWidget) {
                QLineEdit *edit = qobject_cast<QLineEdit *>(ui->tableCommands->cellWidget(row, 1));
                if (edit) {
                    slotCommandTextChanged();
                }
                return;
            }
        }
    }
}

bool MainWindow::validateHexSyntax(const QString &text)
{
    if (text.trimmed().isEmpty()) {
        return true;
    }
    const QStringList parts = text.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    if (parts.isEmpty()) {
        return true;
    }
    for (const QString &part : parts) {
        if (part.length() % 2 != 0) {
            return false;
        }
        for (const QChar &c : part) {
            if (!isValidHexChar(c)) {
                return false;
            }
        }
    }
    return true;
}

bool MainWindow::isValidHexChar(QChar c)
{
    return (c >= QLatin1Char('0') && c <= QLatin1Char('9')) ||
           (c >= QLatin1Char('a') && c <= QLatin1Char('f')) ||
           (c >= QLatin1Char('A') && c <= QLatin1Char('F'));
}

QList<CommandItem> MainWindow::collectCommands()
{
    QList<CommandItem> commands;
    for (int row = 0; row < ui->tableCommands->rowCount(); ++row) {
        QLineEdit *edit = qobject_cast<QLineEdit *>(ui->tableCommands->cellWidget(row, 1));
        if (!edit || edit->text().trimmed().isEmpty()) continue;

        QWidget *modeWidget = ui->tableCommands->cellWidget(row, 2);
        if (!modeWidget) continue;

        bool hexMode = false;
        for (auto *rb : modeWidget->findChildren<QRadioButton *>()) {
            if (rb->isChecked() && rb->text() == QStringLiteral("HEX")) {
                hexMode = true;
                break;
            }
        }

        bool valid = true;
        if (hexMode) {
            valid = validateHexSyntax(edit->text());
        }

        CommandItem item;
        item.text = edit->text();
        item.hexMode = hexMode;
        item.valid = valid;
        commands.append(item);
    }
    return commands;
}

void MainWindow::slotSendAllClicked()
{
    if (!m_connected) {
        appendLog(LogLevel::Error, QStringLiteral("Please connect first"));
        return;
    }

    const QList<CommandItem> commands = collectCommands();
    if (commands.isEmpty()) {
        appendLog(LogLevel::Error, QStringLiteral("No commands to send"));
        return;
    }

    for (const CommandItem &item : commands) {
        if (!item.valid) {
            appendLog(LogLevel::Error, QStringLiteral("Invalid command syntax"));
            return;
        }
    }

    for (const CommandItem &item : commands) {
        QByteArray payload;
        if (item.hexMode) {
            const QString hexStr = item.text.simplified().remove(QRegularExpression(QStringLiteral("\\s+")));
            payload = QByteArray::fromHex(hexStr.toLatin1());
        } else {
            payload = item.text.toUtf8();
        }

        if (payload.isEmpty()) continue;

        PacketInfo packet = m_statistics.recordSend(payload);
        appendLog(LogLevel::Tx, QStringLiteral("[SendAll] #%1 %2").arg(packet.id).arg(item.text));
        QMetaObject::invokeMethod(m_workerObject, "sendData", Qt::QueuedConnection, Q_ARG(QByteArray, payload));
    }
    scheduleStatsRefresh();
}

void MainWindow::applyIndustrialTheme()
{
    setWindowTitle(QStringLiteral("CommBench Pro - Industrial Communication Benchmark"));
    resize(1280, 780);
}

void MainWindow::slotModeChanged(int index)
{
    const bool serialMode = index == 1;
    ui->widgetTcpConfig->setVisible(!serialMode);
    ui->widgetSerialConfig->setVisible(serialMode);
    destroyWorker();
    setConnectionState(ConnectionState::Disconnected);
}

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

void MainWindow::slotConnectClicked()
{
    destroyWorker();
    createWorker();
    if (!m_workerObject) {
        return;
    }

    setConnectionState(ConnectionState::Connecting);
    appendLog(LogLevel::Info, QStringLiteral("Connecting: %1").arg(currentConfigDescription()));
    QMetaObject::invokeMethod(m_workerObject, "connect", Qt::QueuedConnection);
}

void MainWindow::slotDisconnectClicked()
{
    stopTest(true);
    destroyWorker();
    setConnectionState(ConnectionState::Disconnected);
    appendLog(LogLevel::Info, QStringLiteral("Connection closed"));
}

void MainWindow::slotStartClicked()
{
    if (!m_connected) {
        appendLog(LogLevel::Error, QStringLiteral("Please connect first"));
        return;
    }

    const QList<CommandItem> commands = collectCommands();
    if (commands.isEmpty()) {
        appendLog(LogLevel::Error, QStringLiteral("No valid commands"));
        return;
    }

    for (const CommandItem &item : commands) {
        if (!item.valid) {
            appendLog(LogLevel::Error, QStringLiteral("Invalid command syntax, please fix highlighted cells"));
            return;
        }
    }

    m_currentCommandIndex = 0;
    m_totalCommands = commands.size();

    ui->textEditLog->clear();
    m_statistics.reset();
    m_testRunning = true;
    m_finishingAfterLimit = false;
    ui->pushButtonStart->setEnabled(false);
    ui->pushButtonStop->setEnabled(true);
    ui->pushButtonConnect->setEnabled(false);
    ui->comboBoxMode->setEnabled(false);
    ui->pushButtonSendAll->setEnabled(false);
    m_timeoutTimer->start();

    appendLog(LogLevel::Info, QStringLiteral("Test started: %1, interval %2 ms, count %3, %4 commands")
                                 .arg(currentModeDescription())
                                 .arg(ui->spinBoxInterval->value())
                                 .arg(ui->spinBoxSendCount->value() == 0 ? QStringLiteral("unlimited") : QString::number(ui->spinBoxSendCount->value()))
                                 .arg(commands.size()));

    // 在启动定时器前设置 Interval，确保实时生效
    m_sendTimer->setInterval(ui->spinBoxInterval->value());
    slotSendNextPacket();
    if (m_testRunning && !m_finishingAfterLimit) {
        m_sendTimer->start();
    }
}

void MainWindow::slotStopClicked()
{
    stopTest(true);
}

void MainWindow::slotSendNextPacket()
{
    if (!m_testRunning || !m_workerObject) {
        return;
    }

    // 每次发送前重新读取 Interval，确保用户动态调整实时生效
    const int intervalMs = ui->spinBoxInterval->value();
    m_sendTimer->setInterval(intervalMs);

    const int targetCount = ui->spinBoxSendCount->value();
    if (targetCount > 0 && m_statistics.snapshot().totalSent >= static_cast<quint64>(targetCount)) {
        m_sendTimer->stop();
        m_finishingAfterLimit = true;
        checkFinishAfterLimit();
        return;
    }

    const QList<CommandItem> commands = collectCommands();
    if (commands.isEmpty()) {
        appendLog(LogLevel::Error, QStringLiteral("No commands, test stopped"));
        stopTest(true);
        return;
    }

    if (m_currentCommandIndex >= m_totalCommands) {
        m_currentCommandIndex = 0;
        m_totalCommands = commands.size();
    }

    const CommandItem &item = commands.at(m_currentCommandIndex);
    m_currentCommandIndex = (m_currentCommandIndex + 1) % commands.size();

    QByteArray payload;
    if (item.hexMode) {
        const QString hexStr = item.text.simplified().remove(QRegularExpression(QStringLiteral("\\s+")));
        payload = QByteArray::fromHex(hexStr.toLatin1());
    } else {
        payload = item.text.toUtf8();
    }

    if (payload.isEmpty()) {
        // payload 为空时跳过本条命令，继续下一条，不中断定时器
        return;
    }

    const PacketInfo packet = m_statistics.recordSend(payload);
    appendLog(LogLevel::Tx, QStringLiteral("#%1 %2").arg(packet.id).arg(item.text));
    QMetaObject::invokeMethod(m_workerObject, "sendData", Qt::QueuedConnection, Q_ARG(QByteArray, payload));
    scheduleStatsRefresh();

    if (targetCount > 0 && packet.id >= static_cast<quint64>(targetCount)) {
        m_sendTimer->stop();
        m_finishingAfterLimit = true;
        appendLog(LogLevel::Info, QStringLiteral("Reached send limit, waiting for remaining responses or timeout"));
        checkFinishAfterLimit();
    }
}

void MainWindow::slotCheckTimeouts()
{
    if (!m_testRunning) {
        return;
    }

    const qint64 timeoutMs = ui->spinBoxTimeout->value();
    const QVector<PacketInfo> timedOut = m_statistics.markTimeouts(timeoutMs);
    for (const PacketInfo &packet : timedOut) {
        appendLog(LogLevel::Error, QStringLiteral("#%1 timeout/lost").arg(packet.id), packet.elapsedMs);
    }
    if (!timedOut.isEmpty()) {
        scheduleStatsRefresh();
    }
    checkFinishAfterLimit();
}

void MainWindow::slotWorkerConnected()
{
    m_connected = true;
    setConnectionState(ConnectionState::Connected);
    appendLog(LogLevel::Info, QStringLiteral("Connected"));
}

void MainWindow::slotWorkerDisconnected()
{
    m_connected = false;
    if (m_testRunning) {
        stopTest(true);
    }
    setConnectionState(ConnectionState::Disconnected);
    appendLog(LogLevel::Info, QStringLiteral("Remote closed connection"));
}

void MainWindow::slotWorkerDataReceived(const QByteArray &data)
{
    PacketInfo packet;
    if (m_statistics.recordReceive(data, &packet)) {
        appendLog(LogLevel::Rx, QStringLiteral("#%1 %2").arg(packet.id).arg(payloadToDisplay(data)), packet.elapsedMs);
    } else {
        appendLog(LogLevel::Rx, QStringLiteral("Unmatched response %1").arg(payloadToDisplay(data)));
    }
    scheduleStatsRefresh();
    checkFinishAfterLimit();
}

void MainWindow::slotWorkerError(const QString &message)
{
    appendLog(LogLevel::Error, message);
}

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

    QLabel *stateLabel = ui->labelConnectionState;
    if (stateLabel) {
        stateLabel->setText(text);
    }
    QFrame *led = ui->frameLed;
    if (led) {
        led->setStyleSheet(QStringLiteral("background:%1; border:1px solid rgba(255,255,255,0.35); border-radius:8px;").arg(color));
    }
}

void MainWindow::createWorker()
{
    m_commThread = new QThread(this);

    if (ui->comboBoxMode->currentIndex() == 0) {
        auto *worker = new TcpClientWorker(ui->lineEditIp->text().trimmed(), static_cast<quint16>(ui->spinBoxPort->value()));
        m_workerObject = worker;
        m_commInterface = worker;
    } else {
        if (ui->comboBoxSerialPort->currentText().isEmpty()) {
            appendLog(LogLevel::Error, QStringLiteral("No serial port available, refresh and retry"));
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

    m_workerObject->moveToThread(m_commThread);
    QObject::connect(m_commThread, &QThread::finished, m_workerObject, &QObject::deleteLater);
    QObject::connect(m_workerObject, SIGNAL(signalConnected()), this, SLOT(slotWorkerConnected()));
    QObject::connect(m_workerObject, SIGNAL(signalDisconnected()), this, SLOT(slotWorkerDisconnected()));
    QObject::connect(m_workerObject, SIGNAL(signalDataReceived(QByteArray)), this, SLOT(slotWorkerDataReceived(QByteArray)));
    QObject::connect(m_workerObject, SIGNAL(signalErrorOccurred(QString)), this, SLOT(slotWorkerError(QString)));
    m_commThread->start();
}

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

void MainWindow::updateStatsView()
{
    const StatisticsSnapshot snap = m_statistics.snapshot();
    ui->labelTotalSentValue->setText(QString::number(snap.totalSent));
    ui->labelSuccessValue->setText(QString::number(snap.successReceived));
    ui->labelLostValue->setText(QString::number(snap.lostPackets));
    ui->labelRateValue->setText(QStringLiteral("%1%").arg(snap.successRate, 0, 'f', 2));
    ui->labelAverageValue->setText(QStringLiteral("%1 ms").arg(snap.averageElapsedMs, 0, 'f', 3));
    ui->labelMaxValue->setText(QStringLiteral("%1 ms").arg(snap.maxElapsedMs));
    ui->labelMinValue->setText(QStringLiteral("%1 ms").arg(snap.minElapsedMs));

    auto setLabel = [&](const QString &objName, const QString &text) {
        auto *label = ui->groupBoxStats->findChild<QLabel*>(objName);
        if (label) label->setText(text);
    };

    


    
}

void MainWindow::setupStatsLabels()
{
    // 在统计区已有的 Min 行下方插入百分位指标行
    int row = 8;

    auto addRow = [&](const QString &title, const QString &objName) {
        auto *label = new QLabel(title, ui->groupBoxStats);
        auto *value = new QLabel(QStringLiteral("--"), ui->groupBoxStats);
        value->setObjectName(objName);
        ui->gridLayoutStats->addWidget(label, row, 0);
        ui->gridLayoutStats->addWidget(value, row, 1);
        ++row;
    };

    addRow(QStringLiteral("P50"), QStringLiteral("labelP50Value"));
    addRow(QStringLiteral("P90"), QStringLiteral("labelP90Value"));
    addRow(QStringLiteral("P95"), QStringLiteral("labelP95Value"));
    addRow(QStringLiteral("P99"), QStringLiteral("labelP99Value"));
}

void MainWindow::scheduleStatsRefresh()
{
    if (!m_statsTimer->isActive()) {
        m_statsTimer->start();
    }
}
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
        tag = QStringLiteral("INF");
        color = QStringLiteral("#c0c8d0");
        break;
    }

    QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz"));
    QString elapsed;
    if (elapsedMs >= 0) {
        elapsed = QStringLiteral(" (%1 ms)").arg(elapsedMs);
    }

    QString html = QStringLiteral("<span style='color:%1;'><b>[%2]</b> %3%4 %5</span>")
                       .arg(color, tag, timestamp, elapsed, htmlEscape(text));

    ui->textEditLog->append(html);
    QScrollBar *scrollBar = ui->textEditLog->verticalScrollBar();
    if (scrollBar) {
        scrollBar->setValue(scrollBar->maximum());
    }
}

void MainWindow::stopTest(bool manualStop)
{
    if (!m_testRunning) {
        return;
    }

    m_sendTimer->stop();
    m_timeoutTimer->stop();
    m_testRunning = false;

    if (manualStop) {
        appendLog(LogLevel::Info, QStringLiteral("Test stopped manually"));
    }

    QVector<PacketInfo> lost = m_statistics.markAllPendingLost();
    for (const PacketInfo &packet : lost) {
        appendLog(LogLevel::Error, QStringLiteral("#%1 lost").arg(packet.id));
    }

    finalizeTestReport();
    scheduleStatsRefresh();

    ui->pushButtonStart->setEnabled(true);
    ui->pushButtonStop->setEnabled(false);
    ui->pushButtonConnect->setEnabled(true);
    ui->comboBoxMode->setEnabled(true);
    ui->pushButtonSendAll->setEnabled(true);
}

void MainWindow::finalizeTestReport()
{
    const QString fileName = QStringLiteral("CommReport_%1.txt")
                                 .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss")));
    QDir logDir(QDir::current().filePath(QStringLiteral("Log")));
    if (!logDir.exists() && !logDir.mkpath(QStringLiteral("."))) {
        appendLog(LogLevel::Error, QStringLiteral("Failed to create log directory: %1").arg(logDir.absolutePath()));
        return;
    }

    const QString filePath = logDir.filePath(fileName);
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        appendLog(LogLevel::Error, QStringLiteral("Failed to write report: %1").arg(file.errorString()));
        return;
    }

    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);

    out << QStringLiteral("CommBench Pro Test Report\n");
    out << QStringLiteral("==========================\n\n");
    out << QStringLiteral("Mode: %1\n").arg(currentModeDescription());
    out << QStringLiteral("Config: %1\n\n").arg(currentConfigDescription());

    const StatisticsSnapshot snap = m_statistics.snapshot();
    out << QStringLiteral("Total Sent: %1\n").arg(snap.totalSent);
    out << QStringLiteral("Success RX: %1\n").arg(snap.successReceived);
    out << QStringLiteral("Lost: %1\n").arg(snap.lostPackets);
    out << QStringLiteral("Success Rate: %1%\n").arg(snap.successRate, 0, 'f', 2);
    out << QStringLiteral("Average: %1 ms\n").arg(snap.averageElapsedMs, 0, 'f', 2);
    out << QStringLiteral("Max: %1 ms\n").arg(snap.maxElapsedMs);
    out << QStringLiteral("Min: %1 ms\n").arg(snap.minElapsedMs);

    out << QStringLiteral("\n--- Packet Details ---\n");
    for (const PacketInfo &packet : m_statistics.packets()) {
        QString statusStr;
        switch (packet.status) {
        case PacketInfo::Status::Pending:
            statusStr = QStringLiteral("PENDING");
            break;
        case PacketInfo::Status::Success:
            statusStr = QStringLiteral("SUCCESS");
            break;
        case PacketInfo::Status::Timeout:
            statusStr = QStringLiteral("TIMEOUT");
            break;
        }

        out << QStringLiteral("#%1 %2 %3 ms\n")
                   .arg(packet.id)
                   .arg(statusStr)
                   .arg(packet.elapsedMs >= 0 ? QString::number(packet.elapsedMs) : QStringLiteral("-"));
    }

    file.close();
    appendLog(LogLevel::Info, QStringLiteral("Test report saved: %1").arg(filePath));
}

void MainWindow::checkFinishAfterLimit()
{
    if (m_testRunning && m_finishingAfterLimit && !m_statistics.hasPendingPackets()) {
        stopTest(false);
    }
}

QString MainWindow::currentModeDescription() const
{
    return ui->comboBoxMode->currentIndex() == 0 ? QStringLiteral("TCP Network") : QStringLiteral("Serial Port");
}

QString MainWindow::currentConfigDescription() const
{
    if (ui->comboBoxMode->currentIndex() == 0) {
        return QStringLiteral("%1:%2, protocol framed")
            .arg(ui->lineEditIp->text().trimmed())
            .arg(ui->spinBoxPort->value());
    }

    return QStringLiteral("%1, %2 bps, %3 data, %4 stop, %5 parity")
        .arg(ui->comboBoxSerialPort->currentText(), ui->comboBoxBaudRate->currentText(),
             ui->comboBoxDataBits->currentText(), ui->comboBoxStopBits->currentText(),
             ui->comboBoxParity->currentText());
}

QByteArray MainWindow::buildPayload()
{
    const QList<CommandItem> commands = collectCommands();
    if (commands.isEmpty()) {
        return {};
    }

    const CommandItem &item = commands.first();
    if (item.hexMode) {
        const QString hexStr = item.text.simplified().remove(QRegularExpression(QStringLiteral("\\s+")));
        return QByteArray::fromHex(hexStr.toLatin1());
    }
    return item.text.toUtf8();
}

QString MainWindow::payloadToDisplay(const QByteArray &payload)
{
    if (payload.isEmpty()) {
        return QStringLiteral("(empty)");
    }

    return QString::fromLatin1(payload.toHex(' ').toUpper());
}

QString MainWindow::htmlEscape(const QString &value)
{
    QString escaped = value;
    escaped.replace(QStringLiteral("&"), QStringLiteral("&amp;"));
    escaped.replace(QStringLiteral("<"), QStringLiteral("&lt;"));
    escaped.replace(QStringLiteral(">"), QStringLiteral("&gt;"));
    return escaped;
}

END_NAMESPACE_CIQTEK