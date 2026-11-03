#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QSerialPort>
#include <QElapsedTimer>
#include <QTimer>
#include "bms_log_parser.h"
#include "bms_commands.h"
#include "bms_csv_logger.h"
#include <QTcpSocket>
#include "bms_gateway_protocol.h"

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private:
    // 分别负责端口列表、连接、接收和错误处理。
    void refreshPorts();
    void toggleConnection();
    void readSerialData();
    void processIncomingData(const QByteArray &data, bool tcpSource = false);
    bool handleGatewayLine(const QByteArray &line);
    bool hasControlChannel() const;
    void clearPendingCommand();
    void toggleTcpConnection();
    void readTcpData();
    void handleTcpError(QAbstractSocket::SocketError error);
    bool isTransportConnected() const;
    void updateConnectionControls();
    void handleSerialError(QSerialPort::SerialPortError error);
    void updateConnectionUi();
    void showTelemetry(const BmsLogRecord &record);
    void checkReportTimeout();
    bool hasFreshReport() const;
    void updateCommandButtons();
    void sendCommand(BmsCommand command);
    void checkCommandResult(const BmsLogRecord &record);
    void toggleRecording();
    void appendRecording(const BmsLogRecord &record);
    void stopRecording();
    void updateRecordingUi();

    BmsCsvLogger m_csvLogger;
    BmsLogRecord m_latestReport;
    bool m_haveReport = false;
    BmsCommand m_pendingCommand = BmsCommand::None;
    // 每个TCP连接都必须重新取得控制能力。
    bool m_tcpControlEnabled = false;

    // 使用64位计数，超过32位上限后停止分配，避免回绕。
    quint64 m_nextTcpCommandId = 1;

    // 当前正在等待的TCP命令编号；0表示没有TCP等待项。
    quint32 m_pendingTcpId = 0;

    // 收到对应ACK后，才开始观察后续BMS目标状态。
    bool m_gatewayAcked = false;
    QElapsedTimer m_commandWait;
    Ui::MainWindow *ui;
    // 串口属于这个窗口，关闭窗口时会自动析构。
    QSerialPort m_serial;
    QTcpSocket m_tcp;
    QTimer m_tcpConnectTimer;

    // 判断选文件期间是否已经换了连接。
    quint64 m_connectionEpoch = 0;
    BmsLineBuffer m_lineBuffer;
    QElapsedTimer m_lastReport;
    QTimer m_reportTimer;
};

#endif // MAINWINDOW_H