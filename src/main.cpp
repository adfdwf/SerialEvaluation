#include "mainwindow.h"

#include <QApplication>

/**
 * @brief  main 应用程序入口函数
 * @param  argc 命令行参数数量
 * @param  argv 命令行参数数组
 * @return int Qt事件循环返回码
 */
int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("CommBench Pro");
    QApplication::setOrganizationName("Industrial CommBench");

    Ciqtek::MainWindow window;
    window.show();

    return app.exec();
}