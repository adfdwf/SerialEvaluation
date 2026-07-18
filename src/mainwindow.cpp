#include "mainwindow.h"
#include "responsetiming.h"
#include "serialclientworker.h"
#include "tcpclientworker.h"
#include "tcpportparser.h"
#include "ui_mainwindow.h"

#include <QDateTime>
#include <QAbstractItemView>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QMetaObject>
#include <QMessageBox>
#include <QGroupBox>
#include <QGridLayout>
#include <QHostAddress>
#include <QSignalBlocker>
#include <QSerialPortInfo>
#include <QTableWidget>
#include <QTabWidget>
#include <QRadioButton>
#include <QRegularExpression>
#include <QTextStream>
#include <QTextEdit>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

BEGIN_NAMESPACE_CIQTEK

namespace {

/**
 * @brief 单个端口允许同时等待的响应数量。
 *
 * 采用请求-响应背压，避免 Interval 小于服务端处理时间时无限堆积 TCP/串口
 * 数据；这样下一个命令只有在上一个响应完成后才会进入传输层。
 */
constexpr int kMaxInFlightPackets = 1;

} // namespace

/**
 * @file mainwindow.cpp
 * @brief 实现 TCP 多端口和串口多端口性能测试主界面。
 *
 * 主线程负责 UI、发送调度、统计和日志；每个网络/串口会话的 worker 在
 * 独立线程中执行 I/O，避免某个端口阻塞其他端口。
 */

/** 创建主窗口并初始化所有动态界面和定时器。 */
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    setupUiLogic();
    applyIndustrialTheme();
    slotRefreshSerialPorts();
    slotModeChanged(ui->comboBoxMode->currentIndex());
    scheduleStatsRefresh();
}

/** 停止测试、关闭所有 worker、删除会话对象并释放 UI。 */
MainWindow::~MainWindow()
{
    stopTcpTest(true);
    stopSerialTest(true);
    destroyTcpWorkers();
    destroySerialWorkers();
    qDeleteAll(m_tcpSessions);
    m_tcpSessions.clear();
    qDeleteAll(m_serialSessions);
    m_serialSessions.clear();
    delete ui;
}

/** 初始化公共控件范围、定时器、信号连接和 TCP/串口动态区域。 */
void MainWindow::setupUiLogic()
{
    m_timeoutTimer = new QTimer(this);
    // 以较小周期检查统计超时，使 recv 超时后能及时放行下一条任务。
    m_timeoutTimer->setInterval(10);
    m_statsTimer = new QTimer(this);
    m_statsTimer->setInterval(100);
    QObject::connect(m_statsTimer, &QTimer::timeout, this, &MainWindow::updateStatsView);
    m_statsTimer->start();

    ui->spinBoxPort->setRange(1, 65535);
    ui->spinBoxPort->setValue(10160);
    ui->spinBoxInterval->setRange(0, 600000);
    ui->spinBoxInterval->setValue(1000);
    ui->spinBoxSendCount->setRange(0, 100000000);
    ui->spinBoxSendCount->setValue(0);
    ui->spinBoxTimeout->setRange(1, 600000);
    ui->spinBoxTimeout->setValue(300);

    ui->comboBoxMode->addItems({QStringLiteral("TCP Network"), QStringLiteral("Serial Port")});
    ui->comboBoxBaudRate->addItems({QStringLiteral("9600"), QStringLiteral("19200"), QStringLiteral("38400"), QStringLiteral("57600"), QStringLiteral("115200"), QStringLiteral("230400"), QStringLiteral("460800"), QStringLiteral("921600")});
    ui->comboBoxBaudRate->setCurrentText(QStringLiteral("115200"));
    ui->comboBoxDataBits->addItems({QStringLiteral("5"), QStringLiteral("6"), QStringLiteral("7"), QStringLiteral("8")});
    ui->comboBoxDataBits->setCurrentText(QStringLiteral("8"));
    ui->comboBoxStopBits->addItems({QStringLiteral("1"), QStringLiteral("1.5"), QStringLiteral("2")});
    ui->comboBoxParity->addItems({QStringLiteral("None"), QStringLiteral("Even"), QStringLiteral("Odd"), QStringLiteral("Mark"), QStringLiteral("Space")});

    ui->lineEditIp->setText(QStringLiteral("172.16.32.231"));

    setupTcpPortUi();
    setupSerialPortUi();

    m_serialHotplugTimer = new QTimer(this);
    m_serialHotplugTimer->setInterval(2000);
    QObject::connect(m_serialHotplugTimer, &QTimer::timeout, this, &MainWindow::slotRefreshSerialPorts);
    m_serialHotplugTimer->start();

    ui->pushButtonStop->setEnabled(false);
    // 日志只写入本地文件，不在界面中显示，也不向 QTextEdit 追加内容。

    QObject::connect(ui->comboBoxMode, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::slotModeChanged);
    QObject::connect(ui->pushButtonRefreshPorts, &QPushButton::clicked, this, &MainWindow::slotRefreshSerialPorts);
    QObject::connect(ui->pushButtonConnect, &QPushButton::clicked, this, &MainWindow::slotConnectClicked);
    QObject::connect(ui->pushButtonDisconnect, &QPushButton::clicked, this, &MainWindow::slotDisconnectClicked);
    QObject::connect(ui->pushButtonStart, &QPushButton::clicked, this, &MainWindow::slotStartClicked);
    QObject::connect(ui->pushButtonStop, &QPushButton::clicked, this, &MainWindow::slotStopClicked);
    QObject::connect(m_tcpAddPortButton, &QPushButton::clicked, this, &MainWindow::slotAddTcpPort);
    QObject::connect(m_tcpBatchAddPortButton, &QPushButton::clicked, this, &MainWindow::slotBatchAddTcpPorts);
    QObject::connect(m_tcpRemovePortButton, &QPushButton::clicked, this, &MainWindow::slotRemoveTcpPort);
    QObject::connect(m_tcpSendAllButton, &QPushButton::clicked, this, &MainWindow::slotSendAllTcpPorts);
    QObject::connect(m_serialAddPortButton, &QPushButton::clicked, this, &MainWindow::slotAddSerialPort);
    QObject::connect(m_serialRemovePortButton, &QPushButton::clicked, this, &MainWindow::slotRemoveSerialPort);
    QObject::connect(m_serialRefreshButton, &QPushButton::clicked, this, &MainWindow::slotRefreshSerialPorts);
    QObject::connect(m_serialSendAllButton, &QPushButton::clicked, this, &MainWindow::slotSendAllSerialPorts);
    QObject::connect(m_timeoutTimer, &QTimer::timeout, this, &MainWindow::slotCheckTimeouts);
    QObject::connect(ui->spinBoxTimeout, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int timeoutMs) {
        // Timeout 是 I/O 线程 recv 的等待窗口，修改后立即广播到所有活动端口。
        for (TcpPortSession *session : m_tcpSessions) {
            if (session->worker) session->worker->setReceiveTimeout(timeoutMs);
        }
        for (SerialPortSession *session : m_serialSessions) {
            if (session->worker) session->worker->setReceiveTimeout(timeoutMs);
        }
    });
    QObject::connect(ui->spinBoxInterval, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int intervalMs) {
        for (TcpPortSession *session : m_tcpSessions) {
            if (session->worker) session->worker->setSendInterval(intervalMs);
        }
        for (SerialPortSession *session : m_serialSessions) {
            if (session->worker) session->worker->setSendInterval(intervalMs);
        }
    });
}

/** 配置旧版单串口命令表的列、选择方式并创建第一条命令。 */
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

/** 校验 A0 81 协议帧的长度字段和累加校验和。 */
bool MainWindow::validateHexPayload(const QString &text)
{
    if (!validateHexSyntax(text)) {
        return false;
    }

    const QByteArray payload = QByteArray::fromHex(text.simplified().remove(QRegularExpression(QStringLiteral("\\s+"))).toLatin1());
    if (payload.size() < 2 || static_cast<quint8>(payload.at(0)) != 0xA0 || static_cast<quint8>(payload.at(1)) != 0x81) {
        return true;
    }

    // A0 81 frames use bytes 3..6 as a big-endian payload length and the
    // low 8 bits of the preceding byte sum as the final checksum byte.
    if (payload.size() < 9) {
        return false;
    }
    quint32 payloadLength = 0;
    for (int index = 3; index <= 6; ++index) {
        payloadLength = (payloadLength << 8) | static_cast<quint8>(payload.at(index));
    }
    const quint64 expectedSize = 9ULL + payloadLength;
    if (expectedSize != static_cast<quint64>(payload.size())) {
        return false;
    }

    quint8 checksum = 0;
    for (int index = 0; index < payload.size() - 1; ++index) {
        checksum = static_cast<quint8>(checksum + static_cast<quint8>(payload.at(index)));
    }
    return checksum == static_cast<quint8>(payload.back());
}

/** 判断单个字符是否属于 0-9、a-f 或 A-F。 */
bool MainWindow::isValidHexChar(QChar c)
{
    return (c >= QLatin1Char('0') && c <= QLatin1Char('9')) ||
           (c >= QLatin1Char('a') && c <= QLatin1Char('f')) ||
           (c >= QLatin1Char('A') && c <= QLatin1Char('F'));
}

/** 从旧版单串口命令表收集非空命令。 */
/** 从指定动态命令表收集非空命令并判断其 HEX 格式。 */
QList<CommandItem> MainWindow::collectCommands(QTableWidget *table) const
{
    QList<CommandItem> commands;
    if (!table) {
        return commands;
    }
    for (int row = 0; row < table->rowCount(); ++row) {
        auto *edit = qobject_cast<QLineEdit *>(table->cellWidget(row, 1));
        if (!edit || edit->text().trimmed().isEmpty()) continue;
        auto *modeWidget = table->cellWidget(row, 2);
        bool hexMode = false;
        if (modeWidget) {
            for (auto *button : modeWidget->findChildren<QRadioButton *>()) {
                if (button->isChecked() && button->text() == QStringLiteral("HEX")) {
                    hexMode = true;
                    break;
                }
            }
        }
        CommandItem item;
        item.text = edit->text();
        item.hexMode = hexMode;
        item.valid = !hexMode || validateHexPayload(item.text);
        commands.append(item);
    }
    return commands;
}

/** 确认命令列表非空且每一条命令都通过校验。 */
bool MainWindow::validateCommands(const QList<CommandItem> &commands)
{
    if (commands.isEmpty()) {
        appendLog(LogLevel::Error, QStringLiteral("No valid commands"));
        return false;
    }
    for (const CommandItem &item : commands) {
        if (!item.valid) {
            appendLog(LogLevel::Error, QStringLiteral("Invalid HEX command: protocol frame length or checksum is invalid"));
            return false;
        }
    }
    return true;
}

/** 设置窗口标题和基础窗口尺寸。 */
void MainWindow::applyIndustrialTheme()
{
    setWindowTitle(QStringLiteral("CommBench Pro - Industrial Communication Benchmark"));
    resize(1280, 780);
}

/** 切换 TCP/串口界面，停止旧模式会话并显示对应动态区域。 */
void MainWindow::slotModeChanged(int index)
{
    const bool serialMode = index == 1;
    stopTcpTest(true);
    destroyTcpWorkers();
    stopSerialTest(true);
    destroySerialWorkers();
    ui->widgetTcpConfig->setVisible(!serialMode);
    ui->widgetSerialConfig->setVisible(false);
    // 统计只在停止时写入各端口日志，界面不再显示 Realtime Statistics 卡片。
    if (m_serialPortBox) {
        m_serialPortBox->setVisible(serialMode);
    }
    if (m_tcpPortTable) {
        m_tcpPortTable->parentWidget()->setVisible(!serialMode);
    }
    ui->spinBoxPort->setVisible(false);
    if (auto *portLabel = ui->groupBoxConfig->findChild<QLabel *>(QStringLiteral("labelPort"))) portLabel->setVisible(false);
    if (serialMode) {
        ui->pushButtonConnect->setText(QStringLiteral("Connect All"));
        ui->pushButtonDisconnect->setText(QStringLiteral("Disconnect All"));
        updateSerialConnectionState();
    } else {
        ui->pushButtonConnect->setText(QStringLiteral("Connect All"));
        ui->pushButtonDisconnect->setText(QStringLiteral("Disconnect All"));
        updateTcpConnectionState();
    }
}

