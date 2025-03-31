// Client.cpp
// Windows IOCP 异步客户端 (Modern C++ 风格，支持持续数据传输)
// Windows IOCP asynchronous client (Modern C++ style, supports persistent transmission)
//
// 本程序启动后会异步连接服务器，后台线程处理 IOCP 完成事件，
// 主线程负责读取用户输入发送数据，当输入 "exit" 时主动关闭连接。
// After startup, the client asynchronously connects to the server.
// A background thread processes IOCP events while the main thread reads input.
// Typing "exit" will close the connection.

#define _WINSOCK_DEPRECATED_NO_WARNINGS  // 屏蔽 inet_addr 弃用警告 / Suppress inet_addr deprecation warning
#include <winsock2.h>
#include <mswsock.h>
#include <windows.h>
#include <iostream>
#include <stdexcept>
#include <cstring>
#include <thread>
#include <string>

#pragma comment(lib, "Ws2_32.lib")

// 定义 I/O 缓冲区大小 / Define I/O buffer size
constexpr int IO_BUFFER_SIZE = 1024;
// 定义服务器端口 / Define server port
constexpr int PORT = 8888;
// 定义等待超时时间（毫秒） / Define timeout (ms)
constexpr DWORD WAIT_TIMEOUT_MS = 1000;
// 定义服务器 IP 地址 / Define server IP address
const char* SERVER_IP = "127.0.0.1";

// 异步操作类型枚举 / Enumeration for asynchronous I/O operations
enum class IO_OPERATION {
    CONNECT, // ConnectEx 操作 / Connect operation using ConnectEx
    RECV,    // 接收操作 / Receive operation (using WSARecv)
    SEND     // 发送操作 / Send operation (using WSASend)
};

// 异步操作的上下文数据结构 / Context for each asynchronous operation
class PerIOData {
public:
    OVERLAPPED overlapped{};                // OVERLAPPED 结构体 / OVERLAPPED for async I/O
    char buffer[IO_BUFFER_SIZE]{};            // 数据缓冲区 / Data buffer
    WSABUF wsaBuf{ IO_BUFFER_SIZE, buffer};  // WSABUF 用于描述数据缓冲区 / WSABUF for buffer description
    IO_OPERATION operationType{ IO_OPERATION::RECV }; // 默认操作为 RECV / Default operation is RECV
    SOCKET socket{ INVALID_SOCKET };         // 关联的套接字 / Associated socket

    // 构造函数要求传入一个 SOCKET 参数 / Constructor requires a SOCKET parameter
    PerIOData(SOCKET s) : socket(s) {}
};

// 客户端类封装了 IOCP 客户端的主要功能 / Client class encapsulating main IOCP client functionality
class IocpClient {
public:
    IocpClient() : hIocp(nullptr), clientSocket(INVALID_SOCKET) {}
    ~IocpClient() {
        if (clientSocket != INVALID_SOCKET)
            closesocket(clientSocket);
        if (hIocp)
            CloseHandle(hIocp);
        WSACleanup(); // 清理 Winsock / Clean up Winsock
    }

    // 初始化客户端：初始化 Winsock、创建/绑定套接字、创建 IOCP 并关联套接字
    // Initialize the client: start Winsock, create/bind socket, and associate with IOCP.
    bool initialize() {
        WSADATA wsaData;
        int iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
        if (iResult != 0) {
            std::cerr << "WSAStartup failed. Error: " << iResult << std::endl;
            return false;
        }
        clientSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (clientSocket == INVALID_SOCKET) {
            std::cerr << "Failed to create client socket. Error: " << WSAGetLastError() << std::endl;
            return false;
        }
        // 绑定本地地址（系统自动分配端口）/ Bind to a local address (auto-assigned port)
        sockaddr_in localAddr{};
        localAddr.sin_family = AF_INET;
        localAddr.sin_addr.s_addr = htonl(INADDR_ANY);
        localAddr.sin_port = 0;
        if (bind(clientSocket, reinterpret_cast<sockaddr*>(&localAddr), sizeof(localAddr)) == SOCKET_ERROR) {
            std::cerr << "Bind failed. Error: " << WSAGetLastError() << std::endl;
            return false;
        }
        // 创建 IOCP 句柄并将客户端套接字关联到 IOCP / Create IOCP handle and associate it with the client socket.
        hIocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
        if (hIocp == nullptr) {
            std::cerr << "CreateIoCompletionPort failed. Error: " << GetLastError() << std::endl;
            return false;
        }
        if (!CreateIoCompletionPort(reinterpret_cast<HANDLE>(clientSocket), hIocp,
            static_cast<ULONG_PTR>(clientSocket), 0)) {
            std::cerr << "Failed to associate client socket with IOCP. Error: " << GetLastError() << std::endl;
            return false;
        }
        std::cout << "Client initialized successfully." << std::endl;
        return true;
    }

