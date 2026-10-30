#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QSerialPort>

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

    Ui::MainWindow *ui;

    // 串口属于这个窗口，关闭窗口时会自动析构。
    QSerialPort m_serial;
};

#endif // MAINWINDOW_H