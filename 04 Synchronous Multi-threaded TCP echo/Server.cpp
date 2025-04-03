// Server.cpp
// 说明：这是一个基于 Windows Socket (WinSock) 的服务器示例。
// 功能：服务器监听端口 8888，接收新连接，打印客户端的地址信息，
//      为每个连接启动一个处理线程，处理线程在接收数据后回复 “Server:” 前缀的消息。
//      会话结束时，处理线程主动上报，并由会话清理线程进行资源回收（join 和移除）。
//
// 编译时请确保链接 ws2_32.lib

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

// ------------------- RAII 类 -------------------------

// WSAInitializer：用于初始化 WinSock 库（Resource Acquisition Is Initialization）
// 中文说明：初始化 WinSock，构造时调用 WSAStartup，析构时调用 WSACleanup。
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

// Socket 类：对 SOCKET 句柄进行封装，确保在析构时自动调用 closesocket 释放资源。
// 中文说明：该类禁止复制（delete 拷贝构造函数与赋值运算符），支持移动语义。
class Socket {
public:
    // explicit 构造函数：不允许隐式转换，默认参数为 INVALID_SOCKET
    explicit Socket(SOCKET s = INVALID_SOCKET) : sock(s) {}
    ~Socket() {
        if (sock != INVALID_SOCKET) {
            closesocket(sock);
        }
    }
    // 禁止复制操作
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
    // 移动构造函数
    Socket(Socket&& other) noexcept : sock(other.sock) {
        other.sock = INVALID_SOCKET;
    }
    // 移动赋值运算符
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
    // 获取底层 SOCKET
    SOCKET get() const { return sock; }
private:
    SOCKET sock;
};

// ------------------- 客户端会话管理 -------------------------

// ClientSession 结构体：用于保存每个客户端会话的处理线程和一个 finished 标志
// finished 标志用于表示该会话是否结束，使用 std::shared_ptr<std::atomic<bool>> 便于在多个线程中共享该状态。
struct ClientSession {
    std::thread thread; // 处理该客户端的线程
    std::shared_ptr<std::atomic<bool>> finished; // 会话完成标志
    ClientSession(std::thread&& t, std::shared_ptr<std::atomic<bool>> f)
        : thread(std::move(t)), finished(f) {
    }
};

// 全局容器，用于保存所有活跃的客户端会话
std::vector<ClientSession> g_clientSessions;
// 全局互斥量，保护全局会话容器的访问
std::mutex g_sessionsMutex;
// 条件变量，用于当有会话结束时通知清理线程
std::condition_variable g_sessionCV;

// ------------------- 会话清理线程 -------------------------

// session_cleaner 函数：等待条件变量通知后，对全局容器中已结束的会话进行 join 和移除。
// 中文说明：此线程无需周期性轮询，而是主动等待会话结束的通知（主动上报）。
void session_cleaner() {
    while (true) {
        std::unique_lock<std::mutex> lock(g_sessionsMutex);
        // 等待直到至少有一个会话结束
        g_sessionCV.wait(lock, [] {
            for (const auto& session : g_clientSessions) {
                if (session.finished->load()) return true;
            }
            return false;
            });
        // 遍历全局容器，清理已结束的会话
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

// ------------------- 客户端处理线程 -------------------------

// handle_client 函数：处理与单个客户端的通信。
// 1. 接收客户端数据，打印客户端 IP/端口信息。
// 2. 回复数据时在前面添加 "Server:" 前缀。
// 3. 当会话结束时，设置 finished 标志并通过条件变量主动上报。
// 参数：
//   clientSocket - 该客户端的 Socket 对象（封装后）
//   finished - 会话完成标志的共享指针
//   clientAddr - 客户端地址信息（sockaddr_in）
void handle_client(Socket clientSocket, std::shared_ptr<std::atomic<bool>> finished, sockaddr_in clientAddr) {
    // 将客户端地址转换为字符串，用于日志输出
    char clientIP[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(clientAddr.sin_addr), clientIP, INET_ADDRSTRLEN);
    std::cout << "Handling client " << clientIP << ":" << ntohs(clientAddr.sin_port) << std::endl;

    const int bufSize = 1024;
    char buffer[bufSize] = { 0 };

    // 通信循环：接收数据并回复
    while (true) {
        int bytesReceived = recv(clientSocket.get(), buffer, bufSize - 1, 0);
        if (bytesReceived > 0) {
            buffer[bytesReceived] = '\0'; // 确保字符串以 '\0' 结尾
            std::cout << "Received from " << clientIP << ": " << buffer << std::endl;
            // 在回复前添加 "Server:" 前缀
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
    // 会话结束，设置 finished 标志并通知清理线程
    finished->store(true);
    g_sessionCV.notify_one();
}

// ------------------- 主函数 -------------------------

int main() {
    try {
        WSAInitializer wsa; // 初始化 WinSock

        // 创建监听 socket
        Socket listenSocket(socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
        if (listenSocket.get() == INVALID_SOCKET) {
            throw std::runtime_error("socket() failed with error: " + std::to_string(WSAGetLastError()));
        }

        // 设置服务器地址：任意地址，端口 8888
        sockaddr_in serverAddr{};
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_addr.s_addr = INADDR_ANY;
        serverAddr.sin_port = htons(8888);

        // 绑定监听 socket
        if (bind(listenSocket.get(), reinterpret_cast<sockaddr*>(&serverAddr), sizeof(serverAddr)) == SOCKET_ERROR) {
            throw std::runtime_error("bind() failed with error: " + std::to_string(WSAGetLastError()));
        }

        // 开始监听连接
        if (listen(listenSocket.get(), SOMAXCONN) == SOCKET_ERROR) {
            throw std::runtime_error("listen() failed with error: " + std::to_string(WSAGetLastError()));
        }

        std::cout << "Server is listening on port 8888..." << std::endl;

        // 启动会话清理线程（主动上报会话结束）
        std::thread cleanerThread(session_cleaner);
        cleanerThread.detach(); // 分离该线程，因其运行时间无限长

        // 主循环：等待并接受新连接
        while (true) {
            sockaddr_in clientAddr{};
            int clientAddrLen = sizeof(clientAddr);
            // 接受新连接，并获取客户端地址信息
            SOCKET clientSock = accept(listenSocket.get(), reinterpret_cast<sockaddr*>(&clientAddr), &clientAddrLen);
            if (clientSock == INVALID_SOCKET) {
                std::cerr << "accept() failed with error: " << WSAGetLastError() << std::endl;
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }
            // 将客户端地址转换为字符串用于打印
            char clientIP[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &(clientAddr.sin_addr), clientIP, INET_ADDRSTRLEN);
            std::cout << "Accepted new connection from " << clientIP << ":" << ntohs(clientAddr.sin_port) << std::endl;

            // 为新连接创建 finished 标志（初始为 false）
            auto finishedFlag = std::make_shared<std::atomic<bool>>(false);
            // 创建处理该客户端的线程，传入 Socket、finished 标志和客户端地址信息
            std::thread t(handle_client, Socket(clientSock), finishedFlag, clientAddr);

            // 将新的客户端会话加入全局容器，便于后续管理和清理
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
