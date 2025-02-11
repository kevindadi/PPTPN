#include "mainwindow.h"
#include "./ui_mainwindow.h"
#include <QMessageBox>
#include <QProcess>
#include <QStandardPaths>
#include <QFile>
#include <QTextStream>
#include <QRegularExpression>
#include <QDebug>
#include <QDir>
#include "analysis.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::on_parse_tdg_clicked()
{
    TDG new_tdg;
    QString content = ui->tdg_config->toPlainText().replace("\n", "");
    if (content.isEmpty()) {
        QMessageBox::warning(this, "错误", "输入不能为空!");
        return;
    }
    content.remove(QChar('\\'), Qt::CaseInsensitive);

    std::string uft8_content = content.toStdString();
    try {
        new_tdg.parse_tdg_from_string(std::move(uft8_content));
        new_tdg.classify_priority();
        tdg = std::move(new_tdg);
    } catch (const LabelParseException& ex) {
        QMessageBox::critical(this, "解析错误", QString::fromStdString(ex.what()));
    } catch (const TimeValueException& ex) {
        QMessageBox::critical(this, "时间值错误", QString::fromStdString(ex.what()));
    } catch (const std::exception& ex) {
        QMessageBox::critical(this, "未知错误", "发生未知错误: " + QString::fromStdString(ex.what()));
    }
}

// 生成 Petri 网
void MainWindow::on_gen_pn_clicked()
{
    PriorityTimePetriNet new_ptpn;
    int cpu_nums = ui->cpu_nums->text().toInt();
    if (cpu_nums <= 0) {
        QMessageBox::warning(this, "CPU数量", "不能为 0");
        return;
    }else {
        tdg.core = cpu_nums;
    }
    QStringList pn_info = new_ptpn.transform_tdg_to_ptpn(tdg);
    QTextEdit *pn_info_l = ui->pn_infos;
    pn_info_l->clear();
    for (const QString &info : pn_info) {
        pn_info_l->append(info);
    }
    pn_file_path = pn_info.last();
    ptpn = std::move(new_ptpn);
}


void MainWindow::on_open_pn_clicked()
{
    QDir currentDir = QDir::current();

    QString dotFilename = "ptpn_graph.dot";
    QString pngFilename = "ptpn_graph.png";

    QString dotFilePath = currentDir.filePath(dotFilename);
    QString pngFilePath = currentDir.filePath(pngFilename);

    if (!QFile::exists(dotFilePath)) {
        QMessageBox::critical(nullptr, "错误", "请先生成 Petri 网");
        return;
    }

    QFile file(dotFilePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::critical(nullptr, "错误", "无法创建 DOT 文件！");
        return;
    }

    // 调用 dot 命令将 DOT 文件转换为 PNG 图像
    QString dotCommand = QString("dot -Tpng %1 -o %2").arg(dotFilename).arg(pngFilename);
    qDebug() << dotCommand;
    QProcess process;
    process.setWorkingDirectory(currentDir.path());
    process.start(dotCommand);
    process.waitForFinished();

    // 检查是否成功生成 PNG 文件
    if (QFile::exists(pngFilePath)) {
        // 使用系统自带的图片查看工具打开 PNG 文件 (默认: MAC)
        QProcess::startDetached("open", QStringList() << pngFilePath);
        // 如果是 Windows：
        // QProcess::startDetached("explorer", QStringList() << pngFilePath);
        // 如果是 Linux:
        // QProcess::startDetached("xdg-open", QStringList() << pngFilePath);
    } else {
        QMessageBox::critical(nullptr, "错误", "生成 PNG 文件失败！");
    }
}


void MainWindow::on_gen_scg_clicked()
{
    StateClassGraph new_scg(ptpn.ptpn);
    QStringList scg_info = new_scg.generate_state_class();
    QTextEdit *scg_info_l = ui->scg_infos;
    scg_info_l->clear();
    for (const QString &info : scg_info) {
        scg_info_l->append(info);
    }
    scg_file_path = scg_info.last();
    // scg 移动构造
    scg = std::move(new_scg.scg);
}


void MainWindow::on_open_scg_clicked()
{
    QDir currentDir = QDir::current();

    QString dotFilename = "scg_graph.dot";
    QString pngFilename = "scg_graph.png";

    QString dotFilePath = currentDir.filePath(dotFilename);
    QString pngFilePath = currentDir.filePath(pngFilename);

    if (!QFile::exists(dotFilePath)) {
        QMessageBox::critical(nullptr, "错误", "请先生成状态类网");
        return;
    }

    QFile file(dotFilePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::critical(nullptr, "错误", "无法创建 DOT 文件！");
        return;
    }

    // 调用 dot 命令将 DOT 文件转换为 PNG 图像
    QString dotCommand = QString("dot -Tpng %1 -o %2").arg(dotFilename).arg(pngFilename);
    qDebug() << dotCommand;
    QProcess process;
    process.setWorkingDirectory(currentDir.path());
    process.start(dotCommand);
    process.waitForFinished();

    // 检查是否成功生成 PNG 文件
    if (QFile::exists(pngFilePath)) {
        // 使用系统自带的图片查看工具打开 PNG 文件 (默认: MAC)
        QProcess::startDetached("open", QStringList() << pngFilePath);
        // 如果是 Windows：
        // QProcess::startDetached("explorer", QStringList() << pngFilePath);
        // 如果是 Linux:
        // QProcess::startDetached("xdg-open", QStringList() << pngFilePath);
    } else {
        QMessageBox::critical(nullptr, "错误", "生成 PNG 文件失败！");
    }
}


void MainWindow::on_wcet_clicked()
{
    QString task_name = ui->task_name->text();
    if (task_name.isEmpty()) {
        QMessageBox::warning(this, "警告", "未指定任务");
        return;
    }

    //int task_wcet = task_wcet();
}


void MainWindow::on_wcrt_clicked()
{
    QString task_input = ui->task_name->text();

    qDebug() << "计算任务" << task_input << "的 wcrt";
    QRegularExpression regex("^(\\S+)\\s*到\\s*(\\S+)$");
    QRegularExpression simpleRegex("^(\\S+)$");

    QRegularExpressionMatch match = regex.match(task_input);
    if (match.hasMatch()) {
        // 如果匹配 "A 到 B" 格式
        QString first = match.captured(1);
        QString second = match.captured(2);
        qDebug() << first << " to " << second;
        int wcet = task_wcet(scg, first.toStdString(), second.toStdString());
        ui->wcet_infos->append("任务: " + first + " 到" + "_任务: " + second + " 的 wcet 为" + QString::number(wcet));
        return;
    }

    match = simpleRegex.match(task_input);
    if (match.hasMatch()) {
        // 如果匹配 "A" 格式
        QString first = match.captured(1);
        qDebug() << first;
        int wcet = task_wcet(scg, first.toStdString(), first.toStdString());
        ui->wcet_infos->append("任务: " + first + " 的 wcet 为" + QString::number(wcet));
        return;
    }

    QMessageBox::critical(nullptr, "错误", "format:A to B");

}


void MainWindow::on_check_deadlock_clicked()
{
    std::vector<std::string> deadlock_info = check_deadlock(scg);
    QTextEdit *text_edit = ui->deadlock_infos;
    for (const std::string& s : deadlock_info) {
        text_edit->append(QString::fromStdString(s));
    }
}