    // 使用 ConnectEx 异步连接到服务器 / Initiate asynchronous connection using ConnectEx.
    bool connectToServer() {
        sockaddr_in serverAddr{};
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_addr.s_addr = inet_addr(SERVER_IP); // 注意：inet_addr 已弃用，但这里屏蔽警告 / Note: inet_addr is deprecated, warning suppressed.
        serverAddr.sin_port = htons(PORT);

        GUID guidConnectEx = WSAID_CONNECTEX;
        DWORD dwBytes = 0;
        LPFN_CONNECTEX lpfnConnectEx = nullptr;
        if (WSAIoctl(clientSocket, SIO_GET_EXTENSION_FUNCTION_POINTER,
            &guidConnectEx, sizeof(guidConnectEx),
            &lpfnConnectEx, sizeof(lpfnConnectEx),
            &dwBytes, nullptr, nullptr) == SOCKET_ERROR) {
            std::cerr << "WSAIoctl for ConnectEx failed. Error: " << WSAGetLastError() << std::endl;
            return false;
        }

        auto* pConnIOData = new PerIOData(clientSocket);
        pConnIOData->operationType = IO_OPERATION::CONNECT; // 连接操作类型（仅用于检测完成） / Mark as CONNECT.
        pConnIOData->wsaBuf.buf = pConnIOData->buffer;
        pConnIOData->wsaBuf.len = 0;

        if (lpfnConnectEx(clientSocket, reinterpret_cast<sockaddr*>(&serverAddr), sizeof(serverAddr),
            nullptr, 0, &dwBytes, &pConnIOData->overlapped) == FALSE) {
            int err = WSAGetLastError();
            if (err != ERROR_IO_PENDING) {
                std::cerr << "ConnectEx failed. Error: " << err << std::endl;
                delete pConnIOData;
                return false;
            }
        }
        std::cout << "Posted an asynchronous ConnectEx operation." << std::endl;

        // 等待连接完成 / Wait (with finite timeout) for connection completion.
        DWORD bytesTransferred = 0;
        ULONG_PTR completionKey = 0;
        LPOVERLAPPED lpOverlapped = nullptr;
        while (true) {
            BOOL result = GetQueuedCompletionStatus(hIocp, &bytesTransferred, &completionKey, &lpOverlapped, WAIT_TIMEOUT_MS);
            if (!result) {
                if (lpOverlapped == nullptr && GetLastError() == WAIT_TIMEOUT)
                    continue;
                else {
                    std::cerr << "GetQueuedCompletionStatus failed during ConnectEx. Error: " << GetLastError() << std::endl;
                    continue;
                }
            }
            auto* pIOData = reinterpret_cast<PerIOData*>(lpOverlapped);
            if (pIOData->operationType == IO_OPERATION::CONNECT) {
                if (setsockopt(clientSocket, SOL_SOCKET, SO_UPDATE_CONNECT_CONTEXT, nullptr, 0) == SOCKET_ERROR) {
                    std::cerr << "setsockopt(SO_UPDATE_CONNECT_CONTEXT) failed. Error: " << WSAGetLastError() << std::endl;
                    delete pIOData;
                    return false;
                }
                std::cout << "Connected to the server successfully." << std::endl;
                delete pIOData;
                break;
            }
            delete pIOData;
        }
        return true;
    }

