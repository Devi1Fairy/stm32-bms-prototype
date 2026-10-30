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

    connect(&m_reportTimer, &QTimer::timeout,
            this, &MainWindow::checkReportTimeout);

    m_reportTimer.start(200);

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
    const auto lines = m_lineBuffer.append(m_serial.readAll());

    for (const QByteArray &line : lines)
    {
        // 现在按完整行显示日志。
        ui->rxText->appendPlainText(QString::fromLatin1(line));

        BmsLogRecord record;

        if (ParseBmsLog(line, record))
        {
            m_lastReport.start();
            showTelemetry(record);
        }
    }
}

void MainWindow::showTelemetry(const BmsLogRecord &record)
{
    static const QStringList stateNames = {
        QStringLiteral("自检"),
        QStringLiteral("待机"),
        QStringLiteral("运行"),
        QStringLiteral("故障锁定")
    };

    // 当前UART没有电压、温度各自的有效位。
    // 自检或存在采样异常时，先保守隐藏这两项。
    const bool samplingOk = record.state != 0
                            && (record.activeFaults & 0xE0) == 0;

    const QString cell = samplingOk
                             ? QString::number(record.cellMv / 1000.0, 'f', 3)
                             : QStringLiteral("--");

    const QString temperature = samplingOk
                                    ? QString::number(record.temperatureDc / 10.0, 'f', 1)
                                    : QStringLiteral("--");

    // 有效标志、采样年龄、来源都符合条件，才显示实采电流。
    const bool currentOk = record.currentValid == 1
                           && record.currentAgeMs < 500
                           && record.currentSource == 3;

    const QString current = currentOk
                                ? QString::number(record.currentMa)
                                : QStringLiteral("--（无有效实采）");

    ui->telemetryLabel->setText(
        QStringLiteral(
            "数据：已收到完整上报\n"
            "模拟单体电压：%1 V    NTC估算：%2 ℃\n"
            "INA226电流：%3 mA\n"
            "状态：%4    负载许可：%5\n"
            "当前故障：0x%6    锁存故障：0x%7")
            .arg(cell)
            .arg(temperature)
            .arg(current)
            .arg(stateNames.at(static_cast<qsizetype>(record.state)))
            .arg(record.load == 1
                     ? QStringLiteral("开启")
                     : QStringLiteral("关闭"))
            .arg(record.activeFaults, 8, 16, QLatin1Char('0'))
            .arg(record.latchedFaults, 8, 16, QLatin1Char('0')));
}

void MainWindow::checkReportTimeout()
{
    if (m_serial.isOpen()
        && m_lastReport.isValid()
        && m_lastReport.elapsed() >= 1000)
    {
        ui->telemetryLabel->setText(
            QStringLiteral(
                "数据过期：超过1秒没有收到可解析的完整上报。"));

        // 提示一次即可，下次成功解析时重新开始计时。
        m_lastReport.invalidate();
    }
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

    // 新会话不能拼接上一次连接留下的半行。
    m_lineBuffer.reset();
    m_lastReport.invalidate();

    if (connected)
    {
        m_lastReport.start();
    }

    ui->telemetryLabel->setText(connected
                                    ? QStringLiteral("等待完整BMS上报……")
                                    : QStringLiteral("未连接"));

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