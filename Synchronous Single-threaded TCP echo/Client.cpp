// Client.cpp
// ---------------------------------------------
// 这是一个简单的同步单线程 TCP Echo 客户端
// This is a simple synchronous single-threaded TCP echo client
//----------------------------------------------

#include <winsock2.h>      // Winsock2 API，网络编程的核心接口
// Winsock2 API, core for network programming

#include <ws2tcpip.h>      // 提供 IP 地址转换函数，如 inet_pton
// Provides IP address conversion functions, e.g., inet_pton

#include <iostream>        // 用于输入输出流
// For input/output streams

#include <string>          // 用于 std::string 类型
// For std::string

#pragma comment(lib, "Ws2_32.lib") // 链接 Ws2_32.lib 库
// Link with Ws2_32.lib

int main() {
    // Step 1: 初始化 Winsock
    // Step 1: Initialize Winsock
    WSADATA wsaData;
    int iResult = WSAStartup(MAKEWORD(2,2), &wsaData);
    if(iResult != 0) {
        std::cerr << "WSAStartup failed: " << iResult << std::endl;
        return 1;
    }

    // Step 2: 创建套接字
    // Step 2: Create a socket
    SOCKET ConnectSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (ConnectSocket == INVALID_SOCKET) {
        std::cerr << "Error at socket(): " << WSAGetLastError() << std::endl;
        WSACleanup();
        return 1;
    }

    // Step 3: 设置服务器地址
    // Step 3: Set up the server address structure
    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(8080);
    // 将 IP 地址 "127.0.0.1" 转换为二进制格式
    // Convert the IP address "127.0.0.1" to binary form
    iResult = inet_pton(AF_INET, "127.0.0.1", &serverAddr.sin_addr);
    if(iResult <= 0) {
        std::cerr << "Invalid address or address not supported" << std::endl;
        closesocket(ConnectSocket);
        WSACleanup();
        return 1;
    }

    // Step 4: 连接到服务器 (阻塞调用)
    // Step 4: Connect to the server (blocking call)
    iResult = connect(ConnectSocket, (sockaddr *)&serverAddr, sizeof(serverAddr));
    if(iResult == SOCKET_ERROR) {
        std::cerr << "Unable to connect to server: " << WSAGetLastError() << std::endl;
        closesocket(ConnectSocket);
        WSACleanup();
        return 1;
    }
    std::cout << "Connected to server." << std::endl;

    // Step 5: 通信循环：发送消息并接收服务器回显
    // Step 5: Communication loop: send messages and receive server echo
    std::string sendbuf;
    const int recvbuflen = 512;
    char recvbuf[recvbuflen];

    while (true) {
        std::cout << "Enter message to send (or 'exit' to quit): ";
        std::getline(std::cin, sendbuf);
        if(sendbuf == "exit")
            break;

        // 发送消息给服务器
        // Send the message to the server
        iResult = send(ConnectSocket, sendbuf.c_str(), (int)sendbuf.size(), 0);
        if (iResult == SOCKET_ERROR) {
            std::cerr << "send failed: " << WSAGetLastError() << std::endl;
            break;
        }
        std::cout << "Sent " << iResult << " bytes to server." << std::endl;

        // 接收来自服务器的回显
        // Receive the echo from the server
        iResult = recv(ConnectSocket, recvbuf, recvbuflen, 0);
        if(iResult > 0) {
            std::string echoMsg(recvbuf, iResult);
            std::cout << "Client received: " << echoMsg << std::endl;
        }
        else if(iResult == 0) {
            std::cout << "Connection closed by server." << std::endl;
            break;
        }
        else {
            std::cerr << "recv failed: " << WSAGetLastError() << std::endl;
            break;
        }
    }

    // Step 6: 清理资源：关闭套接字并清理 Winsock
    // Step 6: Cleanup: close the socket and cleanup Winsock
    closesocket(ConnectSocket);
    WSACleanup();
    std::cout << "Client shutdown completed." << std::endl;

    return 0;
}
