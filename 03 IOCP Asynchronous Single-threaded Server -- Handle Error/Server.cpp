// Server.cpp
// Windows IOCP 异步单线程回显服务器 (Modern C++ 风格)
// Windows IOCP asynchronous single-threaded echo server (Modern C++ style)
//
// 本程序采用 RAII 管理资源、in-class 成员初始化、一次性获取 AcceptEx 扩展函数指针。
// It uses RAII, in-class member initialization, and retrieves the AcceptEx pointer once.

#include <winsock2.h>
#include <mswsock.h>
#include <windows.h>
#include <iostream>
#include <stdexcept>

#pragma comment(lib, "Ws2_32.lib")

// 定义 I/O 缓冲区大小 / Define I/O buffer size
constexpr int IO_BUFFER_SIZE = 1024;
// 定义监听端口 / Define listening port
constexpr int PORT = 8888;
// 定义 GetQueuedCompletionStatus 的超时时间（毫秒） / Define timeout for GetQueuedCompletionStatus (ms)
constexpr DWORD WAIT_TIMEOUT_MS = 1000;

// 异步操作类型枚举 / Enumeration for asynchronous I/O operations
enum class IO_OPERATION {
    ACCEPT,  // AcceptEx 操作 / Accept operation
    RECV,    // 接收操作 (使用 WSARecv) / Receive operation (using WSARecv)
    SEND     // 发送操作 (使用 WSASend) / Send operation (using WSASend)
};

// 异步操作的上下文数据结构 / Context for each asynchronous operation
class PerIOData {
public:
    OVERLAPPED overlapped{};                // OVERLAPPED 结构体，用于异步 I/O / OVERLAPPED for async I/O
    char buffer[IO_BUFFER_SIZE]{};            // 数据缓冲区 / Data buffer
    WSABUF wsaBuf{  IO_BUFFER_SIZE, buffer };  // WSABUF 用于描述数据缓冲区 / WSABUF describing the buffer
    IO_OPERATION operationType{ IO_OPERATION::RECV }; // 默认操作为 RECV / Default operation is RECV
    SOCKET socket{ INVALID_SOCKET };         // 关联的套接字 / Associated socket
    PerIOData() {}
    PerIOData(SOCKET s) :socket(s) {}
    // 默认构造函数使用 in-class 初始化器完成所有初始化
};

// 服务器类封装了 IOCP 服务器的主要功能 / Server class encapsulating main IOCP server functionality
class IocpServer {
public:
    IocpServer() : hIocp(nullptr), listenSocket(INVALID_SOCKET), acceptExFunc(nullptr) {}

    ~IocpServer() {
        if (listenSocket != INVALID_SOCKET)
            closesocket(listenSocket);
        if (hIocp)
            CloseHandle(hIocp);
        WSACleanup(); // 清理 Winsock 资源 / Clean up Winsock
    }

    // 初始化服务器：初始化 Winsock、创建/绑定/监听套接字、创建 IOCP，并获取 AcceptEx 扩展函数指针
    // Initialize server: start Winsock, create/bind/listen socket, create IOCP, and retrieve AcceptEx pointer.
    bool initialize() {
        WSADATA wsaData;
        int iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
        if (iResult != 0) {
            std::cerr << "WSAStartup failed. Error: " << iResult << std::endl;
            return false;
        }
        // 创建监听套接字 / Create listening socket
        listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (listenSocket == INVALID_SOCKET) {
            std::cerr << "Failed to create listening socket. Error: " << WSAGetLastError() << std::endl;
            return false;
        }
        // 配置并绑定地址 / Configure and bind address
        sockaddr_in serverAddr{};
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_addr.s_addr = htonl(INADDR_ANY); // 监听所有网卡 / Listen on all interfaces
        serverAddr.sin_port = htons(PORT);              // 设置端口 / Set port
        if (bind(listenSocket, reinterpret_cast<sockaddr*>(&serverAddr), sizeof(serverAddr)) == SOCKET_ERROR) {
            std::cerr << "Bind failed. Error: " << WSAGetLastError() << std::endl;
            return false;
        }
        // 开始监听 / Start listening
        if (listen(listenSocket, SOMAXCONN) == SOCKET_ERROR) {
            std::cerr << "Listen failed. Error: " << WSAGetLastError() << std::endl;
            return false;
        }
        // 创建 IOCP 句柄 / Create IOCP handle
        hIocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
        if (!hIocp) {
            std::cerr << "CreateIoCompletionPort failed. Error: " << GetLastError() << std::endl;
            return false;
        }
        // 将监听套接字关联到 IOCP / Associate listening socket with IOCP
        if (!CreateIoCompletionPort(reinterpret_cast<HANDLE>(listenSocket), hIocp,
            static_cast<ULONG_PTR>(listenSocket), 0)) {
            std::cerr << "Failed to associate listening socket with IOCP. Error: " << GetLastError() << std::endl;
            return false;
        }
        // 一次性获取 AcceptEx 扩展函数指针 / Retrieve AcceptEx pointer once
        GUID guidAcceptEx = WSAID_ACCEPTEX;
        DWORD bytesReturned = 0;
        if (WSAIoctl(listenSocket, SIO_GET_EXTENSION_FUNCTION_POINTER,
            &guidAcceptEx, sizeof(guidAcceptEx),
            &acceptExFunc, sizeof(acceptExFunc),
            &bytesReturned, nullptr, nullptr) == SOCKET_ERROR) {
            std::cerr << "WSAIoctl for AcceptEx failed. Error: " << WSAGetLastError() << std::endl;
            return false;
        }
        std::cout << "Server initialized successfully, listening on port " << PORT << std::endl;
        return true;
    }

    // 主循环：使用有限等待时间调用 GetQueuedCompletionStatus，并分派各操作完成事件
    // Main loop: use finite timeout for GetQueuedCompletionStatus and dispatch I/O events.
    void run() {
        // 投递初始 AcceptEx 操作 / Post initial AcceptEx operation
        postAccept();

        DWORD bytesTransferred = 0;
        ULONG_PTR completionKey = 0;
        LPOVERLAPPED lpOverlapped = nullptr;

        while (true) {
            BOOL result = GetQueuedCompletionStatus(hIocp, &bytesTransferred, &completionKey, &lpOverlapped, WAIT_TIMEOUT_MS);
            if (!result) {
                // 超时或临时错误不致命 / Timeout or transient error; continue looping
                if (lpOverlapped == nullptr && GetLastError() == WAIT_TIMEOUT) {
                    continue; // 无 I/O 事件，继续等待 / No I/O event, continue waiting
                }
                else {
                    std::cerr << "GetQueuedCompletionStatus failed. Error: " << GetLastError() << std::endl;
                    continue;
                }
            }
            // 获取完成的异步操作上下文 / Retrieve completed operation context
            auto* pIOData = reinterpret_cast<PerIOData*>(lpOverlapped);
            switch (pIOData->operationType) {
            case IO_OPERATION::ACCEPT:
                handleAccept(pIOData);
                break;
            case IO_OPERATION::RECV:
                handleRecv(pIOData, bytesTransferred);
                break;
            case IO_OPERATION::SEND:
                handleSend(pIOData);
                break;
            default:
                std::cerr << "Unknown I/O operation type." << std::endl;
                delete pIOData;
                break;
            }
        }
    }

private:
    HANDLE hIocp;              // IOCP 句柄 / IOCP handle
    SOCKET listenSocket;       // 监听套接字 / Listening socket
    LPFN_ACCEPTEX acceptExFunc; // AcceptEx 函数指针 / Pointer to AcceptEx

    // 投递一个异步 AcceptEx 操作，用于接受新连接
    // Post an asynchronous AcceptEx operation to accept a new connection.
    void postAccept() {
        // 为新连接创建一个套接字 / Create a new socket for the incoming connection.
        SOCKET acceptSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (acceptSocket == INVALID_SOCKET) {
            std::cerr << "Failed to create accept socket. Error: " << WSAGetLastError() << std::endl;
            return;
        }
        // 分配并初始化上下文对象 / Allocate and initialize the context object.
        auto* pIOData = new PerIOData();
        pIOData->operationType = IO_OPERATION::ACCEPT;
        pIOData->socket = acceptSocket;
        // 调用 AcceptEx 发起异步接受 / Call AcceptEx to initiate asynchronous accept.
        DWORD bytesReturned = 0;
        if (!acceptExFunc(listenSocket, acceptSocket, pIOData->buffer, 0,
            sizeof(sockaddr_in) + 16, sizeof(sockaddr_in) + 16,
            &bytesReturned, &pIOData->overlapped))
        {
            int err = WSAGetLastError();
            if (err != ERROR_IO_PENDING) {
                std::cerr << "AcceptEx failed. Error: " << err << std::endl;
                closesocket(acceptSocket);
                delete pIOData;
                return;
            }
        }
        std::cout << "Posted an asynchronous AcceptEx operation." << std::endl;
    }

    // 处理 AcceptEx 完成事件 / Handle completion of an AcceptEx operation.
    void handleAccept(PerIOData* pIOData) {
        SOCKET clientSocket = pIOData->socket;
        // 将新客户端套接字关联到 IOCP / Associate the accepted socket with IOCP.
        if (!CreateIoCompletionPort(reinterpret_cast<HANDLE>(clientSocket), hIocp,
            static_cast<ULONG_PTR>(clientSocket), 0)) {
            std::cerr << "Failed to associate client socket with IOCP. Error: " << GetLastError() << std::endl;
            closesocket(clientSocket);
            delete pIOData;
            return;
        }
        // 更新套接字上下文 (必须调用 setsockopt(SO_UPDATE_ACCEPT_CONTEXT) )
        // Update socket context by calling setsockopt(SO_UPDATE_ACCEPT_CONTEXT)
        if (setsockopt(clientSocket, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT,
            reinterpret_cast<char*>(&listenSocket), sizeof(listenSocket)) == SOCKET_ERROR) {
            std::cerr << "setsockopt(SO_UPDATE_ACCEPT_CONTEXT) failed. Error: " << WSAGetLastError() << std::endl;
            closesocket(clientSocket);
            delete pIOData;
            return;
        }
        std::cout << "Accepted a new connection. Client socket: " << clientSocket << std::endl;
        // 为新连接投递接收操作 / Post a receive operation on the new connection.
        postRecv(clientSocket);
        // 再次投递 AcceptEx 以便接受更多连接 / Post another AcceptEx for subsequent connections.
        postAccept();
        // 释放当前上下文对象 / Free the current context object.
        delete pIOData;
    }

    // 处理异步接收完成事件（WSARecv 已完成） / Handle completion of a receive operation.
    void handleRecv(PerIOData* pIOData, DWORD bytesTransferred) {
        if (bytesTransferred == 0) {
            std::cout << "Client disconnected. Socket: " << pIOData->socket << std::endl;
            closesocket(pIOData->socket);
            delete pIOData;
            return;
        }
        // 终止字符串 / Null-terminate the received data.
        pIOData->buffer[bytesTransferred] = '\0';
        std::cout << "Received data from socket " << pIOData->socket << ": " << pIOData->buffer << std::endl;
        // 接收到数据后，修改操作类型为 SEND，准备将数据回显给客户端
        // After receiving data, change operation type to SEND to echo data back.
        pIOData->operationType = IO_OPERATION::SEND;
        pIOData->wsaBuf.len = bytesTransferred; // 发送的数据长度与接收到的一致 / Set send length equal to received bytes.
        DWORD bytesSent = 0;
        int ret = WSASend(pIOData->socket, &pIOData->wsaBuf, 1, &bytesSent, 0, &pIOData->overlapped, nullptr);
        if (ret == SOCKET_ERROR) {
            int err = WSAGetLastError();
            if (err != WSA_IO_PENDING) {
                std::cerr << "WSASend failed in handleRecv. Error: " << err << std::endl;
                closesocket(pIOData->socket);
                delete pIOData;
                return;
            }
        }
        std::cout << "Posted asynchronous WSASend operation (echo) for socket " << pIOData->socket << std::endl;
        // 注意：这里不删除 pIOData，因为它将用于发送完成后的处理
    }

    // 处理异步发送完成事件 / Handle completion of a send operation.
    void handleSend(PerIOData* pIOData) {
        // 发送完成后，为当前连接重新投递接收操作 / After sending, post a new receive to continue communication.
        postRecv(pIOData->socket);
        delete pIOData;
    }

    // 投递异步接收操作（WSARecv） / Post an asynchronous receive (WSARecv) operation on socket s.
    void postRecv(SOCKET s) {
        auto* pIOData = new PerIOData(s);
        pIOData->operationType = IO_OPERATION::RECV; // 标记为 RECV 操作 / Mark as RECV.
        DWORD flags = 0;
        DWORD bytesReceived = 0;
        int ret = WSARecv(s, &pIOData->wsaBuf, 1, &bytesReceived, &flags, &pIOData->overlapped, nullptr);
        if (ret == SOCKET_ERROR) {
            int err = WSAGetLastError();
            if (err != WSA_IO_PENDING) {
                std::cerr << "WSARecv failed. Error: " << err << std::endl;
                closesocket(s);
                delete pIOData;
                return;
            }
        }
        std::cout << "Posted an asynchronous WSARecv operation on socket " << s << std::endl;
    }
};

int main() {
    try {
        IocpServer server;
        if (!server.initialize())
            return 1;
        server.run();
    }
    catch (const std::exception& ex) {
        std::cerr << "Exception occurred: " << ex.what() << std::endl;
    }
    return 0;
}
