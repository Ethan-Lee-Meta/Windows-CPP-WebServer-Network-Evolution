// Server.cpp
// ---------------------------------------------
// 这是一个简单的同步单线程 TCP Echo 服务器
// This is a simple synchronous single-threaded TCP echo server
//----------------------------------------------

#include <winsock2.h>      // Winsock2 API，网络编程的核心接口
// Winsock2 API, the core interface for network programming

#include <ws2tcpip.h>      // 提供 IP 地址转换函数，如 inet_pton
// Provides IP address conversion functions, e.g., inet_pton

#include <iostream>        // 用于输入输出流
// For input/output streams

#include <string>          // 用于 std::string 类型
// For std::string

// 链接 Ws2_32.lib 库
// Link with Ws2_32.lib library
#pragma comment(lib, "Ws2_32.lib")

int main() {
    // Step 1: 初始化 Winsock
    // Step 1: Initialize Winsock
    WSADATA wsaData;
    int iResult = WSAStartup(MAKEWORD(2, 2), &wsaData); // 请求 Winsock 2.2 版本
    if (iResult != 0) {
        std::cerr << "WSAStartup failed: " << iResult << std::endl;
        return 1;
    }

    // Step 2: 创建监听套接字 (TCP 套接字)
    // Step 2: Create a listening socket (TCP socket)
    SOCKET ListenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (ListenSocket == INVALID_SOCKET) {
        std::cerr << "Error at socket(): " << WSAGetLastError() << std::endl;
        WSACleanup();
        return 1;
    }

    // Step 3: 绑定套接字到本地地址和端口
    // Step 3: Bind the socket to a local IP address and port
    sockaddr_in service;
    service.sin_family = AF_INET;                   // 使用 IPv4
    service.sin_addr.s_addr = INADDR_ANY;             // 绑定到所有可用接口
    service.sin_port = htons(8080);                  // 端口 27015（htons 用于转换为网络字节序）
    
    iResult = bind(ListenSocket, (SOCKADDR *)&service, sizeof(service));
    if (iResult == SOCKET_ERROR) {
        std::cerr << "bind failed: " << WSAGetLastError() << std::endl;
        closesocket(ListenSocket);
        WSACleanup();
        return 1;
    }

    // Step 4: 监听传入的连接请求
    // Step 4: Listen for incoming connection requests
    if (listen(ListenSocket, SOMAXCONN) == SOCKET_ERROR) {
        std::cerr << "listen failed: " << WSAGetLastError() << std::endl;
        closesocket(ListenSocket);
        WSACleanup();
        return 1;
    }
    std::cout << "Server is listening on port 27015..." << std::endl;

    // Step 5: 接受客户端连接 (阻塞调用 accept)
    // Step 5: Accept an incoming client connection (blocking accept call)
    SOCKET ClientSocket = accept(ListenSocket, NULL, NULL);
    if (ClientSocket == INVALID_SOCKET) {
        std::cerr << "accept failed: " << WSAGetLastError() << std::endl;
        closesocket(ListenSocket);
        WSACleanup();
        return 1;
    }
    std::cout << "Accepted a client connection." << std::endl;

    // Step 6: 通信循环：接收数据，显示接收数据内容，并回显给客户端，前缀加上 "Server:"
    // Step 6: Communication loop: receive data, display the received content, and echo it back with prefix "Server:"
    const int recvbuflen = 512;
    char recvbuf[recvbuflen];
    int iSendResult;
    int iRecvResult;

    do {
        // 阻塞等待接收数据
        // Blocking call to receive data
        iRecvResult = recv(ClientSocket, recvbuf, recvbuflen, 0);
        if (iRecvResult > 0) {
            // 将接收到的数据转换为 std::string 便于显示
            // Convert the received data to std::string for display
            std::string receivedMsg(recvbuf, iRecvResult);
            std::cout << "Server received: " << receivedMsg << std::endl;

            // 准备回显消息，加上前缀 "Server:"
            // Prepare the echo message by prefixing "Server:"
            std::string sendMsg = "Server:" + receivedMsg;
            iSendResult = send(ClientSocket, sendMsg.c_str(), (int)sendMsg.size(), 0);
            if (iSendResult == SOCKET_ERROR) {
                std::cerr << "send failed: " << WSAGetLastError() << std::endl;
                closesocket(ClientSocket);
                WSACleanup();
                return 1;
            }
            std::cout << "Server sent: " << sendMsg << std::endl;
        }
        else if (iRecvResult == 0) {
            std::cout << "Connection closing..." << std::endl;
        }
        else {
            std::cerr << "recv failed: " << WSAGetLastError() << std::endl;
            closesocket(ClientSocket);
            WSACleanup();
            return 1;
        }
    } while (iRecvResult > 0);

    // Step 7: 关闭发送方向
    // Step 7: Shutdown the connection for sending
    iResult = shutdown(ClientSocket, SD_SEND);
    if (iResult == SOCKET_ERROR) {
        std::cerr << "shutdown failed: " << WSAGetLastError() << std::endl;
        closesocket(ClientSocket);
        WSACleanup();
        return 1;
    }

    // Step 8: 清理资源：关闭客户端和监听套接字，清理 Winsock
    // Step 8: Cleanup: close client and listening sockets, and cleanup Winsock
    closesocket(ClientSocket);
    closesocket(ListenSocket);
    WSACleanup();
    std::cout << "Server shutdown completed." << std::endl;

    return 0;
}
