#define WIN32_LEAN_AND_MEAN
#include <iostream>
#include <thread>
#include <filesystem>
#include <map>
#include <mutex>
#include <utility>
#include <QtCore/QtCore>
#include <QtWidgets/QtWidgets>
#include "cpplibs/ssocket.hpp"
#include "cpplibs/strlib.hpp"
#include "cpplibs/libjson.hpp"
#include "cpplibs/libbase64.hpp"
using namespace std;
namespace fs = filesystem;

string server_ip = "";
int server_port = 9723;

int buffer_size = 16384;

class ConnectWindow : public QMainWindow {

};

class AppWindow : public QMainWindow {
    Q_OBJECT

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
    Base64 base64;
    Json json;

    map<string, map<string, ofstream*>> wfiles;
    map<string, map<string, ifstream*>> rfiles;

    fs::path currentFile;
    string currentSaveName;
    bool currentConfirmation;

    mutex fileMtx;
    mutex sockMtx;

    // void resizeEvent(QResizeEvent* event) {
    //     btn1->resize(event->size());
    // }
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

        connect(confirmButtons, &QDialogButtonBox::accepted, confirmDialog, [this]() {
            fileMtx.lock();
            confirmDialog->accept();
            currentSaveName = QFileDialog::getSaveFileName(this, tr("Save file"), QString::fromStdString(currentSaveName)).toStdString();
            fileMtx.unlock();
        });

        connect(confirmButtons, &QDialogButtonBox::rejected, confirmDialog, &QDialog::reject);
    }

    void handler() {
        try {
            sock.open(AF_INET, SOCK_STREAM);
            sock.connect(server_ip, server_port);
        } catch (int e) {
            cout << e << endl;
            return;
        }

        JsonNode node;
        node.addPair("cmd", "cl_list");
        sock.send(json.dump(node));

        node = json.parse(sock.recv(8192).string);
        for (auto i : node["clients"].array) addrLst->insertItem(addrLst->count(), QString::fromStdString(i.str));

        while (true) {
            sockrecv_t data = sock.recv(65536);
            // sockMtx.lock();

            if (!data.size) break;

            JsonNode node = json.parse(data.string);

            if (node["cmd"].str == "client_connected")
                addrLst->insertItem(addrLst->count(), QString::fromStdString(node["address"].str));

            else if (node["cmd"].str == "client_disconnected") {
                addrLst->setCurrentIndex(0);
                addrLst->removeItem(addrLst->findText(QString::fromStdString(node["address"].str)));

                for (auto [name, file] : rfiles[node["address"].str]) {
                    file->close();
                    delete file;
                }

                for (auto [name, file] : wfiles[node["address"].str]) {
                    file->close();
                    delete file;
                }

                rfiles.erase(node["address"].str);
                wfiles.erase(node["address"].str);
            }

            else if (node["cmd"].str == "transfer_request") {
                confirmName->setText("Filename: " + QString::fromStdString(node["filename"].str));
                confirmName->adjustSize();

                confirmSize->setText("Size: " + QString::number(node["filesize"].integer));
                confirmSize->adjustSize();

                confirmFrom->setText("From: " + QString::fromStdString(node["from"].str));
                confirmFrom->adjustSize();

                JsonNode node1;

                currentSaveName = node["filename"].str;

                emit confirm();
                if (currentConfirmation) {
                    node1.addPair("cmd", "accept_transfer_request");
                    node1.addPair("filename", node["filename"].str);
                    node1.addPair("to", node["from"].str);

                    fileMtx.lock();
#ifdef _WIN32
                    wfiles[node["from"].str][node["filename"].str] = new ofstream(str2wstr(currentSaveName), ios::binary);
#else
                    wfiles[node["from"].str][node["filename"].str] = new ofstream(currentSaveName, ios::binary);
#endif
                    fileMtx.unlock();
                }

                else {
                    node1.addPair("cmd", "discard_transfer_request");
                    node1.addPair("filename", node["filename"].str);
                    node1.addPair("to", node["from"].str);
                }

                sock.send(json.dump(node1));
            }

            else if (node["cmd"].str == "transfer_packet") {
                size_t size = node["true_size"].integer;
                char* buffer = new char[size];

                base64.decode(node["data"].str.c_str(), buffer, node["data"].str.size());

                wfiles[node["from"].str][node["filename"].str]->write(buffer, size);

                if (node["eof"].boolean) {
                    wfiles[node["from"].str][node["filename"].str]->close();

                    delete wfiles[node["from"].str][node["filename"].str];
                    wfiles.erase(node["from"].str);
                }

                JsonNode node1;
                node1.addPair("cmd", "packet_received");
                node1.addPair("filename", node["filename"].str);
                node1.addPair("eof", node["eof"].boolean);
                node1.addPair("to", node["from"].str);

                sock.send(json.dump(node1));

                delete[] buffer;
            }

            else if (node["cmd"].str == "packet_received" || node["cmd"].str == "transfer_accept") {
                if (node["eof"].boolean) continue;

                char* buffer = new char[buffer_size];
                char* encbuffer = new char[base64.calculateEncodedSize(buffer_size)];

                rfiles[node["from"].str][node["filename"].str]->read(buffer, buffer_size);
                int gcount = rfiles[node["from"].str][node["filename"].str]->gcount();

                base64.encode(buffer, encbuffer, gcount);

                bool eof = gcount < buffer_size;

                JsonNode node1;
                node1.addPair("cmd", "transfer_packet");
                node1.addPair("to", node["from"].str);
                node1.addPair("filename", node["filename"].str);
                node1.addPair("data", string(encbuffer, base64.calculateEncodedSize(gcount)));
                node1.addPair("true_size", gcount);
                node1.addPair("eof", eof);

                sock.send(json.dump(node1));

                delete[] buffer;
                delete[] encbuffer;
            }

            else if (node["cmd"].str == "transfer_diascard") {
                

            }
        
            // sockMtx.unlock();
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

        //confirmDialog->exec();

        connect(selectFileBtn, &QPushButton::clicked, [this]() {
#ifdef _WIN32
            currentFile = str2wstr(QFileDialog::getOpenFileName(this, tr("Open file")).toStdString());
            selectedFile->setText(QString::fromStdString(wstr2str(currentFile.wstring())));
#else       
            currentFile = QFileDialog::getOpenFileName(this, tr("Open file")).toStdString();
            selectedFile->setText(QString::fromStdString(currentFile.string()));
#endif
            selectedFile->adjustSize();
        });

        connect(sendBtn, &QPushButton::clicked, [this]() {
            if (!fs::exists(currentFile) || !addrLst->currentIndex()) return;

            JsonNode node;
            node.addPair("cmd", "transfer_request");
            node.addPair("to", addrLst->currentText().toStdString());
#ifdef _WIN32
            node.addPair("filename", wstr2str(currentFile.filename().wstring()));
#else
            node.addPair("filename", currentFile.filename().string());
#endif
            node.addPair("filesize", (long)fs::file_size(currentFile));

            sock.send(json.dump(node));

            rfiles[node["to"].str][node["filename"].str] = new ifstream(currentFile, ios::binary);
        });

        connect(this, &AppWindow::confirm, this, [this]() { currentConfirmation = confirmDialog->exec(); }, Qt::BlockingQueuedConnection);

        std::thread(&AppWindow::handler, this).detach();
    }

signals:
    void confirm();
};

#include "main.moc"

int main(int argc, char** argv) {
    /*setlocale*/
    //SetConsoleOutputCP(CP_UTF8);

    QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    // QGuiApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

    //QTextCodec::setCodecForLocale(QTextCodec::codecForName("UTF-8"));

    QApplication app(argc, argv);

    AppWindow* window = new AppWindow;
    window->show();

    return app.exec();
}
