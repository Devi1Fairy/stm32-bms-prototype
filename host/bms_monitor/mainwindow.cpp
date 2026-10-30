#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QMessageBox>
#include <QSerialPortInfo>
#include <QTextCursor>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    // 不让Qt在关闭串口前恢复打开前缓存的旧参数。
    m_serial.setSettingsRestoredOnClose(false);
    setWindowTitle("BMS Monitor");

    ui->rxText->setReadOnly(true);

    // 只保留最近1000个文本块，避免日志一直积累。
    ui->rxText->setMaximumBlockCount(1000);

    // connect注册事件与处理函数的关系，不是立即调用函数。
    connect(ui->refreshButton, &QPushButton::clicked,
            this, &MainWindow::refreshPorts);

    connect(ui->connectButton, &QPushButton::clicked,
            this, &MainWindow::toggleConnection);

    connect(&m_serial, &QSerialPort::readyRead,
            this, &MainWindow::readSerialData);

    connect(&m_serial, &QSerialPort::errorOccurred,
            this, &MainWindow::handleSerialError);

    refreshPorts();
    updateConnectionUi();
}

MainWindow::~MainWindow()
{
    // 退出时释放串口，其他程序才可以重新使用它。
    m_serial.close();
    delete ui;
}

void MainWindow::refreshPorts()
{
    const QString previousPort =
        ui->portCombo->currentData().toString();

    ui->portCombo->clear();

    const auto ports = QSerialPortInfo::availablePorts();

    for (const QSerialPortInfo &port : ports)
    {
        // 显示端口和说明，实际连接使用单独保存的端口名。
        const QString text =
            port.portName() + " - " + port.description();

        ui->portCombo->addItem(text, port.portName());
    }

    // 刷新后尽量保留之前的选择。
    const int previousIndex =
        ui->portCombo->findData(previousPort);

    if (previousIndex >= 0)
    {
        ui->portCombo->setCurrentIndex(previousIndex);
    }
}

void MainWindow::toggleConnection()
{
    if (m_serial.isOpen())
    {
        m_serial.close();
        updateConnectionUi();
        return;
    }

    const QString portName =
        ui->portCombo->currentData().toString();

    if (portName.isEmpty())
    {
        QMessageBox::warning(
            this, "Serial", "No serial port selected.");
        return;
    }

    // 与STM32当前UART配置保持一致：115200、8N1、无流控。
    m_serial.setPortName(portName);
    m_serial.setBaudRate(QSerialPort::Baud115200);
    m_serial.setDataBits(QSerialPort::Data8);
    m_serial.setParity(QSerialPort::NoParity);
    m_serial.setStopBits(QSerialPort::OneStop);
    m_serial.setFlowControl(QSerialPort::NoFlowControl);

    if (!m_serial.open(QIODevice::ReadWrite))
    {
        QMessageBox::warning(
            this, "Open failed", m_serial.errorString());
        return;
    }

    // 与此前串口助手保持一致，不主动拉起DTR和RTS。
    m_serial.setDataTerminalReady(false);
    m_serial.setRequestToSend(false);

    updateConnectionUi();
}

void MainWindow::readSerialData()
{
    const QByteArray data = m_serial.readAll();

    // MCU当前输出ASCII日志，收到多少就显示多少。
    // 一次readyRead可能只有半行，也可能包含好几行。
    QTextCursor cursor = ui->rxText->textCursor();
    cursor.movePosition(QTextCursor::End);
    cursor.insertText(QString::fromLatin1(data));

    ui->rxText->setTextCursor(cursor);
    ui->rxText->ensureCursorVisible();
}

void MainWindow::handleSerialError(
    QSerialPort::SerialPortError error)
{
    // 打开失败由toggleConnection提示。
    // 这里处理连接中拔出设备等错误。
    if (error != QSerialPort::ResourceError ||
        !m_serial.isOpen())
    {
        return;
    }

    const QString message = m_serial.errorString();

    m_serial.close();
    updateConnectionUi();

    QMessageBox::warning(
        this, "Serial disconnected", message);
}

void MainWindow::updateConnectionUi()
{
    const bool connected = m_serial.isOpen();

    ui->connectButton->setText(
        connected ? "Disconnect" : "Connect");

    // 连接期间固定端口选择。
    ui->portCombo->setEnabled(!connected);
    ui->refreshButton->setEnabled(!connected);

    if (connected)
    {
        ui->statusLabel->setText(
            m_serial.portName() + " 115200 8N1");
    }
    else
    {
        ui->statusLabel->setText("Disconnected");
    }
}