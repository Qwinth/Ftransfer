#define WIN32_LEAN_AND_MEAN
#include <iostream>
#include <filesystem>
#include <map>
#include <utility>
#include <chrono>
#include "cpplibs/ssocket.hpp"
#include "cpplibs/strlib.hpp"
#include "cpplibs/libjson.hpp"
#include "cpplibs/libbase64.hpp"

// #include <QtCore/QApplication>
#include <QtWidgets/QApplication>
#include <QtWidgets/QMainWindow>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QProgressBar>
#include <QtWidgets/QTableWidget>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QDialog>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QFileDialog>
// #include <QtGui/QKeyEvent>
#include <QtCore/QTimer>

using namespace std;
namespace fs = filesystem;

Socket sock;

int server_port = 9723;

size_t buffer_size = 256 * 1024;

uint64_t math_map(uint64_t x, uint64_t in_min, uint64_t in_max, uint64_t out_min, uint64_t out_max) {
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

int64_t timems() {
    return chrono::duration_cast<chrono::milliseconds>(chrono::system_clock::now().time_since_epoch()).count();
}

double round_up(double value, int decimal_places) {
    const double multiplier = std::pow(10.0, decimal_places);
    return std::ceil(value * multiplier) / multiplier;
}

struct FileDesc {
    fstream* file;
    size_t size;
    QProgressBar* progress;
    int lstrow;
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

    QTimer* socktimer = nullptr;

    Base64 base64;
    Json json;

    map<string, map<string, FileDesc>> wfiles;
    map<string, map<string, FileDesc>> rfiles;

    fs::path currentFilePath;

    int64_t tstart;

    void initTable() {
        lst = new QTableWidget(0, 4, this);
        lst->setGeometry(60, 110, 680, 270);
        lst->setShowGrid(false);
        lst->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);

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

        connect(confirmButtons, &QDialogButtonBox::accepted, confirmDialog, &QDialog::accept);
        connect(confirmButtons, &QDialogButtonBox::rejected, confirmDialog, &QDialog::reject);
    }

    void getClients() {
        JsonNode node;
        node.addPair("cmd", "cl_list");
        sock.sendmsg(json.dump(node));

        cout << "here" << endl;

        node = json.parse(sock.recvmsg().string);
        for (auto i : node["clients"].array) addrLst->insertItem(addrLst->count(), QString::fromStdString(i.str));

        sock.setblocking(false);
    }

    void handler() {
        sockrecv_t data = sock.recvmsg();
        if (!data.size) return;

        JsonNode node = json.parse(data.string);

        if (node["cmd"].str == "client_connected")
            addrLst->insertItem(addrLst->count(), QString::fromStdString(node["address"].str));

        else if (node["cmd"].str == "client_disconnected") {
            addrLst->setCurrentIndex(0);
            addrLst->removeItem(addrLst->findText(QString::fromStdString(node["address"].str)));

            if (rfiles.find(node["address"].str) != rfiles.end()) {
                for (auto [name, file] : rfiles[node["address"].str]) {
                    cout << "Delete rfile fd: " << node["address"].str << " " << name << endl;

                    file.file->close();
                    delete file.file;
                }

                rfiles.erase(node["address"].str);
            }

            if (wfiles.find(node["address"].str) != wfiles.end()) {
                for (auto [name, file] : wfiles[node["address"].str]) {
                    cout << "Delete wfile fd: " << node["address"].str << " " << name << endl;

                    file.file->close();
                    delete file.file;
                }

                wfiles.erase(node["address"].str);
            }
        }

        else if (node["cmd"].str == "transfer_request") {
            confirmName->setText("Filename: " + QString::fromStdString(node["filename"].str));
            confirmName->adjustSize();

            confirmSize->setText("Size: " + QString::number(node["filesize"].integer));
            confirmSize->adjustSize();

            confirmFrom->setText("From: " + QString::fromStdString(node["from"].str));
            confirmFrom->adjustSize();

            JsonNode node1;

            if (confirmDialog->exec()) {
                node1.addPair("cmd", "accept_transfer_request");
                node1.addPair("filename", node["filename"].str);
                node1.addPair("to", node["from"].str);

                string currentSaveName = QFileDialog::getSaveFileName(this, tr("Save file"), QString::fromStdString(node["filename"].str)).toStdString();

                lst->insertRow(lst->rowCount());
                lst->setItem(lst->rowCount() - 1, 0, new QTableWidgetItem(QString::fromStdString(node["filename"].str)));
                lst->setItem(lst->rowCount() - 1, 2, new QTableWidgetItem("Download"));
                lst->setItem(lst->rowCount() - 1, 3, new QTableWidgetItem("0Mb/s"));
                lst->setCellWidget(lst->rowCount() - 1, 1, new QProgressBar(this));
                ((QProgressBar*)lst->cellWidget(lst->rowCount() - 1, 1))->setValue(0);

                lst->item(lst->rowCount() - 1, 2)->setTextAlignment(Qt::AlignCenter);
                lst->item(lst->rowCount() - 1, 3)->setTextAlignment(Qt::AlignCenter);
#ifdef _WIN32
                wfiles[node["from"].str][node["filename"].str].file = new fstream(str2wstr(currentSaveName), ios::binary | ios::out);
#else
                wfiles[node["from"].str][node["filename"].str].file = new fstream(currentSaveName, ios::binary | ios::out);
#endif
                wfiles[node["from"].str][node["filename"].str].size = node["filesize"].integer;
                wfiles[node["from"].str][node["filename"].str].progress = (QProgressBar*)lst->cellWidget(lst->rowCount() - 1, 1);
                wfiles[node["from"].str][node["filename"].str].lstrow = lst->rowCount() - 1;
            }

            else {
                node1.addPair("cmd", "discard_transfer_request");
                node1.addPair("filename", node["filename"].str);
                node1.addPair("to", node["from"].str);
            }

            sock.sendmsg(json.dump(node1));
        }

        else if (node["cmd"].str == "transfer_packet") {
            FileDesc currentFile = wfiles[node["from"].str][node["filename"].str];

            lst->item(currentFile.lstrow, 3)->setText(QString::number(round_up(node["true_size"].integer / (double)(timems() - tstart) / 1000, 2)) + "Mb/s");
            tstart = timems();

            size_t size = node["true_size"].integer;
            char* buffer = new char[size];

            base64.decode(node["data"].str.c_str(), buffer, node["data"].str.size());
            currentFile.file->write(buffer, size);

            if (currentFile.size) currentFile.progress->setValue(math_map((currentFile.file->tellg() > 0) ? (size_t)currentFile.file->tellg() : currentFile.size, 0, currentFile.size, 0, 100));
            else currentFile.progress->setValue(100);

            if (node["eof"].boolean) {
                currentFile.file->close();

                delete currentFile.file;
                wfiles[node["from"].str].erase(node["filename"].str);

                lst->item(currentFile.lstrow, 2)->setText("Downloaded");
                lst->item(currentFile.lstrow, 2)->setForeground(Qt::green);

                cout << "Delete wfile fd: " << node["from"].str << " " << node["filename"].str << endl;
            }

            else {
                JsonNode node1;
                node1.addPair("cmd", "packet_received");
                node1.addPair("filename", node["filename"].str);
                node1.addPair("to", node["from"].str);

                sock.sendmsg(json.dump(node1));
            }

            delete[] buffer;
        }

        else if (node["cmd"].str == "packet_received" || node["cmd"].str == "transfer_accept") {
            FileDesc currentFile = rfiles[node["from"].str][node["filename"].str];

            char* buffer = new char[buffer_size];
            char* encbuffer = new char[base64.calculateEncodedSize(buffer_size)];

            currentFile.file->read(buffer, buffer_size);
            size_t gcount = currentFile.file->gcount();

            lst->item(currentFile.lstrow, 3)->setText(QString::number(round_up(gcount / (double)(timems() - tstart) / 1000, 2)) + "Mb/s");
            tstart = timems();

            base64.encode(buffer, encbuffer, gcount);

            bool eof = gcount < buffer_size || !gcount;

            cout << "Sending packet: " << node["filename"].str << " to: " << node["from"].str << endl;

            if (currentFile.size) currentFile.progress->setValue(math_map((currentFile.file->tellg() > 0) ? (size_t)currentFile.file->tellg() : currentFile.size, 0, currentFile.size, 0, 100));
            else currentFile.progress->setValue(100);

            if (eof) {
                currentFile.file->close();

                delete currentFile.file;
                rfiles[node["from"].str].erase(node["filename"].str);

                lst->item(currentFile.lstrow, 2)->setText("Uploaded");
                lst->item(currentFile.lstrow, 2)->setForeground(Qt::green);

                cout << "Delete rfile fd: " << node["from"].str << " " << node["filename"].str << endl;
            }

            JsonNode node1;
            node1.addPair("cmd", "transfer_packet");
            node1.addPair("to", node["from"].str);
            node1.addPair("filename", node["filename"].str);
            node1.addPair("data", string(encbuffer, base64.calculateEncodedSize(gcount)));
            node1.addPair("true_size", (long)gcount);
            node1.addPair("eof", eof);

            sock.sendmsg(json.dump(node1));
            cout << json.dump(node1).size() << endl;

            delete[] buffer;
            delete[] encbuffer;
        }

        else if (node["cmd"].str == "transfer_discard") {


        }
    }
public:
    AppWindow(QWidget* parent = nullptr) : QMainWindow(parent) {
        this->setFixedSize(800, 450);

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

        socktimer = new QTimer(this);

        initTable();
        initTransferDialog();
        getClients();

        connect(socktimer, &QTimer::timeout, this, &AppWindow::handler);

        connect(selectFileBtn, &QPushButton::clicked, [this]() {
#ifdef _WIN32
            currentFilePath = str2wstr(QFileDialog::getOpenFileName(this, tr("Open file")).toStdString());
            selectedFile->setText(QString::fromStdString(wstr2str(currentFilePath.wstring())));
#else       
            currentFilePath = QFileDialog::getOpenFileName(this, tr("Open file")).toStdString();
            selectedFile->setText(QString::fromStdString(currentFilePath.string()));
#endif
            selectedFile->adjustSize();
            });

        connect(sendBtn, &QPushButton::clicked, [this]() {
            if (!fs::exists(currentFilePath) || !addrLst->currentIndex()) return;

            JsonNode node;
            node.addPair("cmd", "transfer_request");
            node.addPair("to", addrLst->currentText().toStdString());
#ifdef _WIN32
            node.addPair("filename", wstr2str(currentFilePath.filename().wstring()));
#else
            node.addPair("filename", currentFilePath.filename().string());
#endif
            node.addPair("filesize", (long)fs::file_size(currentFilePath));

            sock.sendmsg(json.dump(node));

            rfiles[node["to"].str][node["filename"].str].file = new fstream(currentFilePath, ios::binary | ios::in);
            rfiles[node["to"].str][node["filename"].str].size = fs::file_size(currentFilePath);

            lst->insertRow(lst->rowCount());
            lst->setItem(lst->rowCount() - 1, 0, new QTableWidgetItem(QString::fromStdString(node["filename"].str)));
            lst->setItem(lst->rowCount() - 1, 2, new QTableWidgetItem("Upload"));
            lst->setItem(lst->rowCount() - 1, 3, new QTableWidgetItem("0Mb/s"));
            lst->setCellWidget(lst->rowCount() - 1, 1, new QProgressBar(this));
            ((QProgressBar*)lst->cellWidget(lst->rowCount() - 1, 1))->setValue(0);

            lst->item(lst->rowCount() - 1, 2)->setTextAlignment(Qt::AlignCenter);
            lst->item(lst->rowCount() - 1, 3)->setTextAlignment(Qt::AlignCenter);

            rfiles[node["to"].str][node["filename"].str].progress = (QProgressBar*)lst->cellWidget(lst->rowCount() - 1, 1);
            rfiles[node["to"].str][node["filename"].str].lstrow = lst->rowCount() - 1;
            });

        socktimer->start(10);

    }
};

class ConnectWindow : public QMainWindow {
    QLineEdit* addr = nullptr;
    QPushButton* connBtn = nullptr;
    QLabel* connErr = nullptr;

    // void keyPressEvent(QKeyEvent event) {
    //     if (event.key() == Qt::Key_Enter) connBtn->click();
    // }
public:
    ConnectWindow() {
        this->setFixedSize(490, 130);

        addr = new QLineEdit(this);
        addr->setGeometry(30, 40, 330, 35);
        addr->setPlaceholderText("Enter address");

        connBtn = new QPushButton(this);
        connBtn->setGeometry(370, 40, 90, 35);
        connBtn->setText("Connect");

        connErr = new QLabel(this);
        connErr->move(30, 80);
        connErr->hide();

        connect(addr, &QLineEdit::returnPressed, connBtn, &QPushButton::click);
        connect(connBtn, &QPushButton::clicked, [this]() {
            connErr->hide();

            try {
                sock.open(AF_INET, SOCK_STREAM);
                // sock.settcpnodelay(1);
                sock.connect(addr->text().toStdString(), server_port);

                this->close();

                (new AppWindow)->show();
            }
            catch (int e) {
                connErr->setText("Connection error: " + QString::number(e));
                connErr->adjustSize();
                connErr->show();
            }
            });
    }
};

int main(int argc, char** argv) {
    /*setlocale*/
    //SetConsoleOutputCP(CP_UTF8);

    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    // QGuiApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

    //QTextCodec::setCodecForLocale(QTextCodec::codecForName("UTF-8"));

    QApplication app(argc, argv);

    ConnectWindow* conWindow = new ConnectWindow;
    conWindow->show();

    return app.exec();
}