/** 创建 TCP 端口工具栏、状态表和命令标签页容器。 */
void MainWindow::setupTcpPortUi()
{
    auto *mainLayout = qobject_cast<QVBoxLayout *>(ui->centralwidget->layout());
    if (!mainLayout) {
        return;
    }

    auto *portBox = new QGroupBox(QStringLiteral("TCP Ports"), ui->centralwidget);
    auto *boxLayout = new QVBoxLayout(portBox);
    auto *toolbar = new QHBoxLayout();
    m_tcpAddPortButton = new QPushButton(QStringLiteral("+ Add Port"), portBox);
    m_tcpBatchAddPortButton = new QPushButton(QStringLiteral("Batch Add"), portBox);
    m_tcpRemovePortButton = new QPushButton(QStringLiteral("- Remove Port"), portBox);
    m_tcpSendAllButton = new QPushButton(QStringLiteral("Send All Ports"), portBox);
    toolbar->addWidget(m_tcpAddPortButton);
    toolbar->addWidget(m_tcpBatchAddPortButton);
    toolbar->addWidget(m_tcpRemovePortButton);
    toolbar->addWidget(m_tcpSendAllButton);
    toolbar->addStretch();
    boxLayout->addLayout(toolbar);

    m_tcpPortTable = new QTableWidget(portBox);
    m_tcpPortTable->setColumnCount(3);
    m_tcpPortTable->setHorizontalHeaderLabels({QStringLiteral("Port"), QStringLiteral("State"), QStringLiteral("Send All")});
    m_tcpPortTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tcpPortTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tcpPortTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tcpPortTable->verticalHeader()->setVisible(false);
    m_tcpPortTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_tcpPortTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_tcpPortTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    boxLayout->addWidget(m_tcpPortTable);

    m_tcpCommandTabs = new QTabWidget(portBox);
    boxLayout->addWidget(m_tcpCommandTabs);
    mainLayout->insertWidget(2, portBox);

    addTcpPort(static_cast<quint16>(ui->spinBoxPort->value()));
    portBox->setVisible(false);
}

/** 创建一个 TCP 端口会话、命令表和端口级 Send All 按钮。 */
void MainWindow::addTcpPort(quint16 port)
{
    if (port == 0 || m_tcpSessions.contains(port)) {
        return;
    }

    auto *session = new TcpPortSession();
    session->port = port;
    session->commandPage = new QWidget(m_tcpCommandTabs);
    auto *pageLayout = new QVBoxLayout(session->commandPage);
    auto *toolbar = new QHBoxLayout();
    auto *addButton = new QPushButton(QStringLiteral("+ Add Command"), session->commandPage);
    auto *removeButton = new QPushButton(QStringLiteral("- Remove Command"), session->commandPage);
    auto *sendButton = new QPushButton(QStringLiteral("Send All Port %1").arg(port), session->commandPage);
    toolbar->addWidget(addButton);
    toolbar->addWidget(removeButton);
    toolbar->addWidget(sendButton);
    toolbar->addStretch();
    pageLayout->addLayout(toolbar);

    session->commandTable = new QTableWidget(session->commandPage);
    session->commandTable->setColumnCount(3);
    session->commandTable->setHorizontalHeaderLabels({QStringLiteral("#"), QStringLiteral("Command"), QStringLiteral("Mode")});
    session->commandTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    session->commandTable->setSelectionMode(QAbstractItemView::SingleSelection);
    session->commandTable->verticalHeader()->setVisible(false);
    session->commandTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    session->commandTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    session->commandTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    pageLayout->addWidget(session->commandTable);

    auto *tcpLogBox = new QGroupBox(QStringLiteral("Log - Port %1").arg(port), session->commandPage);
    auto *tcpLogLayout = new QVBoxLayout(tcpLogBox);
    session->logEdit = new QTextEdit(tcpLogBox);
    session->logEdit->setReadOnly(true);
    session->logEdit->setLineWrapMode(QTextEdit::NoWrap);
    session->logEdit->setMinimumHeight(360);
    session->logEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    session->logEdit->document()->setMaximumBlockCount(500);
    tcpLogBox->setMinimumHeight(400);
    tcpLogLayout->addWidget(session->logEdit);
    pageLayout->addWidget(tcpLogBox);

    auto *tcpStatsBox = new QGroupBox(QStringLiteral("Realtime Statistics - Port %1").arg(port), session->commandPage);
    auto *tcpStatsLayout = new QGridLayout(tcpStatsBox);
    const QStringList tcpStatNames = {
        QStringLiteral("TX Count"), QStringLiteral("RX Count"), QStringLiteral("TX Bytes"),
        QStringLiteral("RX Bytes"), QStringLiteral("Success Rate")
    };
    const QStringList tcpObjectNames = {
        QStringLiteral("tcpTxCountValue"), QStringLiteral("tcpRxCountValue"), QStringLiteral("tcpTxBytesValue"),
        QStringLiteral("tcpRxBytesValue"), QStringLiteral("tcpSuccessRateValue")
    };
    for (int i = 0; i < tcpStatNames.size(); ++i) {
        auto *label = new QLabel(tcpStatNames.at(i), tcpStatsBox);
        auto *value = new QLabel(QStringLiteral("0"), tcpStatsBox);
        value->setObjectName(tcpObjectNames.at(i));
        tcpStatsLayout->addWidget(label, 0, i * 2);
        tcpStatsLayout->addWidget(value, 0, i * 2 + 1);
    }
    pageLayout->addWidget(tcpStatsBox);

    m_tcpCommandTabs->addTab(session->commandPage, QStringLiteral("Port %1").arg(port));
    m_tcpSessions.insert(port, session);

    QObject::connect(addButton, &QPushButton::clicked, this, [this, session]() { addTcpCommand(session); });
    QObject::connect(removeButton, &QPushButton::clicked, this, [this, session]() { removeTcpCommand(session); });
    QObject::connect(sendButton, &QPushButton::clicked, this, [this, session]() { sendAllTcpPort(session); });

    const int row = m_tcpPortTable->rowCount();
    m_tcpPortTable->insertRow(row);
    m_tcpPortTable->setItem(row, 0, new QTableWidgetItem(QString::number(port)));
    m_tcpPortTable->setItem(row, 1, new QTableWidgetItem(QStringLiteral("Disconnected")));
    auto *rowSendButton = new QPushButton(QStringLiteral("Send All"), m_tcpPortTable);
    QObject::connect(rowSendButton, &QPushButton::clicked, this, [this, session]() { sendAllTcpPort(session); });
    m_tcpPortTable->setCellWidget(row, 2, rowSendButton);

    addTcpCommand(session);
    updateTcpConnectionState();
}

/** 关闭并删除指定 TCP 会话，同时移除其表格行和标签页。 */
void MainWindow::removeTcpPort(quint16 port)
{
    TcpPortSession *session = m_tcpSessions.take(port);
    if (!session) {
        return;
    }
    if (session->worker) {
        QObject::disconnect(session->worker, nullptr, this, nullptr);
        static_cast<TcpClientWorker *>(session->worker)->disconnect();
        delete static_cast<TcpClientWorker *>(session->worker);
        session->worker = nullptr;
    }
    delete session->sendTimer;
    session->sendTimer = nullptr;
    const int tabIndex = m_tcpCommandTabs->indexOf(session->commandPage);
    if (tabIndex >= 0) {
        m_tcpCommandTabs->removeTab(tabIndex);
    }
    for (int row = 0; row < m_tcpPortTable->rowCount(); ++row) {
        if (m_tcpPortTable->item(row, 0) && m_tcpPortTable->item(row, 0)->text().toUShort() == port) {
            m_tcpPortTable->removeRow(row);
            break;
        }
    }
    delete session;
    updateTcpConnectionState();
}

/** 弹出端口号输入框并添加 TCP 会话。 */
void MainWindow::slotAddTcpPort()
{
    bool ok = false;
    const int port = QInputDialog::getInt(this, QStringLiteral("Add TCP Port"), QStringLiteral("Port:"), 10160, 1, 65535, 1, &ok);
    if (ok) {
        if (m_tcpSessions.contains(static_cast<quint16>(port))) {
            QMessageBox::warning(this, QStringLiteral("Duplicate port"), QStringLiteral("This port has already been added."));
        } else {
            addTcpPort(static_cast<quint16>(port));
        }
    }
}

/** 删除 TCP 端口表中当前选中的端口。 */
void MainWindow::slotRemoveTcpPort()
{
    const int row = m_tcpPortTable->currentRow();
    if (row < 0 || !m_tcpPortTable->item(row, 0)) {
        return;
    }
    removeTcpPort(static_cast<quint16>(m_tcpPortTable->item(row, 0)->text().toUShort()));
}

/** 为指定 TCP 会话新增一条 ASCII/HEX 命令。 */
void MainWindow::addTcpCommand(TcpPortSession *session)
{
    if (!session || !session->commandTable) {
        return;
    }
    const int row = session->commandTable->rowCount();
    session->commandTable->insertRow(row);
    auto *number = new QTableWidgetItem(QString::number(row + 1));
    number->setFlags(number->flags() & ~Qt::ItemIsEditable);
    number->setTextAlignment(Qt::AlignCenter);
    session->commandTable->setItem(row, 0, number);

    auto *edit = new QLineEdit(session->commandTable);
    edit->setMaxLength(16 * 1024 * 1024);
    edit->setPlaceholderText(QStringLiteral("A0 81 01 00 00 00 00 00 22"));
    session->commandTable->setCellWidget(row, 1, edit);
    auto *ascii = new QRadioButton(QStringLiteral("ASCII"));
    auto *hex = new QRadioButton(QStringLiteral("HEX"));
    ascii->setChecked(true);
    auto *modeWidget = new QWidget(session->commandTable);
    auto *modeLayout = new QHBoxLayout(modeWidget);
    modeLayout->setContentsMargins(4, 0, 4, 0);
    modeLayout->addWidget(ascii);
    modeLayout->addWidget(hex);
    modeLayout->addStretch();
    session->commandTable->setCellWidget(row, 2, modeWidget);

    const auto refreshValidation = [this, session, edit]() {
        bool hexMode = false;
        int editRow = -1;
        for (int row = 0; row < session->commandTable->rowCount(); ++row) {
            if (session->commandTable->cellWidget(row, 1) == edit) {
                editRow = row;
                break;
            }
        }
        QWidget *modeWidget = editRow >= 0 ? session->commandTable->cellWidget(editRow, 2) : nullptr;
        if (modeWidget) {
            for (auto *button : modeWidget->findChildren<QRadioButton *>()) {
                hexMode = button->isChecked() && button->text() == QStringLiteral("HEX");
                if (hexMode) break;
            }
        }
        const bool valid = !hexMode || validateHexPayload(edit->text());
        edit->setStyleSheet(valid ? QStringLiteral("background: #ffffff; color: #000000;")
                                   : QStringLiteral("background: #ffe0e0; color: #cc0000;"));
    };
    QObject::connect(edit, &QLineEdit::textChanged, this, refreshValidation);
    QObject::connect(ascii, &QRadioButton::toggled, this, refreshValidation);
    QObject::connect(hex, &QRadioButton::toggled, this, refreshValidation);
}

/** 删除指定 TCP 会话当前选中的命令。 */
void MainWindow::removeTcpCommand(TcpPortSession *session)
{
    if (!session || !session->commandTable || session->commandTable->rowCount() <= 1) {
        return;
    }
    int row = session->commandTable->currentRow();
    if (row < 0) row = session->commandTable->rowCount() - 1;
    session->commandTable->removeRow(row);
    for (int index = row; index < session->commandTable->rowCount(); ++index) {
        if (session->commandTable->item(index, 0)) {
            session->commandTable->item(index, 0)->setText(QString::number(index + 1));
        }
    }
}

/** 刷新旧版串口下拉框并同步动态串口候选项。 */
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
    refreshSerialPortChoices();
}

/** 创建串口工具栏、状态表和每串口命令标签页容器。 */
void MainWindow::setupSerialPortUi()
{
    auto *mainLayout = qobject_cast<QVBoxLayout *>(ui->centralwidget->layout());
    if (!mainLayout) {
        return;
    }

    m_serialPortBox = new QGroupBox(QStringLiteral("Serial Ports"), ui->centralwidget);
    auto *boxLayout = new QVBoxLayout(m_serialPortBox);
    auto *toolbar = new QHBoxLayout();
    m_serialAddPortButton = new QPushButton(QStringLiteral("+ Add Port"), m_serialPortBox);
    m_serialRemovePortButton = new QPushButton(QStringLiteral("- Remove Port"), m_serialPortBox);
    m_serialRefreshButton = new QPushButton(QStringLiteral("Refresh"), m_serialPortBox);
    m_serialSendAllButton = new QPushButton(QStringLiteral("Send All Serial Ports"), m_serialPortBox);
    toolbar->addWidget(m_serialAddPortButton);
    toolbar->addWidget(m_serialRemovePortButton);
    toolbar->addWidget(m_serialRefreshButton);
    toolbar->addWidget(m_serialSendAllButton);
    toolbar->addStretch();
    boxLayout->addLayout(toolbar);

    m_serialPortTable = new QTableWidget(m_serialPortBox);
    m_serialPortTable->setColumnCount(3);
    m_serialPortTable->setHorizontalHeaderLabels({QStringLiteral("Port"), QStringLiteral("State"), QStringLiteral("Send All")});
    m_serialPortTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_serialPortTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_serialPortTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_serialPortTable->verticalHeader()->setVisible(false);
    m_serialPortTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_serialPortTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_serialPortTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    boxLayout->addWidget(m_serialPortTable);

    m_serialCommandTabs = new QTabWidget(m_serialPortBox);
    boxLayout->addWidget(m_serialCommandTabs);
    mainLayout->insertWidget(2, m_serialPortBox);

    const QList<QSerialPortInfo> available = QSerialPortInfo::availablePorts();
    if (!available.isEmpty()) {
        addSerialPort(available.first().portName());
    }
    m_serialPortBox->setVisible(false);
}

