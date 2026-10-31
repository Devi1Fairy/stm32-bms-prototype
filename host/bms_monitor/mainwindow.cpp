#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QMessageBox>
#include <QSerialPortInfo>
#include <QTextCursor>
#include <QStringList>
#include <QFileDialog>
#include <QDateTime>
#include <QDir>
#include <QStandardPaths>

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

    connect(ui->startButton, &QPushButton::clicked, this,
            [this]() { sendCommand(BmsCommand::Start); });

    connect(ui->stopButton, &QPushButton::clicked, this,
            [this]() { sendCommand(BmsCommand::Stop); });

    connect(ui->clearFaultButton, &QPushButton::clicked, this,
            [this]() { sendCommand(BmsCommand::Clear); });

    connect(ui->recordButton, &QPushButton::clicked,
            this, &MainWindow::toggleRecording);

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
            m_latestReport = record;
            m_haveReport = true;
            m_lastReport.start();

            showTelemetry(record);
            checkCommandResult(record);
            updateCommandButtons();
            appendRecording(record);
        }
    }
}

bool MainWindow::hasFreshReport() const
{
    return m_haveReport && m_lastReport.isValid()
    && m_lastReport.elapsed() < 1000;
}

void MainWindow::updateCommandButtons()
{
    const bool connected = m_serial.isOpen();
    const bool fresh = hasFreshReport();
    const bool pending = m_pendingCommand != BmsCommand::None;

    ui->startButton->setEnabled(BmsCommandAllowed(
        BmsCommand::Start, connected, fresh, pending, m_latestReport));

    ui->stopButton->setEnabled(BmsCommandAllowed(
        BmsCommand::Stop, connected, fresh, pending, m_latestReport));

    ui->clearFaultButton->setEnabled(BmsCommandAllowed(
        BmsCommand::Clear, connected, fresh, pending, m_latestReport));
}

void MainWindow::sendCommand(BmsCommand command)
{
    // 点击时再检查一次，避免按钮显示后数据已经过期。
    if (!BmsCommandAllowed(command, m_serial.isOpen(), hasFreshReport(),
                           m_pendingCommand != BmsCommand::None,
                           m_latestReport))
    {
        ui->commandLabel->setText(
            QStringLiteral("当前条件不允许此命令。"));
        return;
    }

    // 只发送一个字符，不附加回车换行。
    const QByteArray data(1, static_cast<char>(command));

    if (m_serial.write(data) != data.size())
    {
        m_pendingCommand = BmsCommand::None;
        m_commandWait.invalidate();

        ui->commandLabel->setText(
            QStringLiteral("串口写入失败，结果未确认。"));

        updateCommandButtons();
        return;
    }

    // write接收了字节，接下来等待上报。
    // 停止请求可以覆盖之前正在等待的其他命令。
    m_pendingCommand = command;
    m_commandWait.start();

    ui->commandLabel->setText(
        QStringLiteral("已交给串口：%1，等待MCU上报。")
            .arg(QChar::fromLatin1(static_cast<char>(command))));

    updateCommandButtons();
}

void MainWindow::checkCommandResult(const BmsLogRecord &record)
{
    if (m_pendingCommand == BmsCommand::None
        || !BmsCommandTargetObserved(m_pendingCommand, record))
    {
        return;
    }

    ui->commandLabel->setText(
        QStringLiteral("已观察到目标状态（命令 %1）。")
            .arg(QChar::fromLatin1(
                static_cast<char>(m_pendingCommand))));

    m_pendingCommand = BmsCommand::None;
    m_commandWait.invalidate();
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
    if (m_serial.isOpen() && m_lastReport.isValid()
        && m_lastReport.elapsed() >= 1000)
    {
        ui->telemetryLabel->setText(
            QStringLiteral(
                "数据过期：超过1秒没有收到可解析的完整上报。"));

        m_lastReport.invalidate();
        m_haveReport = false;

        if (m_pendingCommand != BmsCommand::None)
        {
            ui->commandLabel->setText(
                QStringLiteral("上报已过期，命令结果未确认。"));

            m_pendingCommand = BmsCommand::None;
            m_commandWait.invalidate();
        }
    }

    if (m_pendingCommand != BmsCommand::None
        && m_commandWait.isValid()
        && m_commandWait.elapsed() >= 2000)
    {
        ui->commandLabel->setText(
            QStringLiteral(
                "2秒内未观察到目标状态，命令结果未确认。"));

        m_pendingCommand = BmsCommand::None;
        m_commandWait.invalidate();
    }

    updateCommandButtons();
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
    const bool hadPending = m_pendingCommand != BmsCommand::None;

    // 连接变化后，旧报告和旧等待状态不进入新会话。
    m_lineBuffer.reset();
    m_haveReport = false;
    m_latestReport = BmsLogRecord{};
    m_pendingCommand = BmsCommand::None;

    m_commandWait.invalidate();
    m_lastReport.invalidate();

    if (connected)
    {
        m_lastReport.start();
    }

    ui->telemetryLabel->setText(connected
                                    ? QStringLiteral("等待完整BMS上报……")
                                    : QStringLiteral("未连接"));

    ui->commandLabel->setText(hadPending
                                  ? QStringLiteral("连接状态改变，上一命令结果未确认。")
                                  : (connected
                                         ? QStringLiteral("等待指令。")
                                         : QStringLiteral("未连接")));

    ui->connectButton->setText(
        connected ? "Disconnect" : "Connect");

    ui->portCombo->setEnabled(!connected);
    ui->refreshButton->setEnabled(!connected);

    ui->statusLabel->setText(connected
                                 ? m_serial.portName() + " 115200 8N1"
                                 : QStringLiteral("Disconnected"));

    updateCommandButtons();

    if (!connected)
    {
        stopRecording();
    }

    updateRecordingUi();
}

