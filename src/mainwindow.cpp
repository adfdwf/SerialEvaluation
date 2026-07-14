#include "mainwindow.h"
#include "serialclientworker.h"
#include "tcpclientworker.h"
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
#include <QHostAddress>
#include <QScrollBar>
#include <QSerialPortInfo>
#include <QTableWidget>
#include <QTabWidget>
#include <QRadioButton>
#include <QTextStream>
#include <QVBoxLayout>

#include <algorithm>

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
    stopTcpTest(true);
    stopTest(true);
    destroyWorker();
    destroyTcpWorkers();
    qDeleteAll(m_tcpSessions);
    m_tcpSessions.clear();
    delete ui;
}

void MainWindow::setupUiLogic()
{
    m_sendTimer = new QTimer(this);
    m_sendTimer->setSingleShot(true);
    m_sendTimer->setTimerType(Qt::PreciseTimer);
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
    setupTcpPortUi();
    setupStatsLabels();

    ui->pushButtonStop->setEnabled(false);

    QObject::connect(ui->comboBoxMode, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::slotModeChanged);
    QObject::connect(ui->pushButtonRefreshPorts, &QPushButton::clicked, this, &MainWindow::slotRefreshSerialPorts);
    QObject::connect(ui->pushButtonConnect, &QPushButton::clicked, this, &MainWindow::slotConnectClicked);
    QObject::connect(ui->pushButtonDisconnect, &QPushButton::clicked, this, &MainWindow::slotDisconnectClicked);
    QObject::connect(ui->pushButtonStart, &QPushButton::clicked, this, &MainWindow::slotStartClicked);
    QObject::connect(ui->pushButtonStop, &QPushButton::clicked, this, &MainWindow::slotStopClicked);
    QObject::connect(ui->pushButtonAddCommand, &QPushButton::clicked, this, &MainWindow::slotAddCommandRow);
    QObject::connect(ui->pushButtonRemoveCommand, &QPushButton::clicked, this, &MainWindow::slotRemoveCommandRow);
    QObject::connect(m_tcpAddPortButton, &QPushButton::clicked, this, &MainWindow::slotAddTcpPort);
    QObject::connect(m_tcpRemovePortButton, &QPushButton::clicked, this, &MainWindow::slotRemoveTcpPort);
    QObject::connect(m_tcpSendAllButton, &QPushButton::clicked, this, &MainWindow::slotSendAllTcpPorts);
    QObject::connect(m_sendTimer, &QTimer::timeout, this, &MainWindow::slotSendNextPacket);
    QObject::connect(m_timeoutTimer, &QTimer::timeout, this, &MainWindow::slotCheckTimeouts);
}

void MainWindow::setupCommandTable()
{
    QStringList headers;
    headers << QStringLiteral("#")
            << QStringLiteral("Command")
            << QStringLiteral("Mode");

    ui->tableCommands->setColumnCount(3);
    ui->tableCommands->setHorizontalHeaderLabels(headers);
    ui->tableCommands->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    ui->tableCommands->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    ui->tableCommands->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
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
    // 4 MiB ASCII needs 4 MiB characters; HEX text may use two digits and a
    // separator per byte, so allow generous text overhead for a 4 MiB payload.
    commandEdit->setMaxLength(16 * 1024 * 1024);
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
            valid = validateHexPayload(edit->text());
        }

        CommandItem item;
        item.text = edit->text();
        item.hexMode = hexMode;
        item.valid = valid;
        commands.append(item);
    }
    return commands;
}

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

void MainWindow::applyIndustrialTheme()
{
    setWindowTitle(QStringLiteral("CommBench Pro - Industrial Communication Benchmark"));
    resize(1280, 780);
}

void MainWindow::slotModeChanged(int index)
{
    const bool serialMode = index == 1;
    stopTcpTest(true);
    stopTest(true);
    destroyTcpWorkers();
    ui->widgetTcpConfig->setVisible(!serialMode);
    ui->widgetSerialConfig->setVisible(serialMode);
    ui->groupBoxCommands->setVisible(serialMode);
    if (m_tcpPortTable) {
        m_tcpPortTable->parentWidget()->setVisible(!serialMode);
    }
    ui->spinBoxPort->setVisible(serialMode);
    if (auto *portLabel = ui->groupBoxConfig->findChild<QLabel *>(QStringLiteral("labelPort"))) {
        portLabel->setVisible(serialMode);
    }
    destroyWorker();
    if (serialMode) {
        ui->pushButtonConnect->setText(QStringLiteral("Connect"));
        ui->pushButtonDisconnect->setText(QStringLiteral("Disconnect"));
        setConnectionState(ConnectionState::Disconnected);
    } else {
        ui->pushButtonConnect->setText(QStringLiteral("Connect All"));
        ui->pushButtonDisconnect->setText(QStringLiteral("Disconnect All"));
        updateTcpConnectionState();
    }
}

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
    m_tcpRemovePortButton = new QPushButton(QStringLiteral("- Remove Port"), portBox);
    m_tcpSendAllButton = new QPushButton(QStringLiteral("Send All Ports"), portBox);
    toolbar->addWidget(m_tcpAddPortButton);
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

void MainWindow::removeTcpPort(quint16 port)
{
    TcpPortSession *session = m_tcpSessions.take(port);
    if (!session) {
        return;
    }
    if (session->worker) {
        QObject::disconnect(session->worker, nullptr, this, nullptr);
        if (session->thread && session->thread->isRunning()) {
            QMetaObject::invokeMethod(session->worker, "disconnect", Qt::BlockingQueuedConnection);
        }
    }
    if (session->thread) {
        session->thread->quit();
        session->thread->wait();
        delete session->thread;
        session->thread = nullptr;
    }
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

void MainWindow::slotRemoveTcpPort()
{
    const int row = m_tcpPortTable->currentRow();
    if (row < 0 || !m_tcpPortTable->item(row, 0)) {
        return;
    }
    removeTcpPort(static_cast<quint16>(m_tcpPortTable->item(row, 0)->text().toUShort()));
}

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

bool MainWindow::isTcpMode() const
{
    return ui->comboBoxMode->currentIndex() == 0;
}

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
        auto *thread = new QThread(this);
        auto *worker = new TcpClientWorker(address.toString(), session->port);
        session->thread = thread;
        session->worker = worker;
        session->connecting = true;
        worker->moveToThread(thread);

        QObject::connect(thread, &QThread::finished, worker, &QObject::deleteLater);
        QObject::connect(worker, &TcpClientWorker::signalConnected, this, [this, session]() {
            if (m_tcpSessions.value(session->port) != session) return;
            session->connected = true;
            session->connecting = false;
            updateTcpPortRow(session, QStringLiteral("Connected"));
            appendLog(LogLevel::Info, QStringLiteral("[Port %1] Connected").arg(session->port));
            updateTcpConnectionState();
        });
        QObject::connect(worker, &TcpClientWorker::signalDisconnected, this, [this, session]() {
            if (m_tcpSessions.value(session->port) != session) return;
            session->connected = false;
            session->connecting = false;
            updateTcpPortRow(session, QStringLiteral("Disconnected"));
            appendLog(LogLevel::Info, QStringLiteral("[Port %1] Disconnected").arg(session->port));
            if (session->testRunning) stopTcpTest(true);
            updateTcpConnectionState();
        });
        QObject::connect(worker, &TcpClientWorker::signalDataReceived, this, [this, session](const QByteArray &data) {
            if (m_tcpSessions.value(session->port) != session) return;
            PacketInfo packet;
            if (session->statistics.recordReceive(data, &packet)) {
                appendLog(LogLevel::Rx, QStringLiteral("[Port %1] #%2 %3").arg(session->port).arg(packet.id).arg(payloadToDisplay(data, packet.txFormat)), packet.elapsedMs);
            } else {
                appendLog(LogLevel::Rx, QStringLiteral("[Port %1] Unmatched response %2").arg(session->port).arg(payloadToDisplay(data)));
            }
            scheduleStatsRefresh();
        });
        QObject::connect(worker, &TcpClientWorker::signalErrorOccurred, this, [this, session](const QString &message) {
            if (m_tcpSessions.value(session->port) == session) {
                appendLog(LogLevel::Error, QStringLiteral("[Port %1] %2").arg(session->port).arg(message));
            }
        });
        thread->start();
        QMetaObject::invokeMethod(worker, "connect", Qt::QueuedConnection);
        updateTcpPortRow(session, QStringLiteral("Connecting"));
    }
    ui->pushButtonConnect->setEnabled(false);
    ui->pushButtonDisconnect->setEnabled(true);
    appendLog(LogLevel::Info, QStringLiteral("Connecting to %1 TCP ports at %2").arg(m_tcpSessions.size()).arg(address.toString()));
}

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

void MainWindow::destroyTcpWorkers()
{
    for (TcpPortSession *session : m_tcpSessions) {
        if (session->sendTimer) session->sendTimer->stop();
        session->oneShotCommands.clear();
        session->oneShotRunning = false;
        if (!session->thread) {
            session->worker = nullptr;
            continue;
        }
        if (session->worker) {
            QObject::disconnect(session->worker, nullptr, this, nullptr);
            if (session->thread->isRunning()) {
                QMetaObject::invokeMethod(session->worker, "disconnect", Qt::BlockingQueuedConnection);
            }
        }
        session->thread->quit();
        session->thread->wait();
        // TcpClientWorker is connected to QThread::finished -> deleteLater().
        // It owns a QTcpSocket with the worker-thread affinity, so never delete
        // it from the GUI thread after the worker thread has stopped.
        delete session->thread;
        session->worker = nullptr;
        session->thread = nullptr;
        session->connected = false;
        session->connecting = false;
    }
}

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

void MainWindow::slotSendSelectedTcpPort()
{
    if (!m_tcpPortTable) return;
    const int row = m_tcpPortTable->currentRow();
    if (row >= 0 && m_tcpPortTable->item(row, 0)) {
        sendAllTcpPort(m_tcpSessions.value(static_cast<quint16>(m_tcpPortTable->item(row, 0)->text().toUShort())));
    }
}

void MainWindow::slotSendAllTcpPorts()
{
    for (TcpPortSession *session : m_tcpSessions) {
        sendAllTcpPort(session);
    }
}

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
    session->oneShotCommands.clear();
    for (const CommandItem &item : commands) {
        session->oneShotCommands.enqueue(item);
    }
    session->oneShotRunning = true;
    session->nextOneShotDeadlineMs = 0;
    session->sendClock.start();
    sendNextOneShotTcpPacket(session);
}

void MainWindow::sendNextOneShotTcpPacket(TcpPortSession *session)
{
    if (!session || !session->oneShotRunning || !session->worker || !session->connected) {
        return;
    }
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
        const PacketInfo packet = session->statistics.recordSend(payload, format);
        appendLog(LogLevel::Tx, QStringLiteral("[Port %1] #%2 %3").arg(session->port).arg(packet.id).arg(item.text));
        QMetaObject::invokeMethod(session->worker, "sendData", Qt::QueuedConnection, Q_ARG(QByteArray, payload));
        scheduleStatsRefresh();
    }
    if (session->oneShotCommands.isEmpty()) {
        session->oneShotRunning = false;
        session->nextOneShotDeadlineMs = 0;
    } else {
        scheduleNextOneShotTcpPacket(session, ui->spinBoxInterval->value());
    }
}

void MainWindow::scheduleNextOneShotTcpPacket(TcpPortSession *session, int intervalMs)
{
    if (!session || !session->oneShotRunning) return;
    const qint64 now = session->sendClock.elapsed();
    if (session->nextOneShotDeadlineMs == 0) session->nextOneShotDeadlineMs = now + intervalMs;
    else session->nextOneShotDeadlineMs += intervalMs;
    if (session->nextOneShotDeadlineMs <= now) session->nextOneShotDeadlineMs = now + intervalMs;
    if (!session->sendTimer) {
        session->sendTimer = new QTimer(this);
        session->sendTimer->setSingleShot(true);
        session->sendTimer->setTimerType(Qt::PreciseTimer);
        QObject::connect(session->sendTimer, &QTimer::timeout, this, [this, session]() {
            if (session->oneShotRunning) sendNextOneShotTcpPacket(session);
            else sendNextTcpPacket(session);
        });
    }
    session->sendTimer->start(static_cast<int>(qMax<qint64>(1, session->nextOneShotDeadlineMs - session->sendClock.elapsed())));
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

    ui->textEditLog->clear();
    for (TcpPortSession *session : m_tcpSessions) {
        const QList<CommandItem> commands = collectCommands(session->commandTable);
        session->statistics.reset();
        session->oneShotCommands.clear();
        session->oneShotRunning = false;
        session->nextOneShotDeadlineMs = 0;
        session->currentCommandIndex = 0;
        session->perCommandSendCount = QVector<int>(commands.size(), 0);
        session->testRunning = true;
        session->finishingAfterLimit = false;
        session->sendClock.start();
        session->nextSendDeadlineMs = 0;
    }
    m_testRunning = true;
    m_finishingAfterLimit = false;
    ui->pushButtonStart->setEnabled(false);
    ui->pushButtonStop->setEnabled(true);
    ui->pushButtonConnect->setEnabled(false);
    ui->comboBoxMode->setEnabled(false);
    m_tcpAddPortButton->setEnabled(false);
    m_tcpRemovePortButton->setEnabled(false);
    m_tcpSendAllButton->setEnabled(false);
    m_tcpCommandTabs->setEnabled(false);
    m_tcpPortTable->setEnabled(false);
    m_timeoutTimer->start();
    appendLog(LogLevel::Info, QStringLiteral("Multi-port test started: %1 ports, interval %2 ms, timeout %3 ms")
                                 .arg(m_tcpSessions.size()).arg(ui->spinBoxInterval->value()).arg(ui->spinBoxTimeout->value()));
    for (TcpPortSession *session : m_tcpSessions) sendNextTcpPacket(session);
}

void MainWindow::stopTcpTest(bool manualStop)
{
    bool anyRunning = m_testRunning;
    for (TcpPortSession *session : m_tcpSessions) anyRunning = anyRunning || session->testRunning;
    if (!anyRunning) return;

    for (TcpPortSession *session : m_tcpSessions) {
        if (session->sendTimer) session->sendTimer->stop();
        session->testRunning = false;
        session->finishingAfterLimit = false;
        session->statistics.markAllPendingLost();
    }
    m_testRunning = false;
    m_finishingAfterLimit = false;
    m_timeoutTimer->stop();
    if (manualStop) appendLog(LogLevel::Info, QStringLiteral("Multi-port test stopped manually"));
    finalizeTcpReport();
    scheduleStatsRefresh();
    ui->pushButtonStart->setEnabled(true);
    ui->pushButtonStop->setEnabled(false);
    ui->comboBoxMode->setEnabled(true);
    m_tcpAddPortButton->setEnabled(true);
    m_tcpRemovePortButton->setEnabled(true);
    m_tcpSendAllButton->setEnabled(true);
    m_tcpCommandTabs->setEnabled(true);
    m_tcpPortTable->setEnabled(true);
    updateTcpConnectionState();
}

void MainWindow::sendNextTcpPacket(TcpPortSession *session)
{
    if (!session || !session->testRunning || !session->worker) return;
    const QList<CommandItem> commands = collectCommands(session->commandTable);
    if (commands.isEmpty()) {
        session->finishingAfterLimit = true;
        return;
    }
    const int intervalMs = ui->spinBoxInterval->value();
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
        const PacketInfo packet = session->statistics.recordSend(payload, format);
        ++session->perCommandSendCount[session->currentCommandIndex];
        appendLog(LogLevel::Tx, QStringLiteral("[Port %1] #%2 %3").arg(session->port).arg(packet.id).arg(item.text));
        QMetaObject::invokeMethod(session->worker, "sendData", Qt::QueuedConnection, Q_ARG(QByteArray, payload));
        scheduleStatsRefresh();
    }
    session->currentCommandIndex = (session->currentCommandIndex + 1) % commands.size();
    scheduleNextTcpPacket(session, intervalMs);
}

void MainWindow::scheduleNextTcpPacket(TcpPortSession *session, int intervalMs)
{
    if (!session || !session->testRunning || session->finishingAfterLimit) return;
    const qint64 now = session->sendClock.elapsed();
    if (session->nextSendDeadlineMs == 0) session->nextSendDeadlineMs = now + intervalMs;
    else session->nextSendDeadlineMs += intervalMs;
    if (session->nextSendDeadlineMs <= now) session->nextSendDeadlineMs = now + intervalMs;
    if (!session->sendTimer) {
        session->sendTimer = new QTimer(this);
        session->sendTimer->setSingleShot(true);
        session->sendTimer->setTimerType(Qt::PreciseTimer);
        QObject::connect(session->sendTimer, &QTimer::timeout, this, [this, session]() {
            if (session->oneShotRunning) sendNextOneShotTcpPacket(session);
            else sendNextTcpPacket(session);
        });
    }
    session->sendTimer->start(static_cast<int>(qMax<qint64>(1, session->nextSendDeadlineMs - session->sendClock.elapsed())));
}

void MainWindow::checkTcpTimeouts()
{
    if (!m_testRunning) return;
    bool allFinished = true;
    for (TcpPortSession *session : m_tcpSessions) {
        const QVector<PacketInfo> timedOut = session->statistics.markTimeouts(ui->spinBoxTimeout->value());
        for (const PacketInfo &packet : timedOut) appendLog(LogLevel::Error, QStringLiteral("[Port %1] #%2 timeout/lost").arg(session->port).arg(packet.id), packet.elapsedMs);
        if (!(session->finishingAfterLimit && !session->statistics.hasPendingPackets())) allFinished = false;
    }
    if (allFinished) stopTcpTest(false);
    scheduleStatsRefresh();
}

void MainWindow::slotConnectClicked()
{
    if (isTcpMode()) {
        connectTcpPorts();
        return;
    }

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
    if (isTcpMode()) {
        stopTcpTest(true);
        disconnectTcpPorts();
        return;
    }

    stopTest(true);
    destroyWorker();
    setConnectionState(ConnectionState::Disconnected);
    appendLog(LogLevel::Info, QStringLiteral("Connection closed"));
}

void MainWindow::slotStartClicked()
{
    if (isTcpMode()) {
        startTcpTest();
        return;
    }

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
    m_perCommandSendCount = QVector<int>(commands.size(), 0);

    ui->textEditLog->clear();
    m_statistics.reset();
    m_testRunning = true;
    m_finishingAfterLimit = false;
    ui->pushButtonStart->setEnabled(false);
    ui->pushButtonStop->setEnabled(true);
    ui->pushButtonConnect->setEnabled(false);
    ui->comboBoxMode->setEnabled(false);
    m_timeoutTimer->start();

    appendLog(LogLevel::Info, QStringLiteral("Test started: %1, interval %2 ms, count %3, %4 commands")
                                 .arg(currentModeDescription())
                                 .arg(ui->spinBoxInterval->value())
                                 .arg(ui->spinBoxSendCount->value() == 0 ? QStringLiteral("unlimited") : QString::number(ui->spinBoxSendCount->value()))
                                 .arg(commands.size()));

    m_sendClock.start();
    m_nextSendDeadlineMs = 0;
    slotSendNextPacket();
}

void MainWindow::slotStopClicked()
{
    if (isTcpMode()) {
        stopTcpTest(true);
        return;
    }
    stopTest(true);
}

void MainWindow::slotSendNextPacket()
{
    if (!m_testRunning || !m_workerObject) {
        return;
    }

    // 每次发送前重新读取 Interval，确保用户动态调整实时生效。
    const int intervalMs = ui->spinBoxInterval->value();

    const int targetPerCommand = ui->spinBoxSendCount->value();

    const QList<CommandItem> commands = collectCommands();
    if (commands.isEmpty()) {
        appendLog(LogLevel::Error, QStringLiteral("No commands, test stopped"));
        stopTest(true);
        return;
    }

    // 确保 m_perCommandSendCount 与当前命令表同步
    if (m_perCommandSendCount.size() != commands.size()) {
        m_perCommandSendCount = QVector<int>(commands.size(), 0);
        if (m_currentCommandIndex >= commands.size()) {
            m_currentCommandIndex = 0;
        }
    }

    // 查找下一条未达到发送次数上限的命令
    int checked = 0;
    while (checked < commands.size()) {
        if (m_currentCommandIndex >= commands.size()) {
            m_currentCommandIndex = 0;
        }

        if (targetPerCommand > 0 && m_perCommandSendCount[m_currentCommandIndex] >= targetPerCommand) {
            m_currentCommandIndex = (m_currentCommandIndex + 1) % commands.size();
            ++checked;
            continue;
        }

        break;
    }

    if (checked >= commands.size()) {
        // 所有命令都已达到发送次数上限
        m_sendTimer->stop();
        m_finishingAfterLimit = true;
        appendLog(LogLevel::Info, QStringLiteral("All commands reached send limit, waiting for remaining responses or timeout"));
        checkFinishAfterLimit();
        return;
    }

    const CommandItem &item = commands.at(m_currentCommandIndex);

    QByteArray payload;
    if (item.hexMode) {
        const QString hexStr = item.text.simplified().remove(QRegularExpression(QStringLiteral("\\s+")));
        payload = QByteArray::fromHex(hexStr.toLatin1());
    } else {
        payload = item.text.toUtf8();
    }

    if (payload.isEmpty()) {
        // payload 为空时跳过本条命令，继续下一条，不中断发送调度。
        m_currentCommandIndex = (m_currentCommandIndex + 1) % commands.size();
        scheduleNextSend(intervalMs);
        return;
    }

    const QString dataFormat = item.hexMode ? QStringLiteral("HEX") : QStringLiteral("ASCII");
    const PacketInfo packet = m_statistics.recordSend(payload, dataFormat);
    ++m_perCommandSendCount[m_currentCommandIndex];
    appendLog(LogLevel::Tx, QStringLiteral("#%1 %2").arg(packet.id).arg(item.text));
    QMetaObject::invokeMethod(m_workerObject, "sendData", Qt::QueuedConnection, Q_ARG(QByteArray, payload));
    scheduleStatsRefresh();

    // 移到下一条命令，下次 timer 触发时发送
    m_currentCommandIndex = (m_currentCommandIndex + 1) % commands.size();
    scheduleNextSend(intervalMs);
}

void MainWindow::scheduleNextSend(int intervalMs)
{
    if (!m_testRunning || m_finishingAfterLimit) {
        return;
    }

    const qint64 nowMs = m_sendClock.elapsed();
    if (m_nextSendDeadlineMs == 0) {
        m_nextSendDeadlineMs = nowMs + intervalMs;
    } else {
        // 基于上一次的目标时刻推进，而不是从当前时刻重新计时，避免调度延迟累计。
        m_nextSendDeadlineMs += intervalMs;
        if (m_nextSendDeadlineMs <= nowMs) {
            // 主线程阻塞超过一个周期时跳过已错过的时刻，避免恢复后突发连续发送。
            const qint64 missedIntervals = (nowMs - m_nextSendDeadlineMs) / intervalMs + 1;
            m_nextSendDeadlineMs += missedIntervals * intervalMs;
        }
    }

    const qint64 remainingMs = qMax<qint64>(1, m_nextSendDeadlineMs - m_sendClock.elapsed());
    m_sendTimer->start(static_cast<int>(remainingMs));
}

void MainWindow::slotCheckTimeouts()
{
    if (isTcpMode()) {
        checkTcpTimeouts();
        return;
    }

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
        appendLog(LogLevel::Rx, QStringLiteral("#%1 %2").arg(packet.id).arg(payloadToDisplay(data, packet.txFormat)), packet.elapsedMs);
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
    if (isTcpMode()) {
        StatisticsSnapshot snap;
        QVector<qint64> elapsedValues;
        qint64 sum = 0;
        for (const TcpPortSession *session : m_tcpSessions) {
            for (const PacketInfo &packet : session->statistics.packets()) {
                ++snap.totalSent;
                if (packet.status == PacketInfo::Status::Success) {
                    ++snap.successReceived;
                    elapsedValues.push_back(packet.elapsedMs);
                    sum += packet.elapsedMs;
                    snap.minElapsedMs = snap.successReceived == 1 ? packet.elapsedMs : qMin(snap.minElapsedMs, packet.elapsedMs);
                    snap.maxElapsedMs = qMax(snap.maxElapsedMs, packet.elapsedMs);
                } else if (packet.status == PacketInfo::Status::Timeout) {
                    ++snap.lostPackets;
                }
            }
        }
        if (snap.totalSent > 0) snap.successRate = static_cast<double>(snap.successReceived) * 100.0 / snap.totalSent;
        if (!elapsedValues.isEmpty()) {
            snap.averageElapsedMs = static_cast<double>(sum) / elapsedValues.size();
            std::sort(elapsedValues.begin(), elapsedValues.end());
            const auto percentile = [&elapsedValues](double p) {
                const double rank = p * (elapsedValues.size() - 1);
                const int low = static_cast<int>(rank);
                const int high = qMin(low + 1, elapsedValues.size() - 1);
                const double fraction = rank - low;
                return elapsedValues[low] * (1.0 - fraction) + elapsedValues[high] * fraction;
            };
            snap.p50Ms = percentile(0.50);
            snap.p90Ms = percentile(0.90);
            snap.p95Ms = percentile(0.95);
            snap.p99Ms = percentile(0.99);
        }
        ui->labelTotalSentValue->setText(QString::number(snap.totalSent));
        ui->labelSuccessValue->setText(QString::number(snap.successReceived));
        ui->labelLostValue->setText(QString::number(snap.lostPackets));
        ui->labelRateValue->setText(QStringLiteral("%1%").arg(snap.successRate, 0, 'f', 2));
        ui->labelAverageValue->setText(QStringLiteral("%1 ms").arg(snap.averageElapsedMs, 0, 'f', 3));
        ui->labelMaxValue->setText(QStringLiteral("%1 ms").arg(snap.maxElapsedMs));
        ui->labelMinValue->setText(QStringLiteral("%1 ms").arg(snap.minElapsedMs));
        auto setTcpLabel = [&](const QString &name, double value) {
            if (auto *label = ui->groupBoxStats->findChild<QLabel *>(name)) label->setText(QStringLiteral("%1 ms").arg(value, 0, 'f', 3));
        };
        setTcpLabel(QStringLiteral("labelP50Value"), snap.p50Ms);
        setTcpLabel(QStringLiteral("labelP90Value"), snap.p90Ms);
        setTcpLabel(QStringLiteral("labelP95Value"), snap.p95Ms);
        setTcpLabel(QStringLiteral("labelP99Value"), snap.p99Ms);
        return;
    }

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

    setLabel(QStringLiteral("labelP50Value"), QStringLiteral("%1 ms").arg(snap.p50Ms, 0, 'f', 3));
    setLabel(QStringLiteral("labelP90Value"), QStringLiteral("%1 ms").arg(snap.p90Ms, 0, 'f', 3));
    setLabel(QStringLiteral("labelP95Value"), QStringLiteral("%1 ms").arg(snap.p95Ms, 0, 'f', 3));
    setLabel(QStringLiteral("labelP99Value"), QStringLiteral("%1 ms").arg(snap.p99Ms, 0, 'f', 3));
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

    // Never render megabytes of command data in QTextEdit: doing so blocks the
    // GUI event loop and directly distorts the send interval. Full payloads are
    // still written to the report file; the on-screen log is a bounded preview.
    QString displayText = text;
    constexpr int kMaximumDisplayCharacters = 4096;
    if (displayText.size() > kMaximumDisplayCharacters) {
        const int previewSize = kMaximumDisplayCharacters / 2;
        displayText = displayText.left(previewSize) +
                      QStringLiteral(" ... [truncated %1 chars] ... ").arg(text.size() - kMaximumDisplayCharacters) +
                      displayText.right(previewSize);
    }

    QString html = QStringLiteral("<span style='color:%1;'><b>[%2]</b> %3%4 %5</span>")
                       .arg(color, tag, timestamp, elapsed, htmlEscape(displayText));

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
}

void MainWindow::finalizeTestReport()
{
    if (isTcpMode()) {
        finalizeTcpReport();
        return;
    }

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
    out << QStringLiteral("Config: %1\n").arg(currentConfigDescription());
    out << QStringLiteral("Interval: %1 ms\n").arg(ui->spinBoxInterval->value());
    out << QStringLiteral("Timeout: %1 ms\n").arg(ui->spinBoxTimeout->value());
    out << QStringLiteral("Send Count: %1\n\n")
               .arg(ui->spinBoxSendCount->value() == 0 ? QStringLiteral("unlimited") : QString::number(ui->spinBoxSendCount->value()));

    out << QStringLiteral("--- Commands ---\n");
    const QList<CommandItem> commands = collectCommands();
    int commandNumber = 1;
    for (const CommandItem &item : commands) {
        const QString format = item.hexMode ? QStringLiteral("HEX") : QStringLiteral("ASCII");
        QString commandText = item.text;
        commandText.replace(QLatin1Char('\r'), QStringLiteral("\\r"));
        commandText.replace(QLatin1Char('\n'), QStringLiteral("\\n"));
        out << QStringLiteral("Command %1 | Format: %2 | Data: %3\n")
                   .arg(commandNumber++)
                   .arg(format, commandText);
    }
    out << QLatin1Char('\n');

    const StatisticsSnapshot snap = m_statistics.snapshot();
    out << QStringLiteral("Total Sent: %1\n").arg(snap.totalSent);
    out << QStringLiteral("Success RX: %1\n").arg(snap.successReceived);
    out << QStringLiteral("Lost: %1\n").arg(snap.lostPackets);
    out << QStringLiteral("Success Rate: %1%\n").arg(snap.successRate, 0, 'f', 2);
    out << QStringLiteral("Average: %1 ms\n").arg(snap.averageElapsedMs, 0, 'f', 2);
    out << QStringLiteral("Max: %1 ms\n").arg(snap.maxElapsedMs);
    out << QStringLiteral("Min: %1 ms\n").arg(snap.minElapsedMs);
    out << QStringLiteral("P50: %1 ms\n").arg(snap.p50Ms, 0, 'f', 3);
    out << QStringLiteral("P90: %1 ms\n").arg(snap.p90Ms, 0, 'f', 3);
    out << QStringLiteral("P95: %1 ms\n").arg(snap.p95Ms, 0, 'f', 3);
    out << QStringLiteral("P99: %1 ms\n").arg(snap.p99Ms, 0, 'f', 3);

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

        QString txData = QString::fromUtf8(packet.txPayload);
        if (packet.txFormat == QStringLiteral("HEX")) {
            txData = QString::fromLatin1(packet.txPayload.toHex(' ').toUpper());
        }
        txData.replace(QLatin1Char('\r'), QStringLiteral("\\r"));
        txData.replace(QLatin1Char('\n'), QStringLiteral("\\n"));

        out << QStringLiteral("#%1 %2 %3 ms | Format: %4 | TX Data: %5\n")
                   .arg(packet.id)
                   .arg(statusStr)
                   .arg(packet.elapsedMs >= 0 ? QString::number(packet.elapsedMs) : QStringLiteral("-"))
                   .arg(packet.txFormat.isEmpty() ? QStringLiteral("UNKNOWN") : packet.txFormat)
                   .arg(txData);
    }

    file.close();
    appendLog(LogLevel::Info, QStringLiteral("Test report saved: %1").arg(filePath));
}

void MainWindow::finalizeTcpReport()
{
    const QString fileName = QStringLiteral("CommReport_%1.txt")
                                 .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss")));
    QDir logDir(QDir::current().filePath(QStringLiteral("Log")));
    if (!logDir.exists() && !logDir.mkpath(QStringLiteral("."))) {
        appendLog(LogLevel::Error, QStringLiteral("Failed to create log directory: %1").arg(logDir.absolutePath()));
        return;
    }
    QFile file(logDir.filePath(fileName));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        appendLog(LogLevel::Error, QStringLiteral("Failed to write report: %1").arg(file.errorString()));
        return;
    }

    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);
    out << QStringLiteral("CommBench Pro Multi-Port Test Report\n");
    out << QStringLiteral("=================================\n\n");
    out << QStringLiteral("Mode: TCP Network\n");
    out << QStringLiteral("IP: %1\n").arg(ui->lineEditIp->text().trimmed());
    out << QStringLiteral("Interval: %1 ms\n").arg(ui->spinBoxInterval->value());
    out << QStringLiteral("Timeout: %1 ms\n").arg(ui->spinBoxTimeout->value());
    out << QStringLiteral("Send Count: %1\n\n")
               .arg(ui->spinBoxSendCount->value() == 0 ? QStringLiteral("unlimited") : QString::number(ui->spinBoxSendCount->value()));

    for (const TcpPortSession *session : m_tcpSessions) {
        out << QStringLiteral("--- Port %1 ---\n").arg(session->port);
        const QList<CommandItem> commands = collectCommands(session->commandTable);
        int commandNumber = 1;
        for (const CommandItem &item : commands) {
            const QString format = item.hexMode ? QStringLiteral("HEX") : QStringLiteral("ASCII");
            QString text = item.text;
            text.replace(QLatin1Char('\r'), QStringLiteral("\\r"));
            text.replace(QLatin1Char('\n'), QStringLiteral("\\n"));
            out << QStringLiteral("Command %1 | Format: %2 | Data: %3\n").arg(commandNumber++).arg(format, text);
        }

        const StatisticsSnapshot snap = session->statistics.snapshot();
        out << QStringLiteral("Total Sent: %1\n").arg(snap.totalSent);
        out << QStringLiteral("Success RX: %1\n").arg(snap.successReceived);
        out << QStringLiteral("Lost: %1\n").arg(snap.lostPackets);
        out << QStringLiteral("Success Rate: %1%\n").arg(snap.successRate, 0, 'f', 2);
        out << QStringLiteral("Average: %1 ms\n").arg(snap.averageElapsedMs, 0, 'f', 3);
        out << QStringLiteral("Max: %1 ms\n").arg(snap.maxElapsedMs);
        out << QStringLiteral("Min: %1 ms\n").arg(snap.minElapsedMs);
        out << QStringLiteral("P50: %1 ms\n").arg(snap.p50Ms, 0, 'f', 3);
        out << QStringLiteral("P90: %1 ms\n").arg(snap.p90Ms, 0, 'f', 3);
        out << QStringLiteral("P95: %1 ms\n").arg(snap.p95Ms, 0, 'f', 3);
        out << QStringLiteral("P99: %1 ms\n").arg(snap.p99Ms, 0, 'f', 3);
        out << QStringLiteral("Packet Details:\n");
        for (const PacketInfo &packet : session->statistics.packets()) {
            QString data = packet.txFormat == QStringLiteral("HEX")
                ? QString::fromLatin1(packet.txPayload.toHex(' ').toUpper())
                : QString::fromUtf8(packet.txPayload);
            data.replace(QLatin1Char('\r'), QStringLiteral("\\r"));
            data.replace(QLatin1Char('\n'), QStringLiteral("\\n"));
            const QString status = packet.status == PacketInfo::Status::Success ? QStringLiteral("SUCCESS")
                : packet.status == PacketInfo::Status::Timeout ? QStringLiteral("TIMEOUT") : QStringLiteral("PENDING");
            out << QStringLiteral("#%1 %2 %3 ms | Format: %4 | TX Data: %5\n")
                       .arg(packet.id).arg(status)
                       .arg(packet.elapsedMs >= 0 ? QString::number(packet.elapsedMs) : QStringLiteral("-"))
                       .arg(packet.txFormat.isEmpty() ? QStringLiteral("UNKNOWN") : packet.txFormat)
                       .arg(data);
        }
        out << QLatin1Char('\n');
    }
    file.close();
    appendLog(LogLevel::Info, QStringLiteral("Multi-port test report saved: %1").arg(file.fileName()));
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

QString MainWindow::payloadToDisplay(const QByteArray &payload, const QString &format)
{
    if (payload.isEmpty()) {
        return QStringLiteral("(empty)");
    }

    constexpr int kPreviewBytes = 256;
    if (format.trimmed().compare(QStringLiteral("ASCII"), Qt::CaseInsensitive) == 0) {
        const auto decodeAscii = [](const QByteArray &bytes) {
            QString text = QString::fromUtf8(bytes);
            text.replace(QLatin1Char('\r'), QStringLiteral("\\r"));
            text.replace(QLatin1Char('\n'), QStringLiteral("\\n"));
            return text;
        };

        if (payload.size() <= kPreviewBytes * 2) {
            return decodeAscii(payload);
        }

        return decodeAscii(payload.left(kPreviewBytes)) +
               QStringLiteral(" ... [truncated %1 bytes] ... ").arg(payload.size() - kPreviewBytes * 2) +
               decodeAscii(payload.right(kPreviewBytes));
    }

    if (payload.size() <= kPreviewBytes * 2) {
        return QString::fromLatin1(payload.toHex(' ').toUpper());
    }

    const QByteArray prefix = payload.left(kPreviewBytes).toHex(' ').toUpper();
    const QByteArray suffix = payload.right(kPreviewBytes).toHex(' ').toUpper();
    return QString::fromLatin1(prefix) +
           QStringLiteral(" ... [truncated %1 bytes] ... ").arg(payload.size() - kPreviewBytes * 2) +
           QString::fromLatin1(suffix);
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