/** 创建一个串口会话及其基础参数、命令和统计控件。 */
void MainWindow::addSerialPort(const QString &portName)
{
    const QString name = portName.trimmed();
    if (name.isEmpty() || m_serialSessions.contains(name)) {
        return;
    }

    auto *session = new SerialPortSession();
    session->portName = name;
    session->commandPage = new QWidget(m_serialCommandTabs);
    auto *pageLayout = new QVBoxLayout(session->commandPage);

    auto *configLayout = new QHBoxLayout();
    configLayout->addWidget(new QLabel(QStringLiteral("Port"), session->commandPage));
    session->portCombo = new QComboBox(session->commandPage);
    // The session key and log prefix are the selected port. Changing it in
    // place would make an active worker ambiguous; remove/re-add a session to
    // change its port while retaining independent configuration.
    session->portCombo->setEditable(false);
    session->portCombo->setEnabled(false);
    configLayout->addWidget(session->portCombo);
    configLayout->addWidget(new QLabel(QStringLiteral("Baud"), session->commandPage));
    session->baudCombo = new QComboBox(session->commandPage);
    session->baudCombo->addItems({QStringLiteral("9600"), QStringLiteral("19200"), QStringLiteral("38400"), QStringLiteral("57600"), QStringLiteral("115200"), QStringLiteral("230400"), QStringLiteral("460800"), QStringLiteral("921600")});
    session->baudCombo->setCurrentText(QStringLiteral("115200"));
    configLayout->addWidget(session->baudCombo);
    configLayout->addWidget(new QLabel(QStringLiteral("Data"), session->commandPage));
    session->dataBitsCombo = new QComboBox(session->commandPage);
    session->dataBitsCombo->addItems({QStringLiteral("5"), QStringLiteral("6"), QStringLiteral("7"), QStringLiteral("8")});
    session->dataBitsCombo->setCurrentText(QStringLiteral("8"));
    configLayout->addWidget(session->dataBitsCombo);
    configLayout->addWidget(new QLabel(QStringLiteral("Stop"), session->commandPage));
    session->stopBitsCombo = new QComboBox(session->commandPage);
    session->stopBitsCombo->addItems({QStringLiteral("1"), QStringLiteral("1.5"), QStringLiteral("2")});
    configLayout->addWidget(session->stopBitsCombo);
    configLayout->addWidget(new QLabel(QStringLiteral("Parity"), session->commandPage));
    session->parityCombo = new QComboBox(session->commandPage);
    session->parityCombo->addItems({QStringLiteral("None"), QStringLiteral("Even"), QStringLiteral("Odd"), QStringLiteral("Mark"), QStringLiteral("Space")});
    configLayout->addWidget(session->parityCombo);
    configLayout->addStretch();
    pageLayout->addLayout(configLayout);

    for (const QSerialPortInfo &info : QSerialPortInfo::availablePorts()) {
        session->portCombo->addItem(info.portName());
    }
    session->portCombo->setCurrentText(name);

    auto *commandToolbar = new QHBoxLayout();
    auto *addButton = new QPushButton(QStringLiteral("+ Add Command"), session->commandPage);
    auto *removeButton = new QPushButton(QStringLiteral("- Remove Command"), session->commandPage);
    auto *sendButton = new QPushButton(QStringLiteral("Send All %1").arg(name), session->commandPage);
    commandToolbar->addWidget(addButton);
    commandToolbar->addWidget(removeButton);
    commandToolbar->addWidget(sendButton);
    commandToolbar->addStretch();
    pageLayout->addLayout(commandToolbar);

    session->commandTable = new QTableWidget(session->commandPage);
    session->commandTable->setColumnCount(3);
    session->commandTable->setHorizontalHeaderLabels({QStringLiteral("#"), QStringLiteral("Command"), QStringLiteral("Mode")});
    session->commandTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    session->commandTable->setSelectionMode(QAbstractItemView::SingleSelection);
    session->commandTable->verticalHeader()->setVisible(false);
    session->commandTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    session->commandTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    session->commandTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    pageLayout->addWidget(session->commandTable);

    auto *serialLogBox = new QGroupBox(QStringLiteral("Log - %1").arg(name), session->commandPage);
    auto *serialLogLayout = new QVBoxLayout(serialLogBox);
    session->logEdit = new QTextEdit(serialLogBox);
    session->logEdit->setReadOnly(true);
    session->logEdit->setLineWrapMode(QTextEdit::NoWrap);
    session->logEdit->setMinimumHeight(360);
    session->logEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    session->logEdit->document()->setMaximumBlockCount(500);
    serialLogBox->setMinimumHeight(400);
    serialLogLayout->addWidget(session->logEdit);
    pageLayout->addWidget(serialLogBox);

    auto *statsBox = new QGroupBox(QStringLiteral("Realtime Statistics - %1").arg(name), session->commandPage);
    auto *statsLayout = new QGridLayout(statsBox);
    const QStringList statNames = {
        QStringLiteral("TX Count"), QStringLiteral("RX Count"), QStringLiteral("TX Bytes"),
        QStringLiteral("RX Bytes"), QStringLiteral("Success Rate")
    };
    const QStringList objectNames = {
        QStringLiteral("serialTxCountValue"), QStringLiteral("serialRxCountValue"), QStringLiteral("serialTxBytesValue"),
        QStringLiteral("serialRxBytesValue"), QStringLiteral("serialSuccessRateValue")
    };
    for (int i = 0; i < statNames.size(); ++i) {
        auto *label = new QLabel(statNames.at(i), statsBox);
        auto *value = new QLabel(QStringLiteral("0"), statsBox);
        value->setObjectName(objectNames.at(i));
        statsLayout->addWidget(label, 0, i * 2);
        statsLayout->addWidget(value, 0, i * 2 + 1);
    }
    pageLayout->addWidget(statsBox);

    m_serialCommandTabs->addTab(session->commandPage, name);
    m_serialSessions.insert(name, session);
    QObject::connect(addButton, &QPushButton::clicked, this, [this, session]() { addSerialCommand(session); });
    QObject::connect(removeButton, &QPushButton::clicked, this, [this, session]() { removeSerialCommand(session); });
    QObject::connect(sendButton, &QPushButton::clicked, this, [this, session]() { sendAllSerialPort(session); });

    const int row = m_serialPortTable->rowCount();
    m_serialPortTable->insertRow(row);
    m_serialPortTable->setItem(row, 0, new QTableWidgetItem(name));
    m_serialPortTable->setItem(row, 1, new QTableWidgetItem(QStringLiteral("Disconnected")));
    auto *rowSendButton = new QPushButton(QStringLiteral("Send All"), m_serialPortTable);
    QObject::connect(rowSendButton, &QPushButton::clicked, this, [this, session]() { sendAllSerialPort(session); });
    m_serialPortTable->setCellWidget(row, 2, rowSendButton);
    addSerialCommand(session);
    updateSerialConnectionState();
}

/** 安全关闭指定串口并删除其动态页面和状态行。 */
void MainWindow::removeSerialPort(const QString &portName)
{
    SerialPortSession *session = m_serialSessions.take(portName);
    if (!session) {
        return;
    }
    destroySerialWorker(session);
    const int tabIndex = m_serialCommandTabs->indexOf(session->commandPage);
    if (tabIndex >= 0) {
        m_serialCommandTabs->removeTab(tabIndex);
    }
    for (int row = 0; row < m_serialPortTable->rowCount(); ++row) {
        if (m_serialPortTable->item(row, 0) && m_serialPortTable->item(row, 0)->text() == portName) {
            m_serialPortTable->removeRow(row);
            break;
        }
    }
    delete session->sendTimer;
    session->sendTimer = nullptr;
    delete session->commandPage;
    session->commandPage = nullptr;
    delete session;
    updateSerialConnectionState();
}

/** 弹出串口名称输入框并创建新的串口会话。 */
void MainWindow::slotAddSerialPort()
{
    QStringList names;
    for (const QSerialPortInfo &info : QSerialPortInfo::availablePorts()) {
        names.append(info.portName());
    }
#ifdef Q_OS_WIN
    const QString defaultName = names.isEmpty() ? QStringLiteral("COM1") : names.first();
#else
    const QString defaultName = names.isEmpty() ? QStringLiteral("/dev/ttyUSB0") : names.first();
#endif
    bool ok = false;
    const QString name = QInputDialog::getText(this, QStringLiteral("Add Serial Port"), QStringLiteral("Port name:"), QLineEdit::Normal, defaultName, &ok).trimmed();
    if (!ok || name.isEmpty()) {
        return;
    }
    if (m_serialSessions.contains(name)) {
        QMessageBox::warning(this, QStringLiteral("Duplicate port"), QStringLiteral("This serial port has already been added."));
        return;
    }
    addSerialPort(name);
}

/** 删除串口状态表中当前选中的串口。 */
void MainWindow::slotRemoveSerialPort()
{
    const int row = m_serialPortTable ? m_serialPortTable->currentRow() : -1;
    if (row < 0 || !m_serialPortTable->item(row, 0)) {
        return;
    }
    removeSerialPort(m_serialPortTable->item(row, 0)->text());
}

/** 对所有已连接串口分别启动一次 Send All。 */
void MainWindow::slotSendAllSerialPorts()
{
    for (SerialPortSession *session : m_serialSessions) {
        sendAllSerialPort(session);
    }
}

/** 根据系统热插拔结果刷新每个串口会话的候选名称。 */
void MainWindow::refreshSerialPortChoices()
{
    QStringList names;
    for (const QSerialPortInfo &info : QSerialPortInfo::availablePorts()) {
        names.append(info.portName());
    }
    for (SerialPortSession *session : m_serialSessions) {
        if (!session->portCombo) continue;
        const QString current = session->portCombo->currentText();
        QSignalBlocker blocker(session->portCombo);
        session->portCombo->clear();
        session->portCombo->addItems(names);
        if (!names.contains(session->portName)) session->portCombo->addItem(session->portName);
        session->portCombo->setCurrentText(current.isEmpty() ? session->portName : current);
    }
}

/** 将串口配置控件转换为 worker 使用的 SerialSettings。 */
SerialSettings MainWindow::serialSettings(const SerialPortSession *session) const
{
    SerialSettings settings;
    if (!session) return settings;
    settings.portName = session->portCombo ? session->portCombo->currentText().trimmed() : session->portName;
    settings.baudRate = session->baudCombo ? session->baudCombo->currentText().toInt() : 115200;
    const int dataBits = session->dataBitsCombo ? session->dataBitsCombo->currentText().toInt() : 8;
    settings.dataBits = dataBits == 5 ? QSerialPort::Data5 : dataBits == 6 ? QSerialPort::Data6 : dataBits == 7 ? QSerialPort::Data7 : QSerialPort::Data8;
    settings.stopBits = session->stopBitsCombo && session->stopBitsCombo->currentText() == QStringLiteral("2") ? QSerialPort::TwoStop :
                        session->stopBitsCombo && session->stopBitsCombo->currentText() == QStringLiteral("1.5") ? QSerialPort::OneAndHalfStop : QSerialPort::OneStop;
    const QString parity = session->parityCombo ? session->parityCombo->currentText() : QStringLiteral("None");
    settings.parity = parity == QStringLiteral("Even") ? QSerialPort::EvenParity : parity == QStringLiteral("Odd") ? QSerialPort::OddParity : parity == QStringLiteral("Mark") ? QSerialPort::MarkParity : parity == QStringLiteral("Space") ? QSerialPort::SpaceParity : QSerialPort::NoParity;
    return settings;
}

/** 为串口会话新增一行带实时 HEX 校验的命令。 */
void MainWindow::addSerialCommand(SerialPortSession *session)
{
    if (!session || !session->commandTable) return;
    const int row = session->commandTable->rowCount();
    session->commandTable->insertRow(row);
    auto *number = new QTableWidgetItem(QString::number(row + 1));
    number->setFlags(number->flags() & ~Qt::ItemIsEditable);
    number->setTextAlignment(Qt::AlignCenter);
    session->commandTable->setItem(row, 0, number);

    auto *edit = new QLineEdit(session->commandTable);
    edit->setMaxLength(16 * 1024 * 1024);
    edit->setPlaceholderText(QStringLiteral("A0 81 01 00 00 00 00 00 22"));
    session->commandTable->setCellWidget(row, 1, edit);
    auto *ascii = new QRadioButton(QStringLiteral("ASCII"));
    auto *hex = new QRadioButton(QStringLiteral("HEX"));
    ascii->setChecked(true);
    auto *modeWidget = new QWidget(session->commandTable);
    auto *modeLayout = new QHBoxLayout(modeWidget);
    modeLayout->setContentsMargins(4, 0, 4, 0);
    modeLayout->addWidget(ascii);
    modeLayout->addWidget(hex);
    modeLayout->addStretch();
    session->commandTable->setCellWidget(row, 2, modeWidget);

    const auto refreshValidation = [this, session, edit]() {
        int editRow = -1;
        for (int index = 0; index < session->commandTable->rowCount(); ++index) {
            if (session->commandTable->cellWidget(index, 1) == edit) {
                editRow = index;
                break;
            }
        }
        QWidget *mode = editRow >= 0 ? session->commandTable->cellWidget(editRow, 2) : nullptr;
        bool hexMode = false;
        if (mode) {
            for (auto *button : mode->findChildren<QRadioButton *>()) {
                if (button->isChecked() && button->text() == QStringLiteral("HEX")) {
                    hexMode = true;
                    break;
                }
            }
        }
        const bool valid = !hexMode || validateHexPayload(edit->text());
        edit->setStyleSheet(valid ? QStringLiteral("background: #ffffff; color: #000000;") : QStringLiteral("background: #ffe0e0; color: #cc0000;"));
    };
    QObject::connect(edit, &QLineEdit::textChanged, this, refreshValidation);
    QObject::connect(ascii, &QRadioButton::toggled, this, refreshValidation);
    QObject::connect(hex, &QRadioButton::toggled, this, refreshValidation);
}

/** 删除串口会话当前选中的命令并重新编号。 */
void MainWindow::removeSerialCommand(SerialPortSession *session)
{
    if (!session || !session->commandTable || session->commandTable->rowCount() <= 1) return;
    int row = session->commandTable->currentRow();
    if (row < 0) row = session->commandTable->rowCount() - 1;
    session->commandTable->removeRow(row);
    for (int index = row; index < session->commandTable->rowCount(); ++index) {
        if (session->commandTable->item(index, 0)) session->commandTable->item(index, 0)->setText(QString::number(index + 1));
    }
}

/** 收集串口会话中非空的独立命令列表。 */
QList<CommandItem> MainWindow::collectSerialCommands(const SerialPortSession *session) const
{
    QList<CommandItem> commands;
    if (!session || !session->commandTable) return commands;
    for (int row = 0; row < session->commandTable->rowCount(); ++row) {
        auto *edit = qobject_cast<QLineEdit *>(session->commandTable->cellWidget(row, 1));
        if (!edit || edit->text().trimmed().isEmpty()) continue;
        bool hexMode = false;
        if (auto *mode = session->commandTable->cellWidget(row, 2)) {
            for (auto *button : mode->findChildren<QRadioButton *>()) {
                if (button->isChecked() && button->text() == QStringLiteral("HEX")) {
                    hexMode = true;
                    break;
                }
            }
        }
        CommandItem item;
        item.text = edit->text();
        item.hexMode = hexMode;
        item.valid = !hexMode || validateHexPayload(item.text);
        commands.append(item);
    }
    return commands;
}

/** 判断当前模式是否为 TCP Network。 */
bool MainWindow::isTcpMode() const
{
    return ui->comboBoxMode->currentIndex() == 0;
}

/** 为全部 TCP 会话创建基于任务队列的 worker 并发起连接。 */
void MainWindow::connectTcpPorts()
{
    if (m_tcpSessions.isEmpty()) {
        appendLog(LogLevel::Error, QStringLiteral("Add at least one TCP port first"));
        return;
    }

    QHostAddress address;
    if (!address.setAddress(ui->lineEditIp->text().trimmed())) {
        appendLog(LogLevel::Error, QStringLiteral("Invalid IP address"));
        return;
    }

    destroyTcpWorkers();
    for (TcpPortSession *session : m_tcpSessions) {
        auto *worker = new TcpClientWorker(address.toString(), session->port);
        session->worker = worker;
        session->connecting = true;
        const quint16 sessionPort = session->port;

        QObject::connect(worker, &TcpClientWorker::signalConnected, this, [this, sessionPort, worker]() {
            TcpPortSession *session = m_tcpSessions.value(sessionPort, nullptr);
            if (!session || session->worker != worker) return;
            session->connected = true;
            session->connecting = false;
            updateTcpPortRow(session, QStringLiteral("Connected"));
            appendLog(LogLevel::Info, QStringLiteral("[Port %1] Connected").arg(session->port));
            updateTcpConnectionState();
        });
        QObject::connect(worker, &TcpClientWorker::signalDisconnected, this, [this, sessionPort, worker]() {
            TcpPortSession *session = m_tcpSessions.value(sessionPort, nullptr);
            if (!session || session->worker != worker) return;
            session->connected = false;
            session->connecting = false;
            updateTcpPortRow(session, QStringLiteral("Disconnected"));
            appendLog(LogLevel::Info, QStringLiteral("[Port %1] Disconnected").arg(session->port));
            if (session->testRunning) {
                session->statisticsValid = false;
                stopTcpTest(true);
            }
            updateTcpConnectionState();
        });
        QObject::connect(worker, &TcpClientWorker::signalDataReceived, this, [this, sessionPort, worker](const QByteArray &data, bool responseValid) {
            TcpPortSession *session = m_tcpSessions.value(sessionPort, nullptr);
            if (!session || session->worker != worker) return;
            if (!session->testRunning && !session->oneShotRunning) return;
            PacketInfo packet;
            qint64 pendingElapsedMs = 0;
            const bool hasPending = session->statistics.oldestPendingElapsed(&pendingElapsedMs);
            const bool responseTimedOut = hasPending && responseReachedTimeout(pendingElapsedMs, ui->spinBoxTimeout->value());
            if (!responseValid || responseTimedOut) {
                const QString reason = !responseValid
                    ? QStringLiteral("sticky response")
                    : QStringLiteral("response exceeded Timeout %1 ms").arg(ui->spinBoxTimeout->value());
                handleTcpAbnormalResponse(session, data, reason, pendingElapsedMs);
            } else if (session->statistics.recordReceive(data, &packet)) {
                session->awaitingResponse = false;
                if (session->oneShotRunning) {
                    if (session->oneShotCommands.isEmpty()) {
                        session->oneShotRunning = false;
                    } else {
                        scheduleNextOneShotTcpPacket(session, ui->spinBoxInterval->value());
                    }
                } else if (session->testRunning) {
                    scheduleNextTcpPacket(session, ui->spinBoxInterval->value());
                }
                const bool responseTimedOut = responseReachedTimeout(packet.elapsedMs, ui->spinBoxTimeout->value());
                const LogLevel responseLevel = responseTimedOut ? LogLevel::Error : LogLevel::Rx;
                const QString responseText = responseTimedOut
                    ? QStringLiteral("[Port %1] #%2 %3 (response exceeded Timeout %4 ms)")
                          .arg(session->port).arg(packet.id).arg(payloadToDisplay(data, packet.txFormat, false)).arg(ui->spinBoxTimeout->value())
                    : QStringLiteral("[Port %1] #%2 %3").arg(session->port).arg(packet.id).arg(payloadToDisplay(data, packet.txFormat, false));
                appendLog(responseLevel, responseText, packet.elapsedMs);
                if (responseTimedOut && session->testRunning) session->statisticsValid = false;
                if (!session->oneShotRunning && session->oneShotCommands.isEmpty() && !session->testRunning) {
                    appendLog(LogLevel::Info, QStringLiteral("[Port %1] Send complete").arg(session->port));
                }
            } else {
                handleTcpAbnormalResponse(session, data, QStringLiteral("Unmatched or late response"), pendingElapsedMs);
            }
            scheduleStatsRefresh();
        });
        QObject::connect(worker, &TcpClientWorker::signalDataSent, this, [this, sessionPort, worker](const QByteArray &data, const QString &format) {
            TcpPortSession *session = m_tcpSessions.value(sessionPort, nullptr);
            if (!session || session->worker != worker) return;
            if (!session->testRunning && !session->oneShotRunning) return;
            const PacketInfo packet = session->statistics.recordSend(data, format);
            appendLog(LogLevel::Tx, QStringLiteral("[Port %1] #%2 %3")
                                      .arg(session->port).arg(packet.id).arg(payloadToDisplay(data, format, false)));
        });
        QObject::connect(worker, &TcpClientWorker::signalReceiveTimeout, this, [this, sessionPort, worker]() {
            TcpPortSession *session = m_tcpSessions.value(sessionPort, nullptr);
            if (!session || session->worker != worker) return;
            if (!session->testRunning && !session->oneShotRunning) return;
            if (session->testRunning) session->statisticsValid = false;
            qint64 elapsedMs = 0;
            session->statistics.oldestPendingElapsed(&elapsedMs);
            session->statistics.markOldestPendingLost(elapsedMs);
            session->awaitingResponse = false;
            if (session->oneShotRunning) {
                if (session->oneShotCommands.isEmpty()) session->oneShotRunning = false;
                else scheduleNextOneShotTcpPacket(session, ui->spinBoxInterval->value());
            } else if (session->testRunning) {
                if (session->finishingAfterLimit) {
                    if (!session->statistics.hasPendingPackets()) stopTcpTest(false);
                } else {
                    scheduleNextTcpPacket(session, ui->spinBoxInterval->value());
                }
            }
            appendLog(LogLevel::Error, QStringLiteral("[Port %1] recv timeout/lost").arg(session->port));
        });
        QObject::connect(worker, &TcpClientWorker::signalErrorOccurred, this, [this, sessionPort, worker](const QString &message) {
            TcpPortSession *session = m_tcpSessions.value(sessionPort, nullptr);
            if (session && session->worker == worker) {
                if (session->testRunning) session->statisticsValid = false;
                appendLog(LogLevel::Error, QStringLiteral("[Port %1] %2").arg(session->port).arg(message));
                if (session->connecting) {
                    session->connecting = false;
                    updateTcpPortRow(session, QStringLiteral("Connection failed"));
                    updateTcpConnectionState();
                }
            }
        });
        QObject::connect(worker, &TcpClientWorker::signalSendFailed, this, [this, sessionPort, worker](const QString &message) {
            TcpPortSession *session = m_tcpSessions.value(sessionPort, nullptr);
            if (!session || session->worker != worker) return;
            handleTcpSendFailure(session, message);
        });
        QObject::connect(worker, &TcpClientWorker::signalResponseAborted, this, [this, sessionPort, worker](const QString &message) {
            TcpPortSession *session = m_tcpSessions.value(sessionPort, nullptr);
            if (!session || session->worker != worker) return;
            handleTcpAbnormalResponse(session, {}, message, -1);
        });
        worker->setReceiveTimeout(ui->spinBoxTimeout->value());
        worker->setSendInterval(ui->spinBoxInterval->value());
        worker->connect();
        updateTcpPortRow(session, QStringLiteral("Connecting"));
    }
    ui->pushButtonConnect->setEnabled(false);
    ui->pushButtonDisconnect->setEnabled(true);
    appendLog(LogLevel::Info, QStringLiteral("Connecting to %1 TCP ports at %2").arg(m_tcpSessions.size()).arg(address.toString()));
}

/** 关闭全部 TCP worker 并更新每个端口的状态。 */
void MainWindow::disconnectTcpPorts()
{
    destroyTcpWorkers();
    for (TcpPortSession *session : m_tcpSessions) {
        session->connected = false;
        session->connecting = false;
        updateTcpPortRow(session, QStringLiteral("Disconnected"));
    }
    updateTcpConnectionState();
    appendLog(LogLevel::Info, QStringLiteral("All TCP connections closed"));
}

/** 停止并释放全部 TCP 会话的定时器和任务队列 worker。 */
void MainWindow::destroyTcpWorkers()
{
    for (TcpPortSession *session : m_tcpSessions) {
        if (session->sendTimer) session->sendTimer->stop();
        session->oneShotCommands.clear();
        session->oneShotRunning = false;
        if (!session->worker) {
            session->worker = nullptr;
            continue;
        }
        if (session->worker) {
            QObject::disconnect(session->worker, nullptr, this, nullptr);
            static_cast<TcpClientWorker *>(session->worker)->disconnect();
            delete static_cast<TcpClientWorker *>(session->worker);
        }
        delete session->sendTimer;
        session->sendTimer = nullptr;
        session->worker = nullptr;
        session->connected = false;
        session->connecting = false;
    }
}

/** 更新 TCP 状态表中指定端口的状态文本。 */
void MainWindow::updateTcpPortRow(TcpPortSession *session, const QString &state)
{
    if (!session || !m_tcpPortTable) return;
    for (int row = 0; row < m_tcpPortTable->rowCount(); ++row) {
        if (m_tcpPortTable->item(row, 0) && m_tcpPortTable->item(row, 0)->text().toUShort() == session->port) {
            m_tcpPortTable->item(row, 1)->setText(state);
            return;
        }
    }
}

/** 汇总 TCP 连接数并刷新顶部状态 LED 和按钮。 */
void MainWindow::updateTcpConnectionState()
{
    int connected = 0;
    int connecting = 0;
    for (const TcpPortSession *session : m_tcpSessions) {
        connected += session->connected ? 1 : 0;
        connecting += session->connecting ? 1 : 0;
    }
    m_connected = connected == m_tcpSessions.size() && !m_tcpSessions.isEmpty();
    QString state = QStringLiteral("Disconnected");
    QString color = QStringLiteral("#b64d4d");
    if (m_connected) {
        state = QStringLiteral("Connected %1/%2").arg(connected).arg(m_tcpSessions.size());
        color = QStringLiteral("#19b982");
    } else if (connecting > 0) {
        state = QStringLiteral("Connecting %1/%2").arg(connected).arg(m_tcpSessions.size());
        color = QStringLiteral("#d7a72f");
    }
    ui->labelConnectionState->setText(state);
    ui->frameLed->setStyleSheet(QStringLiteral("background:%1; border:1px solid rgba(255,255,255,0.35); border-radius:8px;").arg(color));
    ui->pushButtonConnect->setEnabled(!m_testRunning && connecting == 0 &&
                                      (m_tcpSessions.isEmpty() || connected != m_tcpSessions.size()));
    ui->pushButtonDisconnect->setEnabled(connected > 0 || connecting > 0);
}

/** 创建基于任务队列的串口 worker 并并发打开全部串口。 */
void MainWindow::connectSerialPorts()
{
    if (m_serialSessions.isEmpty()) {
        appendLog(LogLevel::Error, QStringLiteral("Add at least one serial port first"));
        return;
    }

    destroySerialWorkers();
    int started = 0;
    for (SerialPortSession *session : m_serialSessions) {
        const SerialSettings settings = serialSettings(session);
        if (settings.portName.isEmpty()) {
            appendLog(LogLevel::Error, QStringLiteral("[%1] Invalid serial port name").arg(session->portName));
            continue;
        }
        auto *worker = new SerialClientWorker(settings);
        session->worker = worker;
        session->connecting = true;
        const QString sessionPortName = session->portName;
        QObject::connect(worker, &SerialClientWorker::signalConnected, this, [this, sessionPortName, worker]() {
            SerialPortSession *session = m_serialSessions.value(sessionPortName, nullptr);
            if (!session || session->worker != worker) return;
            session->connected = true;
            session->connecting = false;
            if (session->portCombo) session->portCombo->setEnabled(false);
            updateSerialPortRow(session, QStringLiteral("Connected"));
            appendLog(LogLevel::Info, QStringLiteral("[%1] Connected").arg(session->portName));
            updateSerialConnectionState();
        });
        QObject::connect(worker, &SerialClientWorker::signalDisconnected, this, [this, sessionPortName, worker]() {
            SerialPortSession *session = m_serialSessions.value(sessionPortName, nullptr);
            if (!session || session->worker != worker) return;
            session->connected = false;
            session->connecting = false;
            if (session->portCombo) session->portCombo->setEnabled(true);
            session->oneShotRunning = false;
            session->oneShotCommands.clear();
            if (session->sendTimer) session->sendTimer->stop();
            updateSerialPortRow(session, QStringLiteral("Disconnected"));
            appendLog(LogLevel::Info, QStringLiteral("[%1] Disconnected").arg(session->portName));
            if (session->testRunning) {
                session->statisticsValid = false;
                if (session->sendTimer) session->sendTimer->stop();
                session->testRunning = false;
                session->finishingAfterLimit = true;
                const QVector<PacketInfo> lost = session->statistics.markAllPendingLost();
                for (const PacketInfo &packet : lost) appendLog(LogLevel::Error, QStringLiteral("[%1] #%2 lost after disconnect").arg(session->portName).arg(packet.id));
                bool anyRunning = false;
                for (const SerialPortSession *other : m_serialSessions) anyRunning = anyRunning || other->testRunning;
                m_testRunning = anyRunning;
            }
            updateSerialConnectionState();
        });
        QObject::connect(worker, &SerialClientWorker::signalDataReceived, this, [this, sessionPortName, worker](const QByteArray &data, bool responseValid) {
            SerialPortSession *session = m_serialSessions.value(sessionPortName, nullptr);
            if (!session || session->worker != worker) return;
            if (!session->testRunning && !session->oneShotRunning) return;
            PacketInfo packet;
            qint64 pendingElapsedMs = 0;
            const bool hasPending = session->statistics.oldestPendingElapsed(&pendingElapsedMs);
            const bool responseTimedOut = hasPending && responseReachedTimeout(pendingElapsedMs, ui->spinBoxTimeout->value());
            if (!responseValid || responseTimedOut) {
                const QString reason = !responseValid
                    ? QStringLiteral("sticky response")
                    : QStringLiteral("response exceeded Timeout %1 ms").arg(ui->spinBoxTimeout->value());
                handleSerialAbnormalResponse(session, data, reason, pendingElapsedMs);
            } else if (session->statistics.recordReceive(data, &packet)) {
                session->awaitingResponse = false;
                if (session->oneShotRunning) {
                    if (session->oneShotCommands.isEmpty()) {
                        session->oneShotRunning = false;
                    } else {
                        scheduleNextOneShotSerialPacket(session, ui->spinBoxInterval->value());
                    }
                } else if (session->testRunning) {
                    scheduleNextSerialPacket(session, ui->spinBoxInterval->value());
                }
                const bool responseTimedOut = responseReachedTimeout(packet.elapsedMs, ui->spinBoxTimeout->value());
                const LogLevel responseLevel = responseTimedOut ? LogLevel::Error : LogLevel::Rx;
                const QString responseText = responseTimedOut
                    ? QStringLiteral("[%1] #%2 %3 (response exceeded Timeout %4 ms)")
                          .arg(session->portName).arg(packet.id).arg(payloadToDisplay(data, packet.txFormat, false)).arg(ui->spinBoxTimeout->value())
                    : QStringLiteral("[%1] #%2 %3").arg(session->portName).arg(packet.id).arg(payloadToDisplay(data, packet.txFormat, false));
                appendLog(responseLevel, responseText, packet.elapsedMs);
                if (responseTimedOut && session->testRunning) session->statisticsValid = false;
                if (!session->oneShotRunning && session->oneShotCommands.isEmpty() && !session->testRunning) {
                    appendLog(LogLevel::Info, QStringLiteral("[%1] Send complete").arg(session->portName));
                }
            } else {
                handleSerialAbnormalResponse(session, data, QStringLiteral("Unmatched or late response"), pendingElapsedMs);
            }
            updateSerialSessionStats(session);
            scheduleStatsRefresh();
        });
        QObject::connect(worker, &SerialClientWorker::signalDataSent, this, [this, sessionPortName, worker](const QByteArray &data, const QString &format) {
            SerialPortSession *session = m_serialSessions.value(sessionPortName, nullptr);
            if (!session || session->worker != worker) return;
            if (!session->testRunning && !session->oneShotRunning) return;
            const PacketInfo packet = session->statistics.recordSend(data, format);
            appendLog(LogLevel::Tx, QStringLiteral("[%1] #%2 %3")
                                      .arg(session->portName).arg(packet.id).arg(payloadToDisplay(data, format, false)));
        });
        QObject::connect(worker, &SerialClientWorker::signalReceiveTimeout, this, [this, sessionPortName, worker]() {
            SerialPortSession *session = m_serialSessions.value(sessionPortName, nullptr);
            if (!session || session->worker != worker) return;
            if (!session->testRunning && !session->oneShotRunning) return;
            if (session->testRunning) session->statisticsValid = false;
            qint64 elapsedMs = 0;
            session->statistics.oldestPendingElapsed(&elapsedMs);
            session->statistics.markOldestPendingLost(elapsedMs);
            session->awaitingResponse = false;
            if (session->oneShotRunning) {
                if (session->oneShotCommands.isEmpty()) session->oneShotRunning = false;
                else scheduleNextOneShotSerialPacket(session, ui->spinBoxInterval->value());
            } else if (session->testRunning) {
                if (session->finishingAfterLimit) {
                    if (!session->statistics.hasPendingPackets()) stopSerialTest(false);
                } else {
                    scheduleNextSerialPacket(session, ui->spinBoxInterval->value());
                }
            }
            appendLog(LogLevel::Error, QStringLiteral("[%1] recv timeout/lost").arg(session->portName));
        });
        QObject::connect(worker, &SerialClientWorker::signalErrorOccurred, this, [this, sessionPortName, worker](const QString &message) {
            SerialPortSession *session = m_serialSessions.value(sessionPortName, nullptr);
            if (!session || session->worker != worker) return;
            if (session->testRunning) session->statisticsValid = false;
            appendLog(LogLevel::Error, QStringLiteral("[%1] %2").arg(session->portName, message));
            if (session->connecting) {
                session->connecting = false;
                updateSerialPortRow(session, QStringLiteral("Connection failed"));
                updateSerialConnectionState();
            }
        });
        QObject::connect(worker, &SerialClientWorker::signalSendFailed, this, [this, sessionPortName, worker](const QString &message) {
            SerialPortSession *session = m_serialSessions.value(sessionPortName, nullptr);
            if (!session || session->worker != worker) return;
            handleSerialSendFailure(session, message);
        });
        QObject::connect(worker, &SerialClientWorker::signalResponseAborted, this, [this, sessionPortName, worker](const QString &message) {
            SerialPortSession *session = m_serialSessions.value(sessionPortName, nullptr);
            if (!session || session->worker != worker) return;
            handleSerialAbnormalResponse(session, {}, message, -1);
        });
        worker->setReceiveTimeout(ui->spinBoxTimeout->value());
        worker->setSendInterval(ui->spinBoxInterval->value());
        worker->connect();
        updateSerialPortRow(session, QStringLiteral("Connecting"));
        ++started;
    }
    appendLog(LogLevel::Info, QStringLiteral("Connecting to %1 serial ports").arg(started));
    updateSerialConnectionState();
}

/** 关闭全部串口连接并恢复可编辑状态。 */
void MainWindow::disconnectSerialPorts()
{
    destroySerialWorkers();
    for (SerialPortSession *session : m_serialSessions) {
        session->connected = false;
        session->connecting = false;
        if (session->portCombo) session->portCombo->setEnabled(true);
        updateSerialPortRow(session, QStringLiteral("Disconnected"));
    }
    updateSerialConnectionState();
    appendLog(LogLevel::Info, QStringLiteral("All serial connections closed"));
}

/** 关闭一个串口 worker 的设备、任务队列和发送定时器。 */
void MainWindow::destroySerialWorker(SerialPortSession *session)
{
    if (!session) return;
    if (session->sendTimer) session->sendTimer->stop();
    session->oneShotCommands.clear();
    session->oneShotRunning = false;
    if (!session->worker) {
        session->worker = nullptr;
        session->connected = false;
        session->connecting = false;
        return;
    }
    if (session->worker) {
        QObject::disconnect(session->worker, nullptr, this, nullptr);
        session->worker->disconnect();
        delete session->worker;
    }
    session->worker = nullptr;
    session->connected = false;
    session->connecting = false;
}

/** 遍历并关闭所有串口 worker。 */
void MainWindow::destroySerialWorkers()
{
    for (SerialPortSession *session : m_serialSessions) destroySerialWorker(session);
}

/** 更新串口状态表中指定串口的状态文本。 */
void MainWindow::updateSerialPortRow(SerialPortSession *session, const QString &state)
{
    if (!session || !m_serialPortTable) return;
    for (int row = 0; row < m_serialPortTable->rowCount(); ++row) {
        if (m_serialPortTable->item(row, 0) && m_serialPortTable->item(row, 0)->text() == session->portName) {
            m_serialPortTable->item(row, 1)->setText(state);
            return;
        }
    }
}

/** 汇总串口连接数并刷新顶部状态 LED 和按钮。 */
void MainWindow::updateSerialConnectionState()
{
    int connected = 0;
    int connecting = 0;
    for (const SerialPortSession *session : m_serialSessions) {
        connected += session->connected ? 1 : 0;
        connecting += session->connecting ? 1 : 0;
    }
    m_connected = connected == m_serialSessions.size() && !m_serialSessions.isEmpty();
    QString state = QStringLiteral("Disconnected");
    QString color = QStringLiteral("#b64d4d");
    if (m_connected) {
        state = QStringLiteral("Connected %1/%2").arg(connected).arg(m_serialSessions.size());
        color = QStringLiteral("#19b982");
    } else if (connecting > 0) {
        state = QStringLiteral("Connecting %1/%2").arg(connected).arg(m_serialSessions.size());
        color = QStringLiteral("#d7a72f");
    } else if (connected > 0) {
        state = QStringLiteral("Partial %1/%2").arg(connected).arg(m_serialSessions.size());
        color = QStringLiteral("#d7a72f");
    }
    ui->labelConnectionState->setText(state);
    ui->frameLed->setStyleSheet(QStringLiteral("background:%1; border:1px solid rgba(255,255,255,0.35); border-radius:8px;").arg(color));
    ui->pushButtonConnect->setEnabled(!m_testRunning && connecting == 0 && connected != m_serialSessions.size());
    ui->pushButtonDisconnect->setEnabled(connected > 0 || connecting > 0);
}

/** 将指定串口的全部命令加入单次发送队列。 */
void MainWindow::sendAllSerialPort(SerialPortSession *session)
{
    if (!session || session->testRunning || session->oneShotRunning) return;
    if (!session->connected || !session->worker) {
        appendLog(LogLevel::Error, QStringLiteral("[%1] Not connected").arg(session ? session->portName : QStringLiteral("?")));
        return;
    }
    const QList<CommandItem> commands = collectSerialCommands(session);
    if (!validateCommands(commands)) return;
    logSerialSessionParameters(session, QStringLiteral("Send All"));
    session->oneShotCommands.clear();
    for (const CommandItem &item : commands) session->oneShotCommands.enqueue(item);
    session->statistics.reset();
    session->worker->resetStatistics();
    session->oneShotRunning = true;
    session->nextOneShotDeadlineMs = 0;
    session->awaitingResponse = false;
    m_timeoutTimer->start();
    session->sendClock.start();
    appendLog(LogLevel::Info, QStringLiteral("[%1] Sending all commands").arg(session->portName));
    sendNextOneShotSerialPacket(session);
}

