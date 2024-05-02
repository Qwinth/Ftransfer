#include <map>
#include <vector>
#include <string>
#include <algorithm>
#include "cpplibs/ssocket.hpp"
#include "cpplibs/libjson.hpp"
using namespace std;

vector<pollfd> fds;

struct client_t {
    Socket sock;
    string name;
};

map<string, vector<string>> sockpipes;
map<int, Socket> clients;

Json json;

Socket findClient(string ip) {
    for (auto i : clients) if (i.second.remoteAddress().str() == ip) return i.second;
    return {};
}

int vecFind(Socket sock) {
    for (int i = 0; i < clients.size(); i++) if (clients[i].fd() == sock.fd()) return i;
    return 0;
}

int handler(Socket sock) {
    sockrecv_t cmd = sock.recvmsg();

    if (!cmd.size || cmd.string == "close_connection") return 0;

    JsonNode node = json.parse(cmd.string);

    if (node["cmd"].str == "cl_list") {
        JsonNode node1;
        JsonNode node2;


        for (auto i : clients) if (i.second != sock) node2.arrayAppend(i.second.remoteAddress().str());

        node1.addPair("clients", node2);
        sock.sendmsg(json.dump(node1));
    }

    else if (node["cmd"].str == "transfer_request") {
        JsonNode node1;
        node1.addPair("cmd", node["cmd"].str);
        node1.addPair("from", sock.remoteAddress().str());
        node1.addPair("filename", node["filename"].str);
        node1.addPair("filesize", node["filesize"].integer);

        findClient(node["to"].str).sendmsg(json.dump(node1));
    }

    else if (node["cmd"].str == "accept_transfer_request") {
        JsonNode node1;
        node1.addPair("cmd", "transfer_accept");
        node1.addPair("from", sock.remoteAddress().str());
        node1.addPair("filename", node["filename"].str);

        findClient(node["to"].str).sendmsg(json.dump(node1));

        sockpipes[node["to"].str].push_back(sock.remoteAddress().str());

        cout << "Created pipe: " << node["to"].str << " >> " << sock.remoteAddress().str() << endl;
    }

    else if (node["cmd"].str == "discard_transfer_request") {
        JsonNode node1;
        node1.addPair("cmd", "transfer_discard");
        node1.addPair("from", sock.remoteAddress().str());
        node1.addPair("filename", node["filename"].str);

        findClient(node["to"].str).sendmsg(json.dump(node1));
    }

    else if (node["cmd"].str == "transfer_packet") {
        JsonNode node1;

        if (sockpipes.find(sock.remoteAddress().str()) == sockpipes.end() || find(sockpipes[sock.remoteAddress().str()].begin(), sockpipes[sock.remoteAddress().str()].end(), node["to"].str) == sockpipes[sock.remoteAddress().str()].end()) {
            node1.addPair("cmd", "transfer_forbidden");
            sock.sendmsg(json.dump(node1));

            cout << "transfer forbidden" << endl;
        }

        else {
            node1.addPair("cmd", node["cmd"].str);
            node1.addPair("from", sock.remoteAddress().str());
            node1.addPair("filename", node["filename"].str);
            node1.addPair("data", node["data"].str);
            node1.addPair("true_size", node["true_size"].integer);
            node1.addPair("eof", node["eof"].boolean);

            if (node["eof"].boolean) {
                sockpipes[sock.remoteAddress().str()].erase(find(sockpipes[sock.remoteAddress().str()].begin(), sockpipes[sock.remoteAddress().str()].end(), node["to"].str));
                cout << "Closed pipe: " << sock.remoteAddress().str() << " >> " << node["to"].str << endl;
            }

            findClient(node["to"].str).sendmsg(json.dump(node1));
        }
    }

    else if (node["cmd"].str == "packet_received") {
        JsonNode node1;
        
        node1.addPair("cmd", node["cmd"].str);
        node1.addPair("filename", node["filename"].str);
        node1.addPair("from", sock.remoteAddress().str());

        findClient(node["to"].str).sendmsg(json.dump(node1));
    }

    return cmd.size;
}

void closeConnection(int i) {
    Socket client = clients[fds[i].fd];

    clients.erase(fds[i].fd);
    fds.erase(fds.begin() + i);

    JsonNode node;
    node.addPair("cmd", "client_disconnected");
    node.addPair("address", client.remoteAddress().str());

    for (auto i : clients) i.second.sendmsg(json.dump(node));

    if (sockpipes.find(client.remoteAddress().str()) != sockpipes.end()) {
        for (auto i : sockpipes[client.remoteAddress().str()])
        cout << "Closed pipe: " << client.remoteAddress().str() << " >> " << i << endl;
    }

    else {
        for (auto [sender, receiver] : sockpipes) {
            auto i = find(receiver.begin(), receiver.end(), client.remoteAddress().str());
            if (i != receiver.end()) {
                receiver.erase(i);
                cout << "Closed pipe: " << sender << " >> " << client.remoteAddress().str() << endl;
                break;
            }
        }
    }

    client.close();
}

int main() {
    Socket sock(AF_INET, SOCK_STREAM);
    sock.setsockopt(SOL_SOCKET, SO_REUSEADDR, 1);
    sock.bind("", 9723);
    sock.listen(0);

    fds.push_back({ sock.fd(), POLLIN, 0 });

    while (true) {
        if (poll(fds.data(), fds.size(), -1)) {
            if (fds[0].revents & POLLIN) {
                auto tmp = sock.accept();

                JsonNode node;
                node.addPair("cmd", "client_connected");
                node.addPair("address", tmp.second.str());

                for (auto i : clients) i.second.sendmsg(json.dump(node));

                fds.push_back({ tmp.first.fd(), POLLIN, 0 });
                clients[tmp.first.fd()] = tmp.first;

                fds[0].revents = 0;
            }

            else for (int i = 1; i < fds.size(); i++) {
                
                Socket client = clients[fds[i].fd];

                if (fds[i].revents & POLLIN) { if (!handler(client)) closeConnection(i--); }

                else if (fds[i].revents & POLLHUP) closeConnection(i--);

                fds[i].revents = 0;
            }
        }
    }
}
