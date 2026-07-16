#include "mainwindow.h"

#include <QApplication>

/**
 * @brief 应用程序入口函数。
 * @param argc 命令行参数数量。
 * @param argv 命令行参数数组。
 * @return Qt 事件循环结束后的返回码。
 */
int main(int argc, char *argv[])
{
    // 创建 Qt 应用对象，负责初始化 GUI 和事件循环。
    QApplication app(argc, argv);
    // 设置应用名称，供窗口系统和配置系统识别。
    QApplication::setApplicationName("CommBench Pro");
    // 设置组织名称，供 Qt 配置存储使用。
    QApplication::setOrganizationName("Industrial CommBench");

    // 创建主窗口并显示客户端界面。
    Ciqtek::MainWindow window;
    window.show();

    // 进入 Qt 事件循环，直到用户关闭窗口。
    return app.exec();
}