void MainWindow::toggleRecording()
{
    if (m_csvLogger.isRecording())
    {
        stopRecording();
        return;
    }

    if (!m_serial.isOpen())
    {
        ui->recordLabel->setText(QStringLiteral("请先连接串口。"));
        return;
    }

    QString directory =
        QStandardPaths::writableLocation(
            QStandardPaths::DocumentsLocation);

    if (directory.isEmpty())
    {
        directory = QDir::homePath();
    }

    const QString suggestedName =
        "bms_" + QDateTime::currentDateTime()
                     .toString("yyyyMMdd_HHmmss_zzz")
        + ".csv";

    QString fileName = QFileDialog::getSaveFileName(
        this,
        QStringLiteral("新建CSV记录"),
        QDir(directory).filePath(suggestedName),
        QStringLiteral("CSV files (*.csv)"),
        nullptr,
        QFileDialog::DontUseNativeDialog
            | QFileDialog::DontConfirmOverwrite);

    if (fileName.isEmpty())
    {
        return;
    }

    // 选文件期间也可能拔掉串口，返回后再检查。
    if (!m_serial.isOpen())
    {
        ui->recordLabel->setText(
            QStringLiteral("连接已结束，未开始记录。"));
        return;
    }

    if (!fileName.endsWith(".csv", Qt::CaseInsensitive))
    {
        fileName += ".csv";
    }

    if (!m_csvLogger.start(fileName))
    {
        ui->recordLabel->setText(
            QStringLiteral("无法开始记录：%1")
                .arg(m_csvLogger.errorString()));

        updateRecordingUi();
        return;
    }

    ui->recordLabel->setToolTip(fileName);
    ui->recordLabel->setText(
        QStringLiteral("记录中：0 行，等待上报。"));

    updateRecordingUi();
}

void MainWindow::appendRecording(const BmsLogRecord &record)
{
    if (!m_csvLogger.isRecording())
    {
        return;
    }

    if (!m_csvLogger.append(record))
    {
        ui->recordLabel->setText(
            QStringLiteral("记录已停止：%1")
                .arg(m_csvLogger.errorString()));

        updateRecordingUi();
        return;
    }

    ui->recordLabel->setText(
        QStringLiteral("记录中：%1 行")
            .arg(m_csvLogger.rowCount()));
}

void MainWindow::stopRecording()
{
    if (!m_csvLogger.isRecording())
    {
        return;
    }

    const bool ok = m_csvLogger.stop();

    ui->recordLabel->setText(ok
                                 ? QStringLiteral("已保存：%1 行")
                                       .arg(m_csvLogger.rowCount())
                                 : QStringLiteral("保存失败：%1")
                                       .arg(m_csvLogger.errorString()));

    updateRecordingUi();
}

void MainWindow::updateRecordingUi()
{
    const bool recording = m_csvLogger.isRecording();

    ui->recordButton->setText(recording
                                  ? QStringLiteral("停止记录 CSV")
                                  : QStringLiteral("开始记录 CSV"));

    ui->recordButton->setEnabled(
        recording || m_serial.isOpen());
}