/** 发送串口单次队列中的下一条命令，并安排后续命令。 */
void MainWindow::sendNextOneShotSerialPacket(SerialPortSession *session)
{
    if (!session || !session->oneShotRunning || !session->worker || !session->connected) return;
    if (session->awaitingResponse) return;
    session->nextOneShotDeadlineMs = 0;
    if (session->oneShotCommands.isEmpty()) {
        session->oneShotRunning = false;
        session->nextOneShotDeadlineMs = 0;
        appendLog(LogLevel::Info, QStringLiteral("[%1] Send complete").arg(session->portName));
        return;
    }
    const CommandItem item = session->oneShotCommands.dequeue();
    const QByteArray payload = item.hexMode ? QByteArray::fromHex(item.text.simplified().remove(QRegularExpression(QStringLiteral("\\s+"))).toLatin1()) : item.text.toUtf8();
    if (!payload.isEmpty()) {
        const QString format = item.hexMode ? QStringLiteral("HEX") : QStringLiteral("ASCII");
        session->awaitingResponse = true;
        session->worker->sendDataWithFormat(payload, format);
        updateSerialSessionStats(session);
        scheduleStatsRefresh();
    } else if (!session->oneShotCommands.isEmpty()) {
        scheduleNextOneShotSerialPacket(session, ui->spinBoxInterval->value());
    } else {
        session->oneShotRunning = false;
        appendLog(LogLevel::Info, QStringLiteral("[%1] Send complete").arg(session->portName));
    }
}

/** 使用绝对目标时间安排串口单次队列的下一次发送。 */
void MainWindow::scheduleNextOneShotSerialPacket(SerialPortSession *session, int intervalMs)
{
    Q_UNUSED(intervalMs);
    if (!session || !session->oneShotRunning) return;
    sendNextOneShotSerialPacket(session);
}

/** 启动全部已连接串口的连续性能测试。 */
void MainWindow::handleSerialAbnormalResponse(SerialPortSession *session, const QByteArray &data,
                                               const QString &reason, qint64 elapsedMs)
{
    if (!session) return;

    qint64 effectiveElapsedMs = elapsedMs;
    if (effectiveElapsedMs < 0) session->statistics.oldestPendingElapsed(&effectiveElapsedMs);

    PacketInfo lostPacket;
    const bool lost = session->statistics.hasPendingPackets() &&
                      session->statistics.markOldestPendingLost(effectiveElapsedMs, &lostPacket);
    session->awaitingResponse = false;
    if (session->testRunning) session->statisticsValid = false;

    const QString prefix = QStringLiteral("[%1]").arg(session->portName);
    if (lost) {
        if (reason == QStringLiteral("sticky response")) {
            appendLog(LogLevel::Error,
                      QStringLiteral("%1 #%2 sticky response: %3")
                          .arg(prefix)
                          .arg(lostPacket.id)
                          .arg(payloadToDisplay(data, lostPacket.txFormat, false)),
                      effectiveElapsedMs);
        } else {
            appendLog(LogLevel::Error,
                      QStringLiteral("%1 #%2 %3%4")
                          .arg(prefix)
                          .arg(lostPacket.id)
                          .arg(reason)
                          .arg(data.isEmpty() ? QString() : QStringLiteral(" (%1)").arg(payloadToDisplay(data, lostPacket.txFormat, false))),
                      effectiveElapsedMs);
        }
    } else {
        appendLog(LogLevel::Error,
                  QStringLiteral("%1 %2%3")
                      .arg(prefix)
                      .arg(reason)
                      .arg(data.isEmpty() ? QString() : QStringLiteral(" %1").arg(payloadToDisplay(data, QString(), false))));
    }

    if (!session->connected) return;
    if (session->oneShotRunning) {
        if (session->oneShotCommands.isEmpty()) {
            session->oneShotRunning = false;
        } else {
            scheduleNextOneShotSerialPacket(session, ui->spinBoxInterval->value());
        }
    } else if (session->testRunning && !session->finishingAfterLimit) {
        scheduleNextSerialPacket(session, ui->spinBoxInterval->value());
    }
}

void MainWindow::handleSerialSendFailure(SerialPortSession *session, const QString &reason)
{
    handleSerialAbnormalResponse(session, {}, QStringLiteral("send failed: %1").arg(reason), -1);
}

void MainWindow::startSerialTest()
{
    if (m_serialSessions.isEmpty()) {
        appendLog(LogLevel::Error, QStringLiteral("Add at least one serial port first"));
        return;
    }
    int active = 0;
    for (SerialPortSession *session : m_serialSessions) {
        if (!session->connected || !session->worker) {
            appendLog(LogLevel::Error, QStringLiteral("[%1] Skipped: not connected").arg(session->portName));
            continue;
        }
        const QList<CommandItem> commands = collectSerialCommands(session);
        if (!validateCommands(commands)) continue;
        logSerialSessionParameters(session, QStringLiteral("Continuous Test"));
        session->statistics.reset();
        session->worker->resetStatistics();
        session->statisticsValid = true;
        session->awaitingResponse = false;
        session->currentCommandIndex = 0;
        session->perCommandSendCount = QVector<int>(commands.size(), 0);
        session->testRunning = true;
        session->finishingAfterLimit = false;
        session->sendClock.start();
        session->nextSendDeadlineMs = 0;
        ++active;
    }
    if (active == 0) {
        appendLog(LogLevel::Error, QStringLiteral("No connected serial port is ready"));
        return;
    }
    m_testRunning = true;
    m_serialFinalStats.clear();
    ui->pushButtonStart->setEnabled(false);
    ui->pushButtonStop->setEnabled(true);
    ui->pushButtonConnect->setEnabled(false);
    ui->comboBoxMode->setEnabled(false);
    if (m_serialAddPortButton) m_serialAddPortButton->setEnabled(false);
    if (m_serialRemovePortButton) m_serialRemovePortButton->setEnabled(false);
    if (m_serialRefreshButton) m_serialRefreshButton->setEnabled(false);
    if (m_serialSendAllButton) m_serialSendAllButton->setEnabled(false);
    if (m_serialPortTable) m_serialPortTable->setEnabled(false);
    for (SerialPortSession *session : m_serialSessions) {
        if (session && session->commandPage) session->commandPage->setEnabled(false);
    }
    m_timeoutTimer->start();
    appendLog(LogLevel::Info, QStringLiteral("Multi-serial test started: %1 ports, timeout %2 ms").arg(active).arg(ui->spinBoxTimeout->value()));
    for (SerialPortSession *session : m_serialSessions) if (session->testRunning) sendNextSerialPacket(session);
}

/** 停止串口连续测试，标记未完成请求并保存报告。 */
void MainWindow::stopSerialTest(bool manualStop)
{
    bool anyRunning = m_testRunning;
    for (SerialPortSession *session : m_serialSessions) anyRunning = anyRunning || session->testRunning;
    if (!anyRunning) return;
    for (SerialPortSession *session : m_serialSessions) {
        if (session->sendTimer) session->sendTimer->stop();
        session->oneShotRunning = false;
        session->testRunning = false;
        session->finishingAfterLimit = false;
        const QVector<PacketInfo> lost = session->statistics.markAllPendingLost();
        if (!lost.isEmpty() && !manualStop) session->statisticsValid = false;
        for (const PacketInfo &packet : lost) appendLog(LogLevel::Error, QStringLiteral("[%1] #%2 lost").arg(session->portName).arg(packet.id));
    }
    m_testRunning = false;
    m_timeoutTimer->stop();
    m_serialFinalStats.clear();
    for (SerialPortSession *session : m_serialSessions) {
        const StatisticsSnapshot snap = session->statistics.snapshot();
        m_serialFinalStats.insert(session->portName, snap);
        appendLog(LogLevel::Info, QStringLiteral("[%1] Final P50=%2 ms P90=%3 ms P95=%4 ms P99=%5 ms Success=%6/%7 Rate=%8% Average=%9 ms Max=%10 ms Min=%11 ms")
                                      .arg(session->portName).arg(snap.p50Ms, 0, 'f', 0).arg(snap.p90Ms, 0, 'f', 0)
                                      .arg(snap.p95Ms, 0, 'f', 0).arg(snap.p99Ms, 0, 'f', 0).arg(snap.successReceived)
                                      .arg(snap.totalSent).arg(snap.successRate, 0, 'f', 2).arg(snap.averageElapsedMs, 0, 'f', 3).arg(snap.maxElapsedMs).arg(snap.minElapsedMs));
        if (!session->statisticsValid) {
            appendLog(LogLevel::Error, QStringLiteral("[%1] Final statistics include communication errors").arg(session->portName));
        }
        updateSerialSessionStats(session);
    }
    if (manualStop) appendLog(LogLevel::Info, QStringLiteral("Multi-serial test stopped manually"));
    scheduleStatsRefresh();
    ui->pushButtonStart->setEnabled(true);
    ui->pushButtonStop->setEnabled(false);
    ui->comboBoxMode->setEnabled(true);
    if (m_serialAddPortButton) m_serialAddPortButton->setEnabled(true);
    if (m_serialRemovePortButton) m_serialRemovePortButton->setEnabled(true);
    if (m_serialRefreshButton) m_serialRefreshButton->setEnabled(true);
    if (m_serialSendAllButton) m_serialSendAllButton->setEnabled(true);
    if (m_serialPortTable) m_serialPortTable->setEnabled(true);
    for (SerialPortSession *session : m_serialSessions) {
        if (session && session->commandPage) session->commandPage->setEnabled(true);
    }
    updateSerialConnectionState();
}

/** 发送指定串口连续测试的下一条命令。 */
void MainWindow::sendNextSerialPacket(SerialPortSession *session)
{
    if (!session || !session->testRunning || !session->worker || !session->connected) return;
    if (session->awaitingResponse) return;
    session->nextSendDeadlineMs = 0;
    const QList<CommandItem> commands = collectSerialCommands(session);
    if (commands.isEmpty()) {
        session->finishingAfterLimit = true;
        return;
    }
    const int targetCount = ui->spinBoxSendCount->value();
    if (session->statistics.pendingPacketCount() >= kMaxInFlightPackets) {
        session->awaitingResponse = true;
        return;
    }
    if (session->perCommandSendCount.size() != commands.size()) session->perCommandSendCount = QVector<int>(commands.size(), 0);
    int checked = 0;
    while (checked < commands.size() && targetCount > 0 && session->perCommandSendCount.value(session->currentCommandIndex) >= targetCount) {
        session->currentCommandIndex = (session->currentCommandIndex + 1) % commands.size();
        ++checked;
    }
    if (checked >= commands.size()) {
        session->finishingAfterLimit = true;
        checkSerialTimeouts();
        return;
    }
    const CommandItem &item = commands.at(session->currentCommandIndex);
    const QByteArray payload = item.hexMode ? QByteArray::fromHex(item.text.simplified().remove(QRegularExpression(QStringLiteral("\\s+"))).toLatin1()) : item.text.toUtf8();
    if (!payload.isEmpty()) {
        ++session->perCommandSendCount[session->currentCommandIndex];
        session->awaitingResponse = true;
        session->worker->sendDataWithFormat(payload, item.hexMode ? QStringLiteral("HEX") : QStringLiteral("ASCII"));
        updateSerialSessionStats(session);
        scheduleStatsRefresh();
    }
    session->currentCommandIndex = (session->currentCommandIndex + 1) % commands.size();
}

/** 使用绝对目标时间安排串口连续测试的下一次发送。 */
void MainWindow::scheduleNextSerialPacket(SerialPortSession *session, int intervalMs)
{
    Q_UNUSED(intervalMs);
    if (!session || !session->testRunning || session->finishingAfterLimit) return;
    sendNextSerialPacket(session);
}

/** 扫描串口会话超时状态，并在全部会话完成后停止测试。 */
void MainWindow::checkSerialTimeouts()
{
    bool allFinished = true;
    bool hasTest = false;
    for (SerialPortSession *session : m_serialSessions) {
        if (!session->testRunning && !session->oneShotRunning) continue;
        if (session->testRunning) hasTest = true;
        if (!(session->finishingAfterLimit && !session->statistics.hasPendingPackets()) && session->testRunning) allFinished = false;
        updateSerialSessionStats(session);
    }
    if (hasTest && allFinished) stopSerialTest(false);
    scheduleStatsRefresh();
}

/** 根据端口表当前选择，对单个 TCP 端口执行 Send All。 */
void MainWindow::slotSendSelectedTcpPort()
{
    if (!m_tcpPortTable) return;
    const int row = m_tcpPortTable->currentRow();
    if (row >= 0 && m_tcpPortTable->item(row, 0)) {
        sendAllTcpPort(m_tcpSessions.value(static_cast<quint16>(m_tcpPortTable->item(row, 0)->text().toUShort())));
    }
}

/** 对全部 TCP 会话并发执行单次 Send All。 */
void MainWindow::slotSendAllTcpPorts()
{
    for (TcpPortSession *session : m_tcpSessions) {
        sendAllTcpPort(session);
    }
}

