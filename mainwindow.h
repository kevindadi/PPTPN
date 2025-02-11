#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include "clap.h"
#include "priority_pn.h"
#include "scg.h"

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

public:
    TDG tdg;
    PriorityTimePetriNet ptpn;

    // Petri网临时文件位置,统一命名为ptpn_graph.dot
    QString pn_file_path;
    QString scg_file_path;
    SCG scg;

    int cpu_nums = 0;
    int cpu_cores = 1;

private slots:
    void on_parse_tdg_clicked();

    void on_gen_pn_clicked();

    void on_open_pn_clicked();

    void on_gen_scg_clicked();

    void on_open_scg_clicked();

    void on_wcet_clicked();

    void on_wcrt_clicked();

    void on_check_deadlock_clicked();

private:
    Ui::MainWindow *ui;
};
#endif // MAINWINDOW_H
