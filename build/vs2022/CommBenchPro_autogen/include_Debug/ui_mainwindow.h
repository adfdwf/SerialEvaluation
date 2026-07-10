/********************************************************************************
** Form generated from reading UI file 'mainwindow.ui'
**
** Created by: Qt User Interface Compiler version 6.8.3
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_MAINWINDOW_H
#define UI_MAINWINDOW_H

#include <QtCore/QVariant>
#include <QtWidgets/QApplication>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QFrame>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QMainWindow>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSpacerItem>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QSplitter>
#include <QtWidgets/QStatusBar>
#include <QtWidgets/QTextEdit>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

QT_BEGIN_NAMESPACE

class Ui_MainWindow
{
public:
    QWidget *centralwidget;
    QVBoxLayout *verticalLayoutMain;
    QGroupBox *groupBoxConfig;
    QHBoxLayout *horizontalLayoutConfig;
    QLabel *labelMode;
    QComboBox *comboBoxMode;
    QWidget *widgetTcpConfig;
    QHBoxLayout *horizontalLayoutTcp;
    QLabel *labelIp;
    QLineEdit *lineEditIp;
    QLabel *labelPort;
    QSpinBox *spinBoxPort;
    QWidget *widgetSerialConfig;
    QHBoxLayout *horizontalLayoutSerial;
    QLabel *labelSerialPort;
    QComboBox *comboBoxSerialPort;
    QPushButton *pushButtonRefreshPorts;
    QLabel *labelBaud;
    QComboBox *comboBoxBaudRate;
    QLabel *labelDataBits;
    QComboBox *comboBoxDataBits;
    QLabel *labelStopBits;
    QComboBox *comboBoxStopBits;
    QLabel *labelParity;
    QComboBox *comboBoxParity;
    QSpacerItem *horizontalSpacerConfig;
    QFrame *frameLed;
    QLabel *labelConnectionState;
    QPushButton *pushButtonConnect;
    QPushButton *pushButtonDisconnect;
    QGroupBox *groupBoxControl;
    QHBoxLayout *horizontalLayoutControl;
    QLabel *labelCommand;
    QLineEdit *lineEditCommand;
    QComboBox *comboBoxPayloadMode;
    QLabel *labelInterval;
    QSpinBox *spinBoxInterval;
    QLabel *labelSendCount;
    QSpinBox *spinBoxSendCount;
    QLabel *labelTimeout;
    QSpinBox *spinBoxTimeout;
    QPushButton *pushButtonStart;
    QPushButton *pushButtonStop;
    QSplitter *splitterData;
    QGroupBox *groupBoxStats;
    QGridLayout *gridLayoutStats;
    QLabel *labelTotalSent;
    QLabel *labelTotalSentValue;
    QLabel *labelSuccess;
    QLabel *labelSuccessValue;
    QLabel *labelLost;
    QLabel *labelLostValue;
    QLabel *labelRate;
    QLabel *labelRateValue;
    QLabel *labelAverage;
    QLabel *labelAverageValue;
    QLabel *labelMax;
    QLabel *labelMaxValue;
    QLabel *labelMin;
    QLabel *labelMinValue;
    QSpacerItem *verticalSpacerStats;
    QGroupBox *groupBoxLog;
    QVBoxLayout *verticalLayoutLog;
    QTextEdit *textEditLog;
    QStatusBar *statusbar;

    void setupUi(QMainWindow *MainWindow)
    {
        if (MainWindow->objectName().isEmpty())
            MainWindow->setObjectName("MainWindow");
        MainWindow->resize(1280, 780);
        centralwidget = new QWidget(MainWindow);
        centralwidget->setObjectName("centralwidget");
        verticalLayoutMain = new QVBoxLayout(centralwidget);
        verticalLayoutMain->setSpacing(10);
        verticalLayoutMain->setObjectName("verticalLayoutMain");
        verticalLayoutMain->setContentsMargins(12, 12, 12, 12);
        groupBoxConfig = new QGroupBox(centralwidget);
        groupBoxConfig->setObjectName("groupBoxConfig");
        horizontalLayoutConfig = new QHBoxLayout(groupBoxConfig);
        horizontalLayoutConfig->setSpacing(12);
        horizontalLayoutConfig->setObjectName("horizontalLayoutConfig");
        labelMode = new QLabel(groupBoxConfig);
        labelMode->setObjectName("labelMode");

        horizontalLayoutConfig->addWidget(labelMode);

        comboBoxMode = new QComboBox(groupBoxConfig);
        comboBoxMode->setObjectName("comboBoxMode");
        comboBoxMode->setMinimumSize(QSize(150, 0));

        horizontalLayoutConfig->addWidget(comboBoxMode);

        widgetTcpConfig = new QWidget(groupBoxConfig);
        widgetTcpConfig->setObjectName("widgetTcpConfig");
        horizontalLayoutTcp = new QHBoxLayout(widgetTcpConfig);
        horizontalLayoutTcp->setObjectName("horizontalLayoutTcp");
        horizontalLayoutTcp->setContentsMargins(0, 0, 0, 0);
        labelIp = new QLabel(widgetTcpConfig);
        labelIp->setObjectName("labelIp");

        horizontalLayoutTcp->addWidget(labelIp);

        lineEditIp = new QLineEdit(widgetTcpConfig);
        lineEditIp->setObjectName("lineEditIp");
        lineEditIp->setMinimumSize(QSize(150, 0));

        horizontalLayoutTcp->addWidget(lineEditIp);

        labelPort = new QLabel(widgetTcpConfig);
        labelPort->setObjectName("labelPort");

        horizontalLayoutTcp->addWidget(labelPort);

        spinBoxPort = new QSpinBox(widgetTcpConfig);
        spinBoxPort->setObjectName("spinBoxPort");

        horizontalLayoutTcp->addWidget(spinBoxPort);


        horizontalLayoutConfig->addWidget(widgetTcpConfig);

        widgetSerialConfig = new QWidget(groupBoxConfig);
        widgetSerialConfig->setObjectName("widgetSerialConfig");
        horizontalLayoutSerial = new QHBoxLayout(widgetSerialConfig);
        horizontalLayoutSerial->setObjectName("horizontalLayoutSerial");
        horizontalLayoutSerial->setContentsMargins(0, 0, 0, 0);
        labelSerialPort = new QLabel(widgetSerialConfig);
        labelSerialPort->setObjectName("labelSerialPort");

        horizontalLayoutSerial->addWidget(labelSerialPort);

        comboBoxSerialPort = new QComboBox(widgetSerialConfig);
        comboBoxSerialPort->setObjectName("comboBoxSerialPort");
        comboBoxSerialPort->setMinimumSize(QSize(110, 0));

        horizontalLayoutSerial->addWidget(comboBoxSerialPort);

        pushButtonRefreshPorts = new QPushButton(widgetSerialConfig);
        pushButtonRefreshPorts->setObjectName("pushButtonRefreshPorts");

        horizontalLayoutSerial->addWidget(pushButtonRefreshPorts);

        labelBaud = new QLabel(widgetSerialConfig);
        labelBaud->setObjectName("labelBaud");

        horizontalLayoutSerial->addWidget(labelBaud);

        comboBoxBaudRate = new QComboBox(widgetSerialConfig);
        comboBoxBaudRate->setObjectName("comboBoxBaudRate");

        horizontalLayoutSerial->addWidget(comboBoxBaudRate);

        labelDataBits = new QLabel(widgetSerialConfig);
        labelDataBits->setObjectName("labelDataBits");

        horizontalLayoutSerial->addWidget(labelDataBits);

        comboBoxDataBits = new QComboBox(widgetSerialConfig);
        comboBoxDataBits->setObjectName("comboBoxDataBits");

        horizontalLayoutSerial->addWidget(comboBoxDataBits);

        labelStopBits = new QLabel(widgetSerialConfig);
        labelStopBits->setObjectName("labelStopBits");

        horizontalLayoutSerial->addWidget(labelStopBits);

        comboBoxStopBits = new QComboBox(widgetSerialConfig);
        comboBoxStopBits->setObjectName("comboBoxStopBits");

        horizontalLayoutSerial->addWidget(comboBoxStopBits);

        labelParity = new QLabel(widgetSerialConfig);
        labelParity->setObjectName("labelParity");

        horizontalLayoutSerial->addWidget(labelParity);

        comboBoxParity = new QComboBox(widgetSerialConfig);
        comboBoxParity->setObjectName("comboBoxParity");

        horizontalLayoutSerial->addWidget(comboBoxParity);


        horizontalLayoutConfig->addWidget(widgetSerialConfig);

        horizontalSpacerConfig = new QSpacerItem(40, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);

        horizontalLayoutConfig->addItem(horizontalSpacerConfig);

        frameLed = new QFrame(groupBoxConfig);
        frameLed->setObjectName("frameLed");
        frameLed->setFrameShape(QFrame::StyledPanel);

        horizontalLayoutConfig->addWidget(frameLed);

        labelConnectionState = new QLabel(groupBoxConfig);
        labelConnectionState->setObjectName("labelConnectionState");
        labelConnectionState->setMinimumSize(QSize(105, 0));

        horizontalLayoutConfig->addWidget(labelConnectionState);

        pushButtonConnect = new QPushButton(groupBoxConfig);
        pushButtonConnect->setObjectName("pushButtonConnect");

        horizontalLayoutConfig->addWidget(pushButtonConnect);

        pushButtonDisconnect = new QPushButton(groupBoxConfig);
        pushButtonDisconnect->setObjectName("pushButtonDisconnect");

        horizontalLayoutConfig->addWidget(pushButtonDisconnect);


        verticalLayoutMain->addWidget(groupBoxConfig);

        groupBoxControl = new QGroupBox(centralwidget);
        groupBoxControl->setObjectName("groupBoxControl");
        horizontalLayoutControl = new QHBoxLayout(groupBoxControl);
        horizontalLayoutControl->setSpacing(10);
        horizontalLayoutControl->setObjectName("horizontalLayoutControl");
        labelCommand = new QLabel(groupBoxControl);
        labelCommand->setObjectName("labelCommand");

        horizontalLayoutControl->addWidget(labelCommand);

        lineEditCommand = new QLineEdit(groupBoxControl);
        lineEditCommand->setObjectName("lineEditCommand");

        horizontalLayoutControl->addWidget(lineEditCommand);

        comboBoxPayloadMode = new QComboBox(groupBoxControl);
        comboBoxPayloadMode->setObjectName("comboBoxPayloadMode");
        comboBoxPayloadMode->setMinimumSize(QSize(90, 0));

        horizontalLayoutControl->addWidget(comboBoxPayloadMode);

        labelInterval = new QLabel(groupBoxControl);
        labelInterval->setObjectName("labelInterval");

        horizontalLayoutControl->addWidget(labelInterval);

        spinBoxInterval = new QSpinBox(groupBoxControl);
        spinBoxInterval->setObjectName("spinBoxInterval");

        horizontalLayoutControl->addWidget(spinBoxInterval);

        labelSendCount = new QLabel(groupBoxControl);
        labelSendCount->setObjectName("labelSendCount");

        horizontalLayoutControl->addWidget(labelSendCount);

        spinBoxSendCount = new QSpinBox(groupBoxControl);
        spinBoxSendCount->setObjectName("spinBoxSendCount");

        horizontalLayoutControl->addWidget(spinBoxSendCount);

        labelTimeout = new QLabel(groupBoxControl);
        labelTimeout->setObjectName("labelTimeout");

        horizontalLayoutControl->addWidget(labelTimeout);

        spinBoxTimeout = new QSpinBox(groupBoxControl);
        spinBoxTimeout->setObjectName("spinBoxTimeout");

        horizontalLayoutControl->addWidget(spinBoxTimeout);

        pushButtonStart = new QPushButton(groupBoxControl);
        pushButtonStart->setObjectName("pushButtonStart");

        horizontalLayoutControl->addWidget(pushButtonStart);

        pushButtonStop = new QPushButton(groupBoxControl);
        pushButtonStop->setObjectName("pushButtonStop");

        horizontalLayoutControl->addWidget(pushButtonStop);


        verticalLayoutMain->addWidget(groupBoxControl);

        splitterData = new QSplitter(centralwidget);
        splitterData->setObjectName("splitterData");
        splitterData->setOrientation(Qt::Horizontal);
        groupBoxStats = new QGroupBox(splitterData);
        groupBoxStats->setObjectName("groupBoxStats");
        groupBoxStats->setMinimumSize(QSize(360, 0));
        gridLayoutStats = new QGridLayout(groupBoxStats);
        gridLayoutStats->setObjectName("gridLayoutStats");
        labelTotalSent = new QLabel(groupBoxStats);
        labelTotalSent->setObjectName("labelTotalSent");

        gridLayoutStats->addWidget(labelTotalSent, 0, 0, 1, 1);

        labelTotalSentValue = new QLabel(groupBoxStats);
        labelTotalSentValue->setObjectName("labelTotalSentValue");

        gridLayoutStats->addWidget(labelTotalSentValue, 0, 1, 1, 1);

        labelSuccess = new QLabel(groupBoxStats);
        labelSuccess->setObjectName("labelSuccess");

        gridLayoutStats->addWidget(labelSuccess, 1, 0, 1, 1);

        labelSuccessValue = new QLabel(groupBoxStats);
        labelSuccessValue->setObjectName("labelSuccessValue");

        gridLayoutStats->addWidget(labelSuccessValue, 1, 1, 1, 1);

        labelLost = new QLabel(groupBoxStats);
        labelLost->setObjectName("labelLost");

        gridLayoutStats->addWidget(labelLost, 2, 0, 1, 1);

        labelLostValue = new QLabel(groupBoxStats);
        labelLostValue->setObjectName("labelLostValue");

        gridLayoutStats->addWidget(labelLostValue, 2, 1, 1, 1);

        labelRate = new QLabel(groupBoxStats);
        labelRate->setObjectName("labelRate");

        gridLayoutStats->addWidget(labelRate, 3, 0, 1, 1);

        labelRateValue = new QLabel(groupBoxStats);
        labelRateValue->setObjectName("labelRateValue");

        gridLayoutStats->addWidget(labelRateValue, 3, 1, 1, 1);

        labelAverage = new QLabel(groupBoxStats);
        labelAverage->setObjectName("labelAverage");

        gridLayoutStats->addWidget(labelAverage, 4, 0, 1, 1);

        labelAverageValue = new QLabel(groupBoxStats);
        labelAverageValue->setObjectName("labelAverageValue");

        gridLayoutStats->addWidget(labelAverageValue, 4, 1, 1, 1);

        labelMax = new QLabel(groupBoxStats);
        labelMax->setObjectName("labelMax");

        gridLayoutStats->addWidget(labelMax, 5, 0, 1, 1);

        labelMaxValue = new QLabel(groupBoxStats);
        labelMaxValue->setObjectName("labelMaxValue");

        gridLayoutStats->addWidget(labelMaxValue, 5, 1, 1, 1);

        labelMin = new QLabel(groupBoxStats);
        labelMin->setObjectName("labelMin");

        gridLayoutStats->addWidget(labelMin, 6, 0, 1, 1);

        labelMinValue = new QLabel(groupBoxStats);
        labelMinValue->setObjectName("labelMinValue");

        gridLayoutStats->addWidget(labelMinValue, 6, 1, 1, 1);

        verticalSpacerStats = new QSpacerItem(20, 240, QSizePolicy::Policy::Minimum, QSizePolicy::Policy::Expanding);

        gridLayoutStats->addItem(verticalSpacerStats, 7, 0, 1, 2);

        splitterData->addWidget(groupBoxStats);
        groupBoxLog = new QGroupBox(splitterData);
        groupBoxLog->setObjectName("groupBoxLog");
        verticalLayoutLog = new QVBoxLayout(groupBoxLog);
        verticalLayoutLog->setObjectName("verticalLayoutLog");
        textEditLog = new QTextEdit(groupBoxLog);
        textEditLog->setObjectName("textEditLog");
        textEditLog->setReadOnly(true);

        verticalLayoutLog->addWidget(textEditLog);

        splitterData->addWidget(groupBoxLog);

        verticalLayoutMain->addWidget(splitterData);

        MainWindow->setCentralWidget(centralwidget);
        statusbar = new QStatusBar(MainWindow);
        statusbar->setObjectName("statusbar");
        MainWindow->setStatusBar(statusbar);

        retranslateUi(MainWindow);

        QMetaObject::connectSlotsByName(MainWindow);
    } // setupUi

    void retranslateUi(QMainWindow *MainWindow)
    {
        MainWindow->setWindowTitle(QCoreApplication::translate("MainWindow", "CommBench Pro", nullptr));
        groupBoxConfig->setTitle(QCoreApplication::translate("MainWindow", "Connection Configuration", nullptr));
        labelMode->setText(QCoreApplication::translate("MainWindow", "Mode", nullptr));
        labelIp->setText(QCoreApplication::translate("MainWindow", "IP", nullptr));
        labelPort->setText(QCoreApplication::translate("MainWindow", "Port", nullptr));
        labelSerialPort->setText(QCoreApplication::translate("MainWindow", "COM", nullptr));
        pushButtonRefreshPorts->setText(QCoreApplication::translate("MainWindow", "Refresh", nullptr));
        labelBaud->setText(QCoreApplication::translate("MainWindow", "Baud", nullptr));
        labelDataBits->setText(QCoreApplication::translate("MainWindow", "Data", nullptr));
        labelStopBits->setText(QCoreApplication::translate("MainWindow", "Stop", nullptr));
        labelParity->setText(QCoreApplication::translate("MainWindow", "Parity", nullptr));
        labelConnectionState->setText(QCoreApplication::translate("MainWindow", "Disconnected", nullptr));
        pushButtonConnect->setText(QCoreApplication::translate("MainWindow", "Connect", nullptr));
        pushButtonDisconnect->setText(QCoreApplication::translate("MainWindow", "Disconnect", nullptr));
        groupBoxControl->setTitle(QCoreApplication::translate("MainWindow", "Test Control", nullptr));
        labelCommand->setText(QCoreApplication::translate("MainWindow", "Command", nullptr));
        lineEditCommand->setPlaceholderText(QCoreApplication::translate("MainWindow", "ASCII command or HEX bytes", nullptr));
        labelInterval->setText(QCoreApplication::translate("MainWindow", "Interval ms", nullptr));
        labelSendCount->setText(QCoreApplication::translate("MainWindow", "Count", nullptr));
        labelTimeout->setText(QCoreApplication::translate("MainWindow", "Timeout ms", nullptr));
        pushButtonStart->setText(QCoreApplication::translate("MainWindow", "Start", nullptr));
        pushButtonStop->setText(QCoreApplication::translate("MainWindow", "Stop", nullptr));
        groupBoxStats->setTitle(QCoreApplication::translate("MainWindow", "Realtime Statistics", nullptr));
        labelTotalSent->setText(QCoreApplication::translate("MainWindow", "Total Sent", nullptr));
        labelTotalSentValue->setText(QCoreApplication::translate("MainWindow", "0", nullptr));
        labelSuccess->setText(QCoreApplication::translate("MainWindow", "Success RX", nullptr));
        labelSuccessValue->setText(QCoreApplication::translate("MainWindow", "0", nullptr));
        labelLost->setText(QCoreApplication::translate("MainWindow", "Lost", nullptr));
        labelLostValue->setText(QCoreApplication::translate("MainWindow", "0", nullptr));
        labelRate->setText(QCoreApplication::translate("MainWindow", "Success Rate", nullptr));
        labelRateValue->setText(QCoreApplication::translate("MainWindow", "0.00%", nullptr));
        labelAverage->setText(QCoreApplication::translate("MainWindow", "Average", nullptr));
        labelAverageValue->setText(QCoreApplication::translate("MainWindow", "0.00 ms", nullptr));
        labelMax->setText(QCoreApplication::translate("MainWindow", "Max", nullptr));
        labelMaxValue->setText(QCoreApplication::translate("MainWindow", "0 ms", nullptr));
        labelMin->setText(QCoreApplication::translate("MainWindow", "Min", nullptr));
        labelMinValue->setText(QCoreApplication::translate("MainWindow", "0 ms", nullptr));
        groupBoxLog->setTitle(QCoreApplication::translate("MainWindow", "Communication Log", nullptr));
    } // retranslateUi

};

namespace Ui {
    class MainWindow: public Ui_MainWindow {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_MAINWINDOW_H