    // 运行客户端：后台线程处理 IOCP 事件，主线程读取用户输入发送数据
    // Run the client: background thread processes IOCP events while main thread reads input.
    void run() {
        // 投递初始接收操作 / Post initial receive operation.
        postRecv();

        std::thread worker(&IocpClient::iocpLoop, this);

        std::cout << "Enter messages to send to the server. Type 'exit' to quit." << std::endl;
        std::string line;
        while (std::getline(std::cin, line)) {
            if (line == "exit") {
                shutdown(clientSocket, SD_BOTH);
                break;
            }
            else {
                postSend(line);
            }
        }

        worker.join();
    }

private:
    HANDLE hIocp;         // IOCP 句柄 / IOCP handle
    SOCKET clientSocket;  // 客户端套接字 / Client socket

    // 后台线程：不断调用 GetQueuedCompletionStatus 处理接收和发送完成事件
    // Background thread: continuously process I/O events.
    void iocpLoop() {
        DWORD bytesTransferred = 0;
        ULONG_PTR completionKey = 0;
        LPOVERLAPPED lpOverlapped = nullptr;
        while (true) {
            BOOL result = GetQueuedCompletionStatus(hIocp, &bytesTransferred, &completionKey, &lpOverlapped, WAIT_TIMEOUT_MS);
            if (!result) {
                if (lpOverlapped == nullptr && GetLastError() == WAIT_TIMEOUT)
                    continue;
                else {
                    std::cerr << "GetQueuedCompletionStatus failed. Error: " << GetLastError() << std::endl;
                    break;
                }
            }
            auto* pIOData = reinterpret_cast<PerIOData*>(lpOverlapped);
            if (pIOData->operationType == IO_OPERATION::RECV) {
                if (bytesTransferred == 0) {
                    std::cout << "Server closed connection." << std::endl;
                    delete pIOData;
                    break;
                }
                else {
                    pIOData->buffer[bytesTransferred] = '\0';
                    std::cout << "Received echo from server: " << pIOData->buffer << std::endl;
                    postRecv();
                    delete pIOData;
                }
            }
            else if (pIOData->operationType == IO_OPERATION::SEND) {
                std::cout << "Message sent to server." << std::endl;
                delete pIOData;
            }
            else {
                delete pIOData;
            }
        }
    }

    // 投递异步接收操作（WSARecv） / Post an asynchronous receive (WSARecv) operation.
    void postRecv() {
        auto* pIOData = new PerIOData(clientSocket);
        pIOData->operationType = IO_OPERATION::RECV;
        DWORD flags = 0;
        DWORD bytesReceived = 0;
        int ret = WSARecv(clientSocket, &pIOData->wsaBuf, 1, &bytesReceived, &flags, &pIOData->overlapped, nullptr);
        if (ret == SOCKET_ERROR) {
            int err = WSAGetLastError();
            if (err != WSA_IO_PENDING) {
                std::cerr << "WSARecv failed. Error: " << err << std::endl;
                closesocket(clientSocket);
                delete pIOData;
                return;
            }
        }
        std::cout << "Posted an asynchronous WSARecv operation." << std::endl;
    }

    // 投递异步发送操作（WSASend） / Post an asynchronous send (WSASend) operation with the given message.
    void postSend(const std::string& msg) {
        auto* pIOData = new PerIOData(clientSocket);
        size_t msgLen = msg.size();
        if (msgLen > IO_BUFFER_SIZE)
            msgLen = IO_BUFFER_SIZE; // 超长则截断 / Truncate if too long.
        memcpy(pIOData->buffer, msg.c_str(), msgLen);
        pIOData->wsaBuf.buf = pIOData->buffer;
        pIOData->wsaBuf.len = static_cast<ULONG>(msgLen);
        pIOData->operationType = IO_OPERATION::SEND;
        DWORD bytesSent = 0;
        int ret = WSASend(clientSocket, &pIOData->wsaBuf, 1, &bytesSent, 0, &pIOData->overlapped, nullptr);
        if (ret == SOCKET_ERROR) {
            int err = WSAGetLastError();
            if (err != WSA_IO_PENDING) {
                std::cerr << "WSASend failed. Error: " << err << std::endl;
                delete pIOData;
                return;
            }
        }
        std::cout << "Posted an asynchronous WSASend operation." << std::endl;
    }
};

int main() {
    try {
        IocpClient client;
        if (!client.initialize())
            return 1;
        if (!client.connectToServer())
            return 1;
        client.run();
    }
    catch (const std::exception& ex) {
        std::cerr << "Exception occurred: " << ex.what() << std::endl;
        return 1;
    }
    return 0;
}
