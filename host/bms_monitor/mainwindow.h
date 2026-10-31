#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QSerialPort>
#include <QElapsedTimer>
#include <QTimer>
#include "bms_log_parser.h"
#include "bms_commands.h"
#include "bms_csv_logger.h"

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
    QElapsedTimer m_commandWait;
    Ui::MainWindow *ui;
    // 串口属于这个窗口，关闭窗口时会自动析构。
    QSerialPort m_serial;
    BmsLineBuffer m_lineBuffer;
    QElapsedTimer m_lastReport;
    QTimer m_reportTimer;
};

#endif // MAINWINDOW_H