#define WIN32_LEAN_AND_MEAN
#include <iostream>
#include <thread>
#include <filesystem>
#include <map>
#include <mutex>
#include <QtCore/QtCore>
#include <QtWidgets/QtWidgets>
// #include <QtCore5Compat/QTextCodec>
#include "cpplibs/ssocket.hpp"
#include "cpplibs/strlib.hpp"
#include "cpplibs/libjson.hpp"
#include "cpplibs/libbase64.hpp"
using namespace std;
namespace fs = filesystem;

string server_ip = "";
int server_port = 9723;

class ConnectWindow : public QMainWindow {

};

class AppWindow : public QMainWindow {
    QPushButton* selectFileBtn = nullptr;
    QPushButton* sendBtn = nullptr;
    QTableWidget* lst = nullptr;
    QComboBox* addrLst = nullptr;
    QLabel* selectedFile = nullptr;

    QDialog* confirmDialog = nullptr;
    QDialogButtonBox* confirmButtons = nullptr;
    QLabel* confirmMessage = nullptr;
    QLabel* confirmName = nullptr;
    QLabel* confirmSize = nullptr;
    QLabel* confirmFrom = nullptr;

    Socket sock;
    Json json;

    fs::path currentFile;

    map<string, ofstream*> rfiles;
    map<string, ifstream*> sfiles;

    string currentSaveName;

    mutex fileMtx;

    // void resizeEvent(QResizeEvent* event) {
    //     btn1->resize(event->size());
    // }
    void initTable() {
        lst = new QTableWidget(0, 4, this);
        lst->setGeometry(60, 110, 680, 270);
        lst->setShowGrid(false);

        lst->setColumnWidth(0, lst->width() / 4 - 1);
        lst->setColumnWidth(1, lst->width() / 4 - 1);
        lst->setColumnWidth(2, lst->width() / 4 - 1);
        lst->setColumnWidth(3, lst->width() / 4 - 1);

        // lst->setItem(0, 0, new QTableWidgetItem("Hello World"));
        lst->setHorizontalHeaderLabels({ "Name", "Progress", "State", "Speed" });

        lst->verticalHeader()->hide();
        // lst->setItem(1, 1, new QProgressBar());
        // lst->setCellWidget(0, 1, new QProgressBar(this));
        // ((QProgressBar*)lst->cellWidget(0, 1))->setValue(60);
    }

    void initTransferDialog() {
        confirmDialog = new QDialog(this);
        confirmDialog->setFixedSize(400, 300);
        confirmDialog->setWindowTitle("Confirmation");

        confirmButtons = new QDialogButtonBox(QDialogButtonBox::Yes | QDialogButtonBox::No, confirmDialog);
        confirmButtons->setGeometry(30, 240, 341, 50);
        // confirmButtons->show();

        confirmMessage = new QLabel(confirmDialog);
        confirmMessage->move(30, 30);
        confirmMessage->setText("Do you want to accept this file?");
        confirmMessage->adjustSize();

        confirmName = new QLabel(confirmDialog);
        confirmName->move(30, 50);

        confirmSize = new QLabel(confirmDialog);
        confirmSize->move(30, 70);

        confirmFrom = new QLabel(confirmDialog);
        confirmFrom->move(30, 90);

        connect(confirmButtons, &QDialogButtonBox::accepted, confirmDialog, [this]() {
            fileMtx.lock();
            confirmDialog->accept();
            currentSaveName = QFileDialog::getSaveFileName(this, tr("Save file"), QString::fromStdString(currentSaveName)).toStdString();
            fileMtx.unlock();

            cout << "out confirm" << endl;
            });

        connect(confirmButtons, &QDialogButtonBox::rejected, confirmDialog, &QDialog::reject);
    }

    // void addPeer(string str) {

    // }

    // Q_INVOKABLE
    // QString getSaveFileName_(QWidget* parent, const QString& caption, const QString& dir,
    //                         const QString& filter) {
    //     return QFileDialog::getSaveFileName(parent, caption, dir, filter);
    // }

    // QString getSaveFileName(QWidget* parent, const QString& caption, const QString& dir = "", const QString& filter = "") {
    //     QString fileName;