/** 将指定 TCP 会话的命令加入单次发送队列。 */
void MainWindow::sendAllTcpPort(TcpPortSession *session)
{
    if (!session || session->testRunning) {
        return;
    }
    if (!session->connected || !session->worker) {
        appendLog(LogLevel::Error, QStringLiteral("[Port %1] Not connected").arg(session->port));
        return;
    }
    const QList<CommandItem> commands = collectCommands(session->commandTable);
    if (!validateCommands(commands)) return;
    logTcpSessionParameters(session, QStringLiteral("Send All"));
    session->oneShotCommands.clear();
    for (const CommandItem &item : commands) {
        session->oneShotCommands.enqueue(item);
    }
    session->statistics.reset();
    session->worker->resetStatistics();
    session->oneShotRunning = true;
    session->nextOneShotDeadlineMs = 0;
    session->awaitingResponse = false;
    m_timeoutTimer->start();
    session->sendClock.start();
    sendNextOneShotTcpPacket(session);
}

/** 发送 TCP 单次队列中的下一条命令。 */
void MainWindow::sendNextOneShotTcpPacket(TcpPortSession *session)
{
    if (!session || !session->oneShotRunning || !session->worker || !session->connected) {
        return;
    }
    if (session->awaitingResponse) return;
    session->nextOneShotDeadlineMs = 0;
    if (session->oneShotCommands.isEmpty()) {
        session->oneShotRunning = false;
        session->nextOneShotDeadlineMs = 0;
        return;
    }

    const CommandItem item = session->oneShotCommands.dequeue();
    const QByteArray payload = item.hexMode
        ? QByteArray::fromHex(item.text.simplified().remove(QRegularExpression(QStringLiteral("\\s+"))).toLatin1())
        : item.text.toUtf8();
    if (!payload.isEmpty()) {
        const QString format = item.hexMode ? QStringLiteral("HEX") : QStringLiteral("ASCII");
        session->awaitingResponse = true;
        session->worker->sendDataWithFormat(payload, format);
        scheduleStatsRefresh();
    } else if (!session->oneShotCommands.isEmpty()) {
        scheduleNextOneShotTcpPacket(session, ui->spinBoxInterval->value());
    } else {
        session->oneShotRunning = false;
        session->awaitingResponse = false;
    }
}

/** 使用绝对目标时间安排 TCP 单次队列的下一次发送。 */
void MainWindow::scheduleNextOneShotTcpPacket(TcpPortSession *session, int intervalMs)
{
    Q_UNUSED(intervalMs);
    if (!session || !session->oneShotRunning) return;
    sendNextOneShotTcpPacket(session);
}

/** 启动所有 TCP 端口的连续性能测试。 */
void MainWindow::handleTcpAbnormalResponse(TcpPortSession *session, const QByteArray &data,
                                            const QString &reason, qint64 elapsedMs)
{
    if (!session) return;

    qint64 effectiveElapsedMs = elapsedMs;
    if (effectiveElapsedMs < 0) session->statistics.oldestPendingElapsed(&effectiveElapsedMs);

    PacketInfo lostPacket;
    const bool lost = session->statistics.hasPendingPackets() &&
                      session->statistics.markOldestPendingLost(effectiveElapsedMs, &lostPacket);
    session->awaitingResponse = false;
    if (session->testRunning) session->statisticsValid = false;

    const QString prefix = QStringLiteral("[Port %1]").arg(session->port);
    if (lost) {
        if (reason == QStringLiteral("sticky response")) {
            appendLog(LogLevel::Error,
                      QStringLiteral("%1 #%2 sticky response: %3")
                          .arg(prefix)
                          .arg(lostPacket.id)
                          .arg(payloadToDisplay(data, lostPacket.txFormat, false)),
                      effectiveElapsedMs);
        } else {
            appendLog(LogLevel::Error,
                      QStringLiteral("%1 #%2 %3%4")
                          .arg(prefix)
                          .arg(lostPacket.id)
                          .arg(reason)
                          .arg(data.isEmpty() ? QString() : QStringLiteral(" (%1)").arg(payloadToDisplay(data, lostPacket.txFormat, false))),
                      effectiveElapsedMs);
        }
    } else {
        appendLog(LogLevel::Error,
                  QStringLiteral("%1 %2%3")
                      .arg(prefix)
                      .arg(reason)
                      .arg(data.isEmpty() ? QString() : QStringLiteral(" %1").arg(payloadToDisplay(data, QString(), false))));
    }

    if (!session->connected) return;
    if (session->oneShotRunning) {
        if (session->oneShotCommands.isEmpty()) {
            session->oneShotRunning = false;
        } else {
            scheduleNextOneShotTcpPacket(session, ui->spinBoxInterval->value());
        }
    } else if (session->testRunning && !session->finishingAfterLimit) {
        scheduleNextTcpPacket(session, ui->spinBoxInterval->value());
    }
}

void MainWindow::handleTcpSendFailure(TcpPortSession *session, const QString &reason)
{
    handleTcpAbnormalResponse(session, {}, QStringLiteral("send failed: %1").arg(reason), -1);
}

void MainWindow::startTcpTest()
{
    if (m_tcpSessions.isEmpty() || !m_connected) {
        appendLog(LogLevel::Error, QStringLiteral("Connect all TCP ports before starting"));
        return;
    }
    for (TcpPortSession *session : m_tcpSessions) {
        if (!validateCommands(collectCommands(session->commandTable))) return;
    }

    for (TcpPortSession *session : m_tcpSessions) {
        const QList<CommandItem> commands = collectCommands(session->commandTable);
        logTcpSessionParameters(session, QStringLiteral("Continuous Test"));
        session->statistics.reset();
        session->worker->resetStatistics();
        session->statisticsValid = true;
        session->oneShotCommands.clear();
        session->oneShotRunning = false;
        session->awaitingResponse = false;
        session->nextOneShotDeadlineMs = 0;
        session->currentCommandIndex = 0;
        session->perCommandSendCount = QVector<int>(commands.size(), 0);
        session->testRunning = true;
        session->finishingAfterLimit = false;
        session->sendClock.start();
        session->nextSendDeadlineMs = 0;
    }
    m_testRunning = true;
    m_tcpFinalStats.clear();
    ui->pushButtonStart->setEnabled(false);
    ui->pushButtonStop->setEnabled(true);
    ui->pushButtonConnect->setEnabled(false);
    ui->comboBoxMode->setEnabled(false);
    m_tcpAddPortButton->setEnabled(false);
    m_tcpBatchAddPortButton->setEnabled(false);
    m_tcpRemovePortButton->setEnabled(false);
    m_tcpSendAllButton->setEnabled(false);
    for (TcpPortSession *session : m_tcpSessions) {
        if (session && session->commandPage) session->commandPage->setEnabled(false);
    }
    m_tcpPortTable->setEnabled(false);
    m_timeoutTimer->start();
    appendLog(LogLevel::Info, QStringLiteral("Multi-port test started: %1 ports, timeout %2 ms")
                                 .arg(m_tcpSessions.size()).arg(ui->spinBoxTimeout->value()));
    for (TcpPortSession *session : m_tcpSessions) sendNextTcpPacket(session);
}

/** 停止 TCP 连续测试、处理未完成请求并保存报告。 */
void MainWindow::stopTcpTest(bool manualStop)
{
    bool anyRunning = m_testRunning;
    for (TcpPortSession *session : m_tcpSessions) anyRunning = anyRunning || session->testRunning;
    if (!anyRunning) return;

    for (TcpPortSession *session : m_tcpSessions) {
        if (session->sendTimer) session->sendTimer->stop();
        session->testRunning = false;
        session->finishingAfterLimit = false;
        const QVector<PacketInfo> lost = session->statistics.markAllPendingLost();
        if (!lost.isEmpty() && !manualStop) session->statisticsValid = false;
    }
    m_testRunning = false;
    m_timeoutTimer->stop();
    m_tcpFinalStats.clear();
    for (TcpPortSession *session : m_tcpSessions) {
        const StatisticsSnapshot snap = session->statistics.snapshot();
        m_tcpFinalStats.insert(session->port, snap);
        appendLog(LogLevel::Info, QStringLiteral("[Port %1] Final P50=%2 ms P90=%3 ms P95=%4 ms P99=%5 ms Success=%6/%7 Rate=%8% Average=%9 ms Max=%10 ms Min=%11 ms")
                                     .arg(session->port).arg(snap.p50Ms, 0, 'f', 0).arg(snap.p90Ms, 0, 'f', 0)
                                     .arg(snap.p95Ms, 0, 'f', 0).arg(snap.p99Ms, 0, 'f', 0).arg(snap.successReceived)
                                     .arg(snap.totalSent).arg(snap.successRate, 0, 'f', 2).arg(snap.averageElapsedMs, 0, 'f', 3).arg(snap.maxElapsedMs).arg(snap.minElapsedMs));
        if (!session->statisticsValid) {
            appendLog(LogLevel::Error, QStringLiteral("[Port %1] Final statistics include communication errors").arg(session->port));
        }
    }
    if (manualStop) appendLog(LogLevel::Info, QStringLiteral("Multi-port test stopped manually"));
    scheduleStatsRefresh();
    ui->pushButtonStart->setEnabled(true);
    ui->pushButtonStop->setEnabled(false);
    ui->comboBoxMode->setEnabled(true);
    m_tcpAddPortButton->setEnabled(true);
    m_tcpBatchAddPortButton->setEnabled(true);
    m_tcpRemovePortButton->setEnabled(true);
    m_tcpSendAllButton->setEnabled(true);
    for (TcpPortSession *session : m_tcpSessions) {
        if (session && session->commandPage) session->commandPage->setEnabled(true);
    }
    m_tcpPortTable->setEnabled(true);
    updateTcpConnectionState();
}

/** 发送指定 TCP 会话连续测试的下一条命令。 */
void MainWindow::sendNextTcpPacket(TcpPortSession *session)
{
    if (!session || !session->testRunning || !session->worker) return;
    if (session->awaitingResponse) return;
    session->nextSendDeadlineMs = 0;
    const QList<CommandItem> commands = collectCommands(session->commandTable);
    if (commands.isEmpty()) {
        session->finishingAfterLimit = true;
        return;
    }
    if (session->statistics.pendingPacketCount() >= kMaxInFlightPackets) {
        session->awaitingResponse = true;
        return;
    }
    const int targetCount = ui->spinBoxSendCount->value();
    if (session->perCommandSendCount.size() != commands.size()) session->perCommandSendCount = QVector<int>(commands.size(), 0);
    int checked = 0;
    while (checked < commands.size() && targetCount > 0 && session->perCommandSendCount[session->currentCommandIndex] >= targetCount) {
        session->currentCommandIndex = (session->currentCommandIndex + 1) % commands.size();
        ++checked;
    }
    if (checked >= commands.size()) {
        session->finishingAfterLimit = true;
        checkTcpTimeouts();
        return;
    }

    const CommandItem &item = commands.at(session->currentCommandIndex);
    QByteArray payload = item.hexMode
        ? QByteArray::fromHex(item.text.simplified().remove(QRegularExpression(QStringLiteral("\\s+"))).toLatin1())
        : item.text.toUtf8();
    if (!payload.isEmpty()) {
        const QString format = item.hexMode ? QStringLiteral("HEX") : QStringLiteral("ASCII");
        ++session->perCommandSendCount[session->currentCommandIndex];
        session->awaitingResponse = true;
        session->worker->sendDataWithFormat(payload, item.hexMode ? QStringLiteral("HEX") : QStringLiteral("ASCII"));
        scheduleStatsRefresh();
    }
    session->currentCommandIndex = (session->currentCommandIndex + 1) % commands.size();
}

/** 使用绝对目标时间安排 TCP 连续测试的下一次发送。 */
void MainWindow::scheduleNextTcpPacket(TcpPortSession *session, int intervalMs)
{
    Q_UNUSED(intervalMs);
    if (!session || !session->testRunning || session->finishingAfterLimit) return;
    sendNextTcpPacket(session);
}

/** 扫描 TCP 会话超时状态，并在全部会话完成后停止测试。 */
void MainWindow::checkTcpTimeouts()
{
    bool allFinished = true;
    bool hasTest = false;
    for (TcpPortSession *session : m_tcpSessions) {
        if (!session->testRunning && !session->oneShotRunning) continue;
        if (session->testRunning) hasTest = true;
        if (session->testRunning && !(session->finishingAfterLimit && !session->statistics.hasPendingPackets())) allFinished = false;
    }
    if (hasTest && allFinished) stopTcpTest(false);
    scheduleStatsRefresh();
}

/** 响应顶部 Connect All 按钮并分派到当前模式。 */
void MainWindow::slotConnectClicked()
{
    if (isTcpMode()) {
        connectTcpPorts();
        return;
    }
    connectSerialPorts();
}

/** 响应顶部 Disconnect All 按钮并关闭当前模式连接。 */
void MainWindow::slotDisconnectClicked()
{
    if (isTcpMode()) {
        stopTcpTest(true);
        disconnectTcpPorts();
        return;
    }
    stopSerialTest(true);
    disconnectSerialPorts();
}

/** 响应 Start 按钮并启动当前模式的连续测试。 */
void MainWindow::slotStartClicked()
{
    if (isTcpMode()) {
        startTcpTest();
        return;
    }
    startSerialTest();
}

/** 响应 Stop 按钮并停止当前模式的连续测试。 */
void MainWindow::slotStopClicked()
{
    if (isTcpMode()) {
        stopTcpTest(true);
        return;
    }
    stopSerialTest(true);
}

/** 兼容旧单 worker 的下一条命令发送槽。 */
void MainWindow::slotCheckTimeouts()
{
    if (isTcpMode()) {
        checkTcpTimeouts();
        return;
    }
    checkSerialTimeouts();
}

/** 旧单 worker 连接成功回调。 */
void MainWindow::updateStatsView()
{
    for (TcpPortSession *session : m_tcpSessions) updateTcpSessionStats(session);
    for (SerialPortSession *session : m_serialSessions) updateSerialSessionStats(session);
}

void MainWindow::updateSerialSessionStats(SerialPortSession *session)
{
    if (!session || !session->commandPage) return;
    const auto setValue = [session](const QString &name, const QString &value) {
        if (auto *label = session->commandPage->findChild<QLabel *>(name)) label->setText(value);
    };
    const StatisticsSnapshot snap = session->statistics.countersSnapshot();
    setValue(QStringLiteral("serialTxCountValue"), QString::number(snap.totalSent));
    setValue(QStringLiteral("serialRxCountValue"), QString::number(snap.successReceived));
    setValue(QStringLiteral("serialTxBytesValue"), QString::number(snap.totalSentBytes));
    setValue(QStringLiteral("serialRxBytesValue"), QString::number(snap.totalReceivedBytes));
    setValue(QStringLiteral("serialSuccessRateValue"), QStringLiteral("%1%").arg(snap.successRate, 0, 'f', 2));
}

/** 弹出多行输入框并批量创建 TCP 端口会话。 */
void MainWindow::slotBatchAddTcpPorts()
{
    bool ok = false;
    const QString text = QInputDialog::getMultiLineText(
        this,
        QStringLiteral("Batch Add TCP Ports"),
        QStringLiteral("Ports (separated by comma, space, semicolon, or newline):"),
        QString(),
        &ok);
    if (!ok || text.trimmed().isEmpty()) {
        return;
    }

    const TcpPortParseResult result = parseTcpPortList(text);
    int addedCount = 0;
    int existingCount = 0;
    for (const quint16 port : result.ports) {
        if (m_tcpSessions.contains(port)) {
            ++existingCount;
            continue;
        }
        addTcpPort(port);
        ++addedCount;
    }

    if (result.ports.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("No valid TCP ports"),
                             QStringLiteral("No valid TCP ports were found in the input."));
        return;
    }

    QMessageBox::information(
        this,
        QStringLiteral("Batch Add Complete"),
        QStringLiteral("Added: %1\nSkipped existing: %2\nDuplicates: %3\nInvalid: %4")
            .arg(addedCount)
            .arg(existingCount)
            .arg(result.duplicateCount)
            .arg(result.invalidCount));
}

void MainWindow::updateTcpSessionStats(TcpPortSession *session)
{
    if (!session || !session->commandPage) return;
    const auto setValue = [session](const QString &name, const QString &value) {
        if (auto *label = session->commandPage->findChild<QLabel *>(name)) label->setText(value);
    };
    const StatisticsSnapshot snap = session->statistics.countersSnapshot();
    setValue(QStringLiteral("tcpTxCountValue"), QString::number(snap.totalSent));
    setValue(QStringLiteral("tcpRxCountValue"), QString::number(snap.successReceived));
    setValue(QStringLiteral("tcpTxBytesValue"), QString::number(snap.totalSentBytes));
    setValue(QStringLiteral("tcpRxBytesValue"), QString::number(snap.totalReceivedBytes));
    setValue(QStringLiteral("tcpSuccessRateValue"), QStringLiteral("%1%").arg(snap.successRate, 0, 'f', 2));
}

/** 向旧版全局统计网格追加 P50/P90/P95/P99 行。 */
void MainWindow::scheduleStatsRefresh()
{
    // 统计结果仅在线程停止时汇总，通信过程中不实时计算分位数。
}
/** 将结构化日志追加到屏幕，并限制超大 payload 的显示长度。 */
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
        tag = QStringLiteral("ERROR");
        color = QStringLiteral("#ff3333");
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

    // 先保存未截断的纯文本事件，报告文件因此可以还原日志区域的完整内容。
    QString portTag = QStringLiteral("global");
    const QRegularExpression portExpression(QStringLiteral("^\\[([^\\]]+)\\]"));
    const QRegularExpressionMatch portMatch = portExpression.match(text);
    if (portMatch.hasMatch()) portTag = portMatch.captured(1);
    const QString line = QStringLiteral("[%1] %2%3 %4").arg(tag, timestamp, elapsed, text);
    if (portTag != QStringLiteral("global")) appendLogFile(portTag, line);
    appendPortLog(portTag, line, color);

}

/** 将一条事件显示到对应端口的独立日志区域，并限制内存中的行数。 */
void MainWindow::appendPortLog(const QString &portTag, const QString &line, const QString &color)
{
    QTextEdit *edit = nullptr;
    if (portTag.startsWith(QStringLiteral("Port "))) {
        bool ok = false;
        const quint16 port = portTag.mid(5).toUShort(&ok);
        if (ok && m_tcpSessions.contains(port)) edit = m_tcpSessions.value(port)->logEdit;
    } else if (m_serialSessions.contains(portTag)) {
        edit = m_serialSessions.value(portTag)->logEdit;
    }
    if (!edit) return;
    constexpr int kMaxUiLogChars = 2048;
    const QString preview = line.size() > kMaxUiLogChars
        ? line.left(kMaxUiLogChars) + QStringLiteral(" ... [truncated]")
        : line;
    edit->append(QStringLiteral("<span style='color:%1;'>%2</span>").arg(color, preview.toHtmlEscaped()));
}

void MainWindow::logTcpSessionParameters(const TcpPortSession *session, const QString &testType)
{
    if (!session) return;
    const QString prefix = QStringLiteral("[Port %1]").arg(session->port);
    const auto write = [this, &prefix](const QString &value) {
        appendLog(LogLevel::Info, prefix + QLatin1Char(' ') + value);
    };
    const auto escapeValue = [](QString value) {
        value.replace(QStringLiteral("\r"), QStringLiteral("\\r"));
        value.replace(QStringLiteral("\n"), QStringLiteral("\\n"));
        return value;
    };

    write(QStringLiteral("=== Parameters ==="));
    write(QStringLiteral("Test Type: %1").arg(testType));
    write(QStringLiteral("Mode: %1").arg(ui->comboBoxMode->currentText()));
    write(QStringLiteral("Target IP: %1").arg(ui->lineEditIp->text().trimmed()));
    write(QStringLiteral("Session Port: %1").arg(session->port));
    write(QStringLiteral("Port SpinBox: %1").arg(ui->spinBoxPort->value()));
    write(QStringLiteral("Interval(ms): %1").arg(ui->spinBoxInterval->value()));
    write(QStringLiteral("Timeout(ms): %1").arg(ui->spinBoxTimeout->value()));
    write(QStringLiteral("Send Count: %1").arg(ui->spinBoxSendCount->value() == 0
                                                    ? QStringLiteral("unlimited")
                                                    : QString::number(ui->spinBoxSendCount->value())));

    const QList<CommandItem> commands = collectCommands(session->commandTable);
    write(QStringLiteral("Command Count: %1").arg(commands.size()));
    int number = 1;
    for (const CommandItem &item : commands) {
        write(QStringLiteral("Command %1 | Format: %2 | Valid: %3 | Data: %4")
                  .arg(number++)
                  .arg(item.hexMode ? QStringLiteral("HEX") : QStringLiteral("ASCII"))
                  .arg(item.valid ? QStringLiteral("true") : QStringLiteral("false"))
                  .arg(escapeValue(item.text)));
    }
    write(QStringLiteral("=== Parameters End ==="));
}

void MainWindow::logSerialSessionParameters(const SerialPortSession *session, const QString &testType)
{
    if (!session) return;
    const QString prefix = QStringLiteral("[%1]").arg(session->portName);
    const auto write = [this, &prefix](const QString &value) {
        appendLog(LogLevel::Info, prefix + QLatin1Char(' ') + value);
    };
    const auto escapeValue = [](QString value) {
        value.replace(QStringLiteral("\r"), QStringLiteral("\\r"));
        value.replace(QStringLiteral("\n"), QStringLiteral("\\n"));
        return value;
    };

    const SerialSettings settings = serialSettings(session);
    write(QStringLiteral("=== Parameters ==="));
    write(QStringLiteral("Test Type: %1").arg(testType));
    write(QStringLiteral("Mode: %1").arg(ui->comboBoxMode->currentText()));
    write(QStringLiteral("Serial Port: %1").arg(settings.portName));
    write(QStringLiteral("Baud: %1").arg(settings.baudRate));
    write(QStringLiteral("Data Bits: %1").arg(session->dataBitsCombo ? session->dataBitsCombo->currentText() : QStringLiteral("8")));
    write(QStringLiteral("Stop Bits: %1").arg(session->stopBitsCombo ? session->stopBitsCombo->currentText() : QStringLiteral("1")));
    write(QStringLiteral("Parity: %1").arg(session->parityCombo ? session->parityCombo->currentText() : QStringLiteral("None")));
    write(QStringLiteral("Interval(ms): %1").arg(ui->spinBoxInterval->value()));
    write(QStringLiteral("Timeout(ms): %1").arg(ui->spinBoxTimeout->value()));
    write(QStringLiteral("Send Count: %1").arg(ui->spinBoxSendCount->value() == 0
                                                    ? QStringLiteral("unlimited")
                                                    : QString::number(ui->spinBoxSendCount->value())));

    const QList<CommandItem> commands = collectSerialCommands(session);
    write(QStringLiteral("Command Count: %1").arg(commands.size()));
    int number = 1;
    for (const CommandItem &item : commands) {
        write(QStringLiteral("Command %1 | Format: %2 | Valid: %3 | Data: %4")
                  .arg(number++)
                  .arg(item.hexMode ? QStringLiteral("HEX") : QStringLiteral("ASCII"))
                  .arg(item.valid ? QStringLiteral("true") : QStringLiteral("false"))
                  .arg(escapeValue(item.text)));
    }
    write(QStringLiteral("=== Parameters End ==="));
}

/** 停止旧单 worker 测试、标记未完成包并保存报告。 */
void MainWindow::appendLogFile(const QString &portTag, const QString &line)
{
    QDir logDir(QDir::current().filePath(QStringLiteral("Log")));
    if (!logDir.exists() && !logDir.mkpath(QStringLiteral("."))) return;

    QString safeName = portTag;
    if (safeName.startsWith(QStringLiteral("Port "))) safeName = safeName.mid(5);
    safeName.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_-]+")), QStringLiteral("_"));
    if (safeName.isEmpty()) return;

    QString filePath = m_logFilePaths.value(portTag);
    if (filePath.isEmpty()) {
        const QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss_zzz"));
        filePath = logDir.filePath(QStringLiteral("%1_%2.log").arg(safeName, timestamp));
        m_logFilePaths.insert(portTag, filePath);
    }

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) return;
    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    stream << line << QLatin1Char('\n');
    stream.flush();
    file.close();
}

QString MainWindow::payloadToDisplay(const QByteArray &payload, const QString &format, bool truncate)
{
    if (payload.isEmpty()) {
        return QStringLiteral("(empty)");
    }

    constexpr int kPreviewBytes = 256;
    if (format.trimmed().compare(QStringLiteral("ASCII"), Qt::CaseInsensitive) == 0) {
        const auto decodeAscii = [](const QByteArray &bytes) {
            QString text;
            text.reserve(bytes.size());
            for (const unsigned char byte : bytes) {
                if (byte == '\r') {
                    text += QStringLiteral("\\r");
                } else if (byte == '\n') {
                    text += QStringLiteral("\\n");
                } else if (byte == '\t') {
                    text += QStringLiteral("\\t");
                } else if (byte >= 0x20 && byte <= 0x7E) {
                    text += QChar(byte);
                } else {
                    // 非 ASCII/不可打印字节使用转义形式，避免 UTF-8 替换字符乱码。
                    text += QStringLiteral("\\x%1").arg(byte, 2, 16, QLatin1Char('0'));
                }
            }
            return text;
        };

        if (!truncate || payload.size() <= kPreviewBytes * 2) {
            return decodeAscii(payload);
        }

        return decodeAscii(payload.left(kPreviewBytes)) +
               QStringLiteral(" ... [truncated %1 bytes] ... ").arg(payload.size() - kPreviewBytes * 2) +
               decodeAscii(payload.right(kPreviewBytes));
    }

    if (!truncate || payload.size() <= kPreviewBytes * 2) {
        return QString::fromLatin1(payload.toHex(' ').toUpper());
    }

    const QByteArray prefix = payload.left(kPreviewBytes).toHex(' ').toUpper();
    const QByteArray suffix = payload.right(kPreviewBytes).toHex(' ').toUpper();
    return QString::fromLatin1(prefix) +
           QStringLiteral(" ... [truncated %1 bytes] ... ").arg(payload.size() - kPreviewBytes * 2) +
           QString::fromLatin1(suffix);
}

/** 将日志中的 HTML 特殊字符转换为安全文本。 */
END_NAMESPACE_CIQTEK
