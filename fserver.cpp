#include <map>
#include <vector>
#include <string>
#include <poll.h>
#include "../cpplibs/ssocket.hpp"
#include "../cpplibs/libjson.hpp"
using namespace std;

struct client_t {
    Socket sock;
    string name;
};

map<string, string> sockpipes;
map<string, string> transfer_requests;
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
    sockrecv_t cmd = sock.recv(8192); 

    if (!cmd.size) return 0;

    JsonNode node = json.parse(cmd.string);

    if (node["cmd"].str == "cl_list") {
        JsonNode node1;
        JsonNode node2;
        

        for (auto i : clients) if (i.second != sock) node2.arrayAppend(i.second.remoteAddress().str());

        node1.addPair("clients", node2);
        sock.send(json.dump(node1));
    }

    else if (node["cmd"].str == "transfer_request") {
        JsonNode node1;
        node1.addPair("cmd", node["cmd"].str);
        node1.addPair("from", sock.remoteAddress().str());
        node1.addPair("filename", node["filename"].str);
        node1.addPair("filesize", node["filesize"].str);

        findClient(node["to"].str).send(json.dump(node1));

        transfer_requests[sock.remoteAddress().str()] = node["to"].str;
    }

    else if (node["cmd"].str == "accept_transfer_request") {
        JsonNode node1;
        node1.addPair("cmd", "accept");
        node1.addPair("from", sock.remoteAddress().str());

        findClient(node["to"].str).send(json.dump(node1));

        sockpipes[node["to"].str] = sock.remoteAddress().str();
        transfer_requests.erase(node["to"].str);

        cout << "Created pipe: " << node["to"].str << " >> " << sock.remoteAddress().str() << endl;
    }

    else if (node["cmd"].str == "discard_transfer_request") {
        JsonNode node1;
        node1.addPair("cmd", "discard");
        node1.addPair("from", sock.remoteAddress().str());

        findClient(node["to"].str).send(json.dump(node1));

        transfer_requests.erase(node["to"].str);
    }

    else if (node["cmd"].str == "transfer_packet") {
        JsonNode node1;

        if (sockpipes.find(sock.remoteAddress().str()) != sockpipes.end() || sockpipes[sock.remoteAddress().str()] != node["to"].str) {
            node1.addPair("cmd", "transfer_forbidden");
            sock.send(json.dump(node1));
        }

        else {
            node1.addPair("cmd", node["cmd"].str);
            node1.addPair("from", sock.remoteAddress().str());
            node1.addPair("data", node["data"].str);

            findClient(node["to"].str).send(json.dump(node1));
        }
    }

    else if (node["cmd"].str == "packet_received") {

    }

    return cmd.size;
}

int main() {
    Socket sock(AF_INET, SOCK_STREAM);
    sock.setsockopt(SOL_SOCKET, SO_REUSEADDR, 1);
    sock.bind("", 9723);
    sock.listen(0);

    vector<pollfd> fds;
    fds.push_back({sock.fd(), POLLIN, 0});

    while (true) {
        if (poll(fds.data(), fds.size(), -1)) {
            if (fds[0].revents & POLLIN) {
                auto tmp = sock.accept();

                JsonNode node;
                node.addPair("cmd", "client_connected");
                node.addPair("address", tmp.second.str());

                for (auto i : clients) i.second.send(json.dump(node));

                fds.push_back({tmp.first.fd(), POLLIN, 0});
                clients[tmp.first.fd()] = tmp.first;
            }

            else for (int i = 1; i < fds.size(); i++) {
                Socket client = clients[fds[i].fd];

                if (fds[i].revents & POLLIN) {                    
                    if (!handler(client)) {
                        clients.erase(fds[i].fd);
                        fds.erase(fds.begin() + i--);

                        JsonNode node;
                        node.addPair("cmd", "client_disconnected");
                        node.addPair("address", client.remoteAddress().str());

                        for (auto i : clients) i.second.send(json.dump(node));

                        for (auto [sender, receiver] : sockpipes)
                        if (client.remoteAddress().str() == sender || client.remoteAddress().str() == receiver) {
                            sockpipes.erase(sender);
                            cout << "Closed pipe: " << sender << " >> " << receiver << endl;
                            break;
                        }

                        client.close();
                        // clients.pop_back();
                        
                    }
                }
            }
        }
    }
}