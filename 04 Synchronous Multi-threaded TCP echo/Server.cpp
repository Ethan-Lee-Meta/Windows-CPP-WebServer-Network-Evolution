// Server.cpp
// ˵��������һ������ Windows Socket (WinSock) �ķ�����ʾ����
// ���ܣ������������˿� 8888�����������ӣ���ӡ�ͻ��˵ĵ�ַ��Ϣ��
//      Ϊÿ����������һ�������̣߳������߳��ڽ������ݺ�ظ� ��Server:�� ǰ׺����Ϣ��
//      �Ự����ʱ�������߳������ϱ������ɻỰ�����߳̽�����Դ���գ�join ���Ƴ�����
//
// ����ʱ��ȷ������ ws2_32.lib

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iostream>
#include <thread>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <stdexcept>
#include <string>

#pragma comment(lib, "ws2_32.lib")

// ------------------- RAII �� -------------------------

// WSAInitializer�����ڳ�ʼ�� WinSock �⣨Resource Acquisition Is Initialization��
// ����˵������ʼ�� WinSock������ʱ���� WSAStartup������ʱ���� WSACleanup��
class WSAInitializer {
public:
    WSAInitializer() {
        int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
        if (result != 0) {
            throw std::runtime_error("WSAStartup failed with error: " + std::to_string(result));
        }
    }
    ~WSAInitializer() {
        WSACleanup();
    }
private:
    WSADATA wsaData;
};

// Socket �ࣺ�� SOCKET ������з�װ��ȷ��������ʱ�Զ����� closesocket �ͷ���Դ��
// ����˵���������ֹ���ƣ�delete �������캯���븳ֵ���������֧���ƶ����塣
class Socket {
public:
    // explicit ���캯������������ʽת����Ĭ�ϲ���Ϊ INVALID_SOCKET
    explicit Socket(SOCKET s = INVALID_SOCKET) : sock(s) {}
    ~Socket() {
        if (sock != INVALID_SOCKET) {
            closesocket(sock);
        }
    }
    // ��ֹ���Ʋ���
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
    // �ƶ����캯��
    Socket(Socket&& other) noexcept : sock(other.sock) {
        other.sock = INVALID_SOCKET;
    }
    // �ƶ���ֵ�����
    Socket& operator=(Socket&& other) noexcept {
        if (this != &other) {
            if (sock != INVALID_SOCKET) {
                closesocket(sock);
            }
            sock = other.sock;
            other.sock = INVALID_SOCKET;
        }
        return *this;
    }
    // ��ȡ�ײ� SOCKET
    SOCKET get() const { return sock; }
private:
    SOCKET sock;
};

// ------------------- �ͻ��˻Ự���� -------------------------

// ClientSession �ṹ�壺���ڱ���ÿ���ͻ��˻Ự�Ĵ����̺߳�һ�� finished ��־
// finished ��־���ڱ�ʾ�ûỰ�Ƿ������ʹ�� std::shared_ptr<std::atomic<bool>> �����ڶ���߳��й����״̬��
struct ClientSession {
    std::thread thread; // ����ÿͻ��˵��߳�
    std::shared_ptr<std::atomic<bool>> finished; // �Ự��ɱ�־
    ClientSession(std::thread&& t, std::shared_ptr<std::atomic<bool>> f)
        : thread(std::move(t)), finished(f) {
    }
};

// ȫ�����������ڱ������л�Ծ�Ŀͻ��˻Ự
std::vector<ClientSession> g_clientSessions;
// ȫ�ֻ�����������ȫ�ֻỰ�����ķ���
std::mutex g_sessionsMutex;
// �������������ڵ��лỰ����ʱ֪ͨ�����߳�
std::condition_variable g_sessionCV;

// ------------------- �Ự�����߳� -------------------------

// session_cleaner �������ȴ���������֪ͨ�󣬶�ȫ���������ѽ����ĻỰ���� join ���Ƴ���
// ����˵�������߳�������������ѯ�����������ȴ��Ự������֪ͨ�������ϱ�����
void session_cleaner() {
    while (true) {
        std::unique_lock<std::mutex> lock(g_sessionsMutex);
        // �ȴ�ֱ��������һ���Ự����
        g_sessionCV.wait(lock, [] {
            for (const auto& session : g_clientSessions) {
                if (session.finished->load()) return true;
            }
            return false;
            });
        // ����ȫ�������������ѽ����ĻỰ
        for (auto it = g_clientSessions.begin(); it != g_clientSessions.end(); ) {
            if (it->finished->load()) {
                if (it->thread.joinable()) {
                    it->thread.join();
                }
                it = g_clientSessions.erase(it);
            }
            else {
                ++it;
            }
        }
    }
}

// ------------------- �ͻ��˴����߳� -------------------------

// handle_client �����������뵥���ͻ��˵�ͨ�š�
// 1. ���տͻ������ݣ���ӡ�ͻ��� IP/�˿���Ϣ��
// 2. �ظ�����ʱ��ǰ����� "Server:" ǰ׺��
// 3. ���Ự����ʱ������ finished ��־��ͨ���������������ϱ���
// ������
//   clientSocket - �ÿͻ��˵� Socket ���󣨷�װ��
//   finished - �Ự��ɱ�־�Ĺ���ָ��
//   clientAddr - �ͻ��˵�ַ��Ϣ��sockaddr_in��
void handle_client(Socket clientSocket, std::shared_ptr<std::atomic<bool>> finished, sockaddr_in clientAddr) {
    // ���ͻ��˵�ַת��Ϊ�ַ�����������־���
    char clientIP[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(clientAddr.sin_addr), clientIP, INET_ADDRSTRLEN);
    std::cout << "Handling client " << clientIP << ":" << ntohs(clientAddr.sin_port) << std::endl;

    const int bufSize = 1024;
    char buffer[bufSize] = { 0 };

    // ͨ��ѭ�����������ݲ��ظ�
    while (true) {
        int bytesReceived = recv(clientSocket.get(), buffer, bufSize - 1, 0);
        if (bytesReceived > 0) {
            buffer[bytesReceived] = '\0'; // ȷ���ַ����� '\0' ��β
            std::cout << "Received from " << clientIP << ": " << buffer << std::endl;
            // �ڻظ�ǰ��� "Server:" ǰ׺
            std::string response = "Server: " + std::string(buffer);
            int bytesSent = send(clientSocket.get(), response.c_str(), (int)response.size(), 0);
            if (bytesSent == SOCKET_ERROR) {
                std::cerr << "send() failed with error: " << WSAGetLastError() << std::endl;
                break;
            }
        }
        else if (bytesReceived == 0) {
            std::cout << "Client " << clientIP << " disconnected gracefully." << std::endl;
            break;
        }
        else {
            std::cerr << "recv() failed with error: " << WSAGetLastError() << std::endl;
            break;
        }
    }
    // �Ự���������� finished ��־��֪ͨ�����߳�
    finished->store(true);
    g_sessionCV.notify_one();
}

// ------------------- ������ -------------------------

int main() {
    try {
        WSAInitializer wsa; // ��ʼ�� WinSock

        // �������� socket
        Socket listenSocket(socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
        if (listenSocket.get() == INVALID_SOCKET) {
            throw std::runtime_error("socket() failed with error: " + std::to_string(WSAGetLastError()));
        }

        // ���÷�������ַ�������ַ���˿� 8888
        sockaddr_in serverAddr{};
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_addr.s_addr = INADDR_ANY;
        serverAddr.sin_port = htons(8888);

        // �󶨼��� socket
        if (bind(listenSocket.get(), reinterpret_cast<sockaddr*>(&serverAddr), sizeof(serverAddr)) == SOCKET_ERROR) {
            throw std::runtime_error("bind() failed with error: " + std::to_string(WSAGetLastError()));
        }

        // ��ʼ��������
        if (listen(listenSocket.get(), SOMAXCONN) == SOCKET_ERROR) {
            throw std::runtime_error("listen() failed with error: " + std::to_string(WSAGetLastError()));
        }

        std::cout << "Server is listening on port 8888..." << std::endl;

        // �����Ự�����̣߳������ϱ��Ự������
        std::thread cleanerThread(session_cleaner);
        cleanerThread.detach(); // ������̣߳���������ʱ�����޳�

        // ��ѭ�����ȴ�������������
        while (true) {
            sockaddr_in clientAddr{};
            int clientAddrLen = sizeof(clientAddr);
            // ���������ӣ�����ȡ�ͻ��˵�ַ��Ϣ
            SOCKET clientSock = accept(listenSocket.get(), reinterpret_cast<sockaddr*>(&clientAddr), &clientAddrLen);
            if (clientSock == INVALID_SOCKET) {
                std::cerr << "accept() failed with error: " << WSAGetLastError() << std::endl;
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }
            // ���ͻ��˵�ַת��Ϊ�ַ������ڴ�ӡ
            char clientIP[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &(clientAddr.sin_addr), clientIP, INET_ADDRSTRLEN);
            std::cout << "Accepted new connection from " << clientIP << ":" << ntohs(clientAddr.sin_port) << std::endl;

            // Ϊ�����Ӵ��� finished ��־����ʼΪ false��
            auto finishedFlag = std::make_shared<std::atomic<bool>>(false);
            // ��������ÿͻ��˵��̣߳����� Socket��finished ��־�Ϳͻ��˵�ַ��Ϣ
            std::thread t(handle_client, Socket(clientSock), finishedFlag, clientAddr);

            // ���µĿͻ��˻Ự����ȫ�����������ں������������
            {
                std::lock_guard<std::mutex> lock(g_sessionsMutex);
                g_clientSessions.emplace_back(std::move(t), finishedFlag);
            }
        }
    }
    catch (const std::exception& ex) {
        std::cerr << "Exception in server: " << ex.what() << std::endl;
        return 1;
    }
    return 0;
}