    //     if (QThread::currentThread() != qApp->thread()) { // no GUI thread
    //     QMetaObject::invokeMethod(this, "getSaveFileName_", Qt::BlockingQueuedConnection,
    //                                 Q_RETURN_ARG(QString, fileName),
    //                                 Q_ARG(QWidget*, parent),
    //                                 Q_ARG(QString, caption),
    //                                 Q_ARG(QString, dir),
    //                                 Q_ARG(QString, filter));
    //     } else { // in GUI thread, direct call
    //         fileName = getSaveFileName_(parent, caption, dir, filter);
    //     }

    //     return fileName;
    // }

    void handler() {
        try { sock.open(AF_INET, SOCK_STREAM); }
        catch (int e) { cout << e << endl; }
        sock.connect(server_ip, server_port);

        JsonNode node;
        node.addPair("cmd", "cl_list");
        sock.send(json.dump(node));

        node = json.parse(sock.recv(8192).string);
        for (auto i : node["clients"].array) addrLst->insertItem(addrLst->count(), QString::fromStdString(i.str));

        while (true) {
            sockrecv_t data = sock.recv(4096);

            if (!data.size) break;

            cout << data.string << endl;

            JsonNode node = json.parse(data.string);

            cout << "here" << endl;

            if (node["cmd"].str == "client_connected")
                addrLst->insertItem(addrLst->count(), QString::fromStdString(node["address"].str));

            else if (node["cmd"].str == "client_disconnected") {
                addrLst->setCurrentIndex(0);
                addrLst->removeItem(addrLst->findText(QString::fromStdString(node["address"].str)));
            }

            else if (node["cmd"].str == "transfer_request") {
                confirmName->setText("Filename: " + QString::fromStdString(node["filename"].str));
                confirmName->adjustSize();

                confirmSize->setText("Size: " + QString::fromStdString(node["filesize"].str));
                confirmSize->adjustSize();

                confirmFrom->setText("From: " + QString::fromStdString(node["from"].str));
                confirmFrom->adjustSize();

                JsonNode node1;

                currentSaveName = node["filename"].str;

                if (confirmDialog->exec()) {
                    cout << "here" << endl;
                    node1.addPair("cmd", "accept_transfer_request");
                    node1.addPair("to", node["from"].str);

                    fileMtx.lock();
                    rfiles[node["from"].str] = new ofstream(currentSaveName, ios::binary);
                    fileMtx.unlock();
                }
                else {
                    node1.addPair("cmd", "discard_transfer_request");
                    node1.addPair("to", node["from"].str);
                }

                sock.send(json.dump(node1));
            }
            // askForTransfer(, node["filesize"].str, node["from"].str);

        }
    }
public:
    AppWindow(QWidget* parent = nullptr) : QMainWindow(parent) {
        this->resize(800, 450);

        selectFileBtn = new QPushButton(this);
        selectFileBtn->setGeometry(60, 20, 100, 34);
        selectFileBtn->setText("Select file");

        selectedFile = new QLabel(this);
        selectedFile->move(60, 70);

        sendBtn = new QPushButton(this);
        sendBtn->setGeometry(640, 20, 100, 34);
        sendBtn->setText("Send");

        addrLst = new QComboBox(this);
        addrLst->setGeometry(360, 20, 271, 34);
        addrLst->insertItem(0, "Select address...");

        initTable();
        initTransferDialog();
        // lst->setcol;
        // lst->setModelColumn(50);
        // lst->setModel(model);


        connect(selectFileBtn, &QPushButton::clicked, [this]() {
            currentFile = QFileDialog::getOpenFileName(this, tr("Open file")).toUtf8().toStdString();
            selectedFile->setText(QString::fromStdString(currentFile.string()));
            selectedFile->adjustSize();
            });

        connect(sendBtn, &QPushButton::clicked, [this]() {
            if (!fs::exists(currentFile) || !addrLst->currentIndex()) return;

            JsonNode node;
            node.addPair("cmd", "transfer_request");
            node.addPair("to", addrLst->currentText().toStdString());
            node.addPair("filename", currentFile.filename().string());
            node.addPair("filesize", to_string(fs::file_size(currentFile)));

            sock.send(json.dump(node));
            });

        std::thread(&AppWindow::handler, this).detach();
    }
};

int main(int argc, char** argv) {
    QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

    //QTextCodec::setCodecForLocale(QTextCodec::codecForName("UTF-8"));

    QApplication app(argc, argv);

    AppWindow* window = new AppWindow;
    window->show();

    return app.exec();
}
