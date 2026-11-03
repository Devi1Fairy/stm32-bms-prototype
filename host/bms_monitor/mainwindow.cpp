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
#include <QHostAddress>
#include <QNetworkProxy>

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

    ui->trendCombo->addItems({
        QStringLiteral("模拟单体电压 / V"),
        QStringLiteral("NTC估算温度 / ℃"),
        QStringLiteral("INA226电流 / mA")
    });

    connect(ui->trendCombo, &QComboBox::currentIndexChanged,
            ui->trendWidget, &BmsTrendWidget::setChannel);

    // TCP只接收遥测，不发送初始化或控制指令。
    m_tcp.setReadBufferSize(4096);

    // 局域网网关使用直连，不经过系统代理。
    m_tcp.setProxy(QNetworkProxy::NoProxy);

    // 限制连接等待时间，避免一直停在“连接中”。
    m_tcpConnectTimer.setSingleShot(true);
    m_tcpConnectTimer.setInterval(5000);

    connect(ui->tcpConnectButton, &QPushButton::clicked,
            this, &MainWindow::toggleTcpConnection);

    connect(&m_tcp, &QTcpSocket::readyRead,
            this, &MainWindow::readTcpData);

    connect(&m_tcp, &QTcpSocket::errorOccurred,
            this, &MainWindow::handleTcpError);

    // 中间状态只刷新控件，不清空历史。
    connect(&m_tcp, &QTcpSocket::stateChanged, this,
            [this](QAbstractSocket::SocketState)
            {
                updateConnectionControls();
            });

    connect(&m_tcp, &QTcpSocket::connected, this, [this]()
            {
                m_tcpConnectTimer.stop();
                updateConnectionUi();
            });

    connect(&m_tcp, &QTcpSocket::disconnected, this, [this]()
            {
                m_tcpConnectTimer.stop();

                // 先处理已经收到的数据，再结束当前会话。
                if (m_tcp.bytesAvailable() > 0)
                    readTcpData();

                updateConnectionUi();
            });

    connect(&m_tcpConnectTimer, &QTimer::timeout, this, [this]()
            {
                if (m_tcp.state() == QAbstractSocket::UnconnectedState
                    || m_tcp.state() == QAbstractSocket::ConnectedState)
                    return;

                m_tcp.abort();
                updateConnectionUi();

                ui->rxText->appendPlainText(
                    QStringLiteral("[TCP] 5秒内未建立连接，已取消。"));
            });

    refreshPorts();
    updateConnectionUi();
}

MainWindow::~MainWindow()
{
    m_reportTimer.stop();
    m_tcpConnectTimer.stop();

    // 关闭socket时不再调用窗口回调。
    QObject::disconnect(&m_tcp, nullptr, this, nullptr);
    m_tcp.abort();

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

    if (m_tcp.state() != QAbstractSocket::UnconnectedState)
    {
        ui->rxText->appendPlainText(
            QStringLiteral("[Serial] 请先断开或取消TCP连接。"));
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
    processIncomingData(m_serial.readAll());
}

void MainWindow::processIncomingData(
    const QByteArray &data, bool tcpSource)
{
    const auto lines = m_lineBuffer.append(data, !tcpSource);

    for (const QByteArray &line : lines)
    {
        ui->rxText->appendPlainText(QString::fromLatin1(line));

        // 只允许TCP收到的能力声明开启网络控制。
        if (tcpSource && handleGatewayLine(line))
            continue;

        BmsLogRecord record;
        if (!ParseBmsLog(line, record))
            continue;

        m_latestReport = record;
        m_haveReport = true;
        m_lastReport.start();

        showTelemetry(record);
        checkCommandResult(record);
        updateCommandButtons();
        appendRecording(record);
        ui->trendWidget->addRecord(record);
    }
}
bool MainWindow::hasFreshReport() const
{
    return m_haveReport && m_lastReport.isValid()
    && m_lastReport.elapsed() < 1000;
}

void MainWindow::updateCommandButtons()
{
    const bool connected = hasControlChannel();
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
    if (!BmsCommandAllowed(
            command,
            hasControlChannel(),
            hasFreshReport(),
            m_pendingCommand != BmsCommand::None,
            m_latestReport))
    {
        ui->commandLabel->setText(
            QStringLiteral("当前条件不允许此命令。"));
        return;
    }

    const bool tcpCommand = !m_serial.isOpen();
    quint32 requestId = 0;
    QByteArray data;

    if (tcpCommand)
    {
        // 编号一旦分配就不回退，失败或超时也不复用。
        requestId = static_cast<quint32>(m_nextTcpCommandId++);
        data = EncodeGatewayCommand(requestId, command);

        // 限制本机待发送数据，避免慢连接持续积压。
        if (m_tcp.bytesToWrite() + data.size() > 256)
        {
            ui->commandLabel->setText(
                QStringLiteral(
                    "TCP发送缓存繁忙，本次未提交；不自动重发。"));

            updateCommandButtons();
            return;
        }
    }
    else
    {
        // 直接串口模式仍然只发送原来的S/T/C单字节。
        data = QByteArray(1, static_cast<char>(command));
    }

    const quint64 epoch = m_connectionEpoch;

    // STOP可以替换本机之前等待的命令。
    // 之前命令的回执随后会因编号不匹配而被忽略。
    m_pendingCommand = command;
    m_pendingTcpId = requestId;
    m_gatewayAcked = false;
    m_commandWait.start();

    const qint64 written =
        tcpCommand ? m_tcp.write(data) : m_serial.write(data);

    if (written != data.size()
        || epoch != m_connectionEpoch
        || !isTransportConnected())
    {
        if (epoch == m_connectionEpoch)
            clearPendingCommand();

        // 出现不完整写入时结束连接，
        // 避免残留命令前缀与下一条命令拼接。
        if (tcpCommand)
            m_tcp.abort();

        ui->commandLabel->setText(
            QStringLiteral(
                "写入未完成或连接改变，结果未知；不自动重发。"));

        updateCommandButtons();
        return;
    }

    ui->commandLabel->setText(
        tcpCommand
            ? QStringLiteral(
                  "命令 #%1 已交给TCP，等待网关回执。")
                  .arg(requestId)
            : QStringLiteral("已交给串口，等待状态报告。"));

    if (tcpCommand && !GatewayIdValid(m_nextTcpCommandId))
    {
        ui->commandLabel->setText(
            ui->commandLabel->text()
            + QStringLiteral(
                " 编号已用尽，后续需手动重连。"));
    }

    updateCommandButtons();
}

void MainWindow::checkCommandResult(const BmsLogRecord &record)
{
    if (m_pendingCommand == BmsCommand::None)
        return;

    // 网络命令必须先收到对应ACK。
    if (m_pendingTcpId != 0 && !m_gatewayAcked)
        return;

    if (!BmsCommandTargetObserved(m_pendingCommand, record))
        return;

    const QString name =
        m_pendingTcpId != 0
            ? QStringLiteral("#%1").arg(m_pendingTcpId)
            : QString(QChar::fromLatin1(
                  static_cast<char>(m_pendingCommand)));

    clearPendingCommand();

    ui->commandLabel->setText(
        QStringLiteral(
            "已在后续报告中观察到目标状态（命令 %1）。")
            .arg(name));
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
    if (isTransportConnected()
        && m_lastReport.isValid()
        && m_lastReport.elapsed() >= 1000)
    {
        ui->telemetryLabel->setText(
            QStringLiteral(
                "数据过期：超过1秒没有收到完整BMS报告。"));

        m_lastReport.invalidate();
        m_haveReport = false;

        if (m_pendingCommand != BmsCommand::None)
        {
            clearPendingCommand();

            ui->commandLabel->setText(
                QStringLiteral(
                    "上报已过期，命令结果未知；不自动重发。"));
        }
    }

    if (m_pendingCommand != BmsCommand::None
        && m_commandWait.isValid()
        && m_commandWait.elapsed() >= 2000)
    {
        const bool waitingAck =
            m_pendingTcpId != 0 && !m_gatewayAcked;

        clearPendingCommand();

        ui->commandLabel->setText(
            waitingAck
                ? QStringLiteral(
                      "2秒内未收到对应网关回执，结果未知；不自动重发。")
                : QStringLiteral(
                      "2秒内未观察到目标状态，结果未知；不自动重发。"));
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
    const bool connected = isTransportConnected();
    const bool hadPending =
        m_pendingCommand != BmsCommand::None;

    ++m_connectionEpoch;

    m_lineBuffer.reset();
    ui->trendWidget->reset();
    m_haveReport = false;
    m_latestReport = BmsLogRecord{};
    clearPendingCommand();

    // 新连接不能继承上一个连接的控制能力和编号。
    m_tcpControlEnabled = false;
    m_nextTcpCommandId = 1;
    m_lastReport.invalidate();

    if (connected)
        m_lastReport.start();

    ui->telemetryLabel->setText(
        connected
            ? QStringLiteral("等待完整BMS上报……")
            : QStringLiteral("未连接"));

    if (hadPending)
    {
        ui->commandLabel->setText(
            QStringLiteral("连接状态改变，上一命令结果未确认。"));
    }
    else if (m_tcp.state() != QAbstractSocket::UnconnectedState)
    {
        ui->commandLabel->setText(
            QStringLiteral("TCP已连接，等待控制能力声明；当前只读。"));
    }
    else
    {
        ui->commandLabel->setText(
            connected
                ? QStringLiteral("等待指令。")
                : QStringLiteral("未连接"));
    }

    if (!connected)
        stopRecording();

    updateConnectionControls();
}

void MainWindow::toggleRecording()
{
    if (m_csvLogger.isRecording())
    {
        stopRecording();
        return;
    }

    if (!isTransportConnected())
    {
        ui->recordLabel->setText(QStringLiteral("请先连接串口或TCP。"));
        return;
    }

    const quint64 epoch = m_connectionEpoch;

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

    // 选文件期间连接可能变化，旧操作不能用于新会话。
    if (!isTransportConnected() || epoch != m_connectionEpoch)
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
        recording || isTransportConnected());
}

bool MainWindow::isTransportConnected() const
{
    return m_serial.isOpen()
    || m_tcp.state() == QAbstractSocket::ConnectedState;
}

void MainWindow::toggleTcpConnection()
{
    if (m_serial.isOpen())
    {
        ui->rxText->appendPlainText(
            QStringLiteral("[TCP] 请先断开串口连接。"));
        return;
    }

    // 已连接时断开；正在连接时取消。
    if (m_tcp.state() != QAbstractSocket::UnconnectedState)
    {
        m_tcpConnectTimer.stop();
        m_tcp.abort();
        updateConnectionUi();
        return;
    }

    QHostAddress address;
    bool portOk = false;
    const uint port =
        ui->tcpPortEdit->text().trimmed().toUInt(&portOk);

    if (!address.setAddress(ui->tcpHostEdit->text().trimmed())
        || address.protocol() != QAbstractSocket::IPv4Protocol
        || !portOk || port == 0 || port > 65535)
    {
        ui->rxText->appendPlainText(
            QStringLiteral("[TCP] 请填写有效IPv4地址和1～65535端口。"));
        return;
    }

    // 手动开始新会话，清理旧数据。
    updateConnectionUi();

    ui->rxText->appendPlainText(
        QStringLiteral("[TCP] 尝试连接 %1:%2")
            .arg(address.toString())
            .arg(port));

    m_tcpConnectTimer.start();
    m_tcp.connectToHost(address, static_cast<quint16>(port));

    updateConnectionControls();
}

void MainWindow::readTcpData()
{
    const QByteArray data = m_tcp.readAll();

    if (!m_serial.isOpen())
        processIncomingData(data, true);
}

bool MainWindow::handleGatewayLine(const QByteArray &line)
{
    GatewayMessage message;
    if (!ParseGatewayMessage(line, message))
        return false;

    if (m_tcp.state() != QAbstractSocket::ConnectedState)
        return true;

    if (message.type == GatewayMessageType::Capability)
    {
        // 重复声明不能重置编号或正在等待的命令。
        if (!m_tcpControlEnabled)
        {
            m_tcpControlEnabled = true;

            if (m_pendingCommand == BmsCommand::None)
            {
                ui->commandLabel->setText(
                    QStringLiteral(
                        "网关支持控制；启动和清除仍需新鲜状态。"));
            }
        }

        updateConnectionControls();
        return true;
    }

    // 无法关联的回执只保留在日志中。
    // 尤其不能用旧编号回执结束新的STOP等待项。
    if (!m_tcpControlEnabled || m_pendingTcpId == 0
        || message.id != m_pendingTcpId)
    {
        return true;
    }

    if (message.type == GatewayMessageType::UartAccepted)
    {
        if (!m_gatewayAcked)
        {
            m_gatewayAcked = true;

            // 从收到ACK开始，另行计时观察后续状态。
            // 重复ACK不会重新计时。
            m_commandWait.start();

            ui->commandLabel->setText(
                QStringLiteral(
                    "命令 #%1 已提交串口，等待后续状态报告。")
                    .arg(message.id));
        }
    }
    else if (message.type == GatewayMessageType::Error)
    {
        clearPendingCommand();

        ui->commandLabel->setText(
            QStringLiteral("命令 #%1 返回错误：%2")
                .arg(message.id)
                .arg(QString::fromLatin1(message.reason)));

        updateCommandButtons();
    }

    return true;
}

bool MainWindow::hasControlChannel() const
{
    return m_serial.isOpen()
           || (m_tcp.state() == QAbstractSocket::ConnectedState
               && m_tcpControlEnabled
               && GatewayIdValid(m_nextTcpCommandId));
}

void MainWindow::clearPendingCommand()
{
    m_pendingCommand = BmsCommand::None;
    m_pendingTcpId = 0;
    m_gatewayAcked = false;
    m_commandWait.invalidate();
}

void MainWindow::handleTcpError(QAbstractSocket::SocketError error)
{
    ui->rxText->appendPlainText(
        QStringLiteral("[TCP] %1").arg(m_tcp.errorString()));

    // 网关到时结束也会关闭连接，随后由disconnected清理。
    if (error == QAbstractSocket::RemoteHostClosedError)
        return;

    m_tcpConnectTimer.stop();
    m_tcp.abort();
    updateConnectionUi();
}

void MainWindow::updateConnectionControls()
{
    const bool serial = m_serial.isOpen();
    const bool tcpBusy =
        m_tcp.state() != QAbstractSocket::UnconnectedState;
    const bool tcpConnected =
        m_tcp.state() == QAbstractSocket::ConnectedState;

    // 两种通道互斥，避免混用同一个组帧缓存。
    ui->connectButton->setEnabled(!tcpBusy);
    ui->connectButton->setText(serial ? "Disconnect" : "Connect");
    ui->portCombo->setEnabled(!serial && !tcpBusy);
    ui->refreshButton->setEnabled(!serial && !tcpBusy);

    ui->tcpHostEdit->setEnabled(!serial && !tcpBusy);
    ui->tcpPortEdit->setEnabled(!serial && !tcpBusy);
    ui->tcpConnectButton->setEnabled(!serial);

    ui->tcpConnectButton->setText(
        tcpConnected
            ? QStringLiteral("断开TCP")
            : (tcpBusy
                   ? QStringLiteral("取消连接")
                   : QStringLiteral("连接TCP")));

    if (serial)
    {
        ui->statusLabel->setText(
            m_serial.portName() + " 115200 8N1");
    }
    else if (tcpConnected)
    {
        ui->statusLabel->setText(
        QStringLiteral("TCP %1:%2（%3）")
            .arg(m_tcp.peerAddress().toString())
            .arg(m_tcp.peerPort())
            .arg(m_tcpControlEnabled
                    ? QStringLiteral("可控制")
                    : QStringLiteral("只读")));
    }
    else if (tcpBusy)
    {
        ui->statusLabel->setText(
            QStringLiteral("TCP连接中……"));
    }
    else
    {
        ui->statusLabel->setText(
            QStringLiteral("未连接"));
    }

    // 这里只刷新控件，不清空报告或趋势。
    updateCommandButtons();
    updateRecordingUi();
}
