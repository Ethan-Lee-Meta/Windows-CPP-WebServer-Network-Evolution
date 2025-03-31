// MinimalAsyncClient.cpp
// -------------------------------------------------------------
// This is a minimal asynchronous single-threaded TCP client using IOCP on port 8888.
// It sends a message prefixed with "Client:" and waits for an echoed response from the server.
// 这是一个使用 IOCP 实现的极简异步单线程 TCP 客户端（端口 8888）。
// 客户端发送的数据带有 "Client:" 前缀，并等待服务器回显。
// -------------------------------------------------------------

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iostream>
#include <string>
#include <cstring>
#pragma comment(lib, "Ws2_32.lib")
using namespace std;

int main() {
    // Step 1: Initialize Winsock / 初始化 Winsock
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    // Step 2: Create a client socket with overlapped support / 创建支持异步 I/O 的客户端套接字
    SOCKET clientSocket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);

    // Step 3: Connect to the server at 127.0.0.1:8888 / 连接到服务器 127.0.0.1:8888（同步连接以简化示例）
    sockaddr_in serverAddr = {};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(8888);
    inet_pton(AF_INET, "127.0.0.1", &serverAddr.sin_addr);
    connect(clientSocket, (sockaddr*)&serverAddr, sizeof(serverAddr));
    cout << "[Info] Client: Connected to server 127.0.0.1:8888." << endl;

    // Step 4: Create IOCP and associate the client socket with it / 创建 IOCP，并将客户端套接字与之关联
    HANDLE iocpHandle = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
    CreateIoCompletionPort((HANDLE)clientSocket, iocpHandle, (ULONG_PTR)clientSocket, 0);

    // Step 5: Post an asynchronous send operation / 投递异步发送操作，将带 "Client:" 前缀的消息发送到服务器
    const char* message = "Client: Hello, IOCP server!";
    WSABUF sendWSABuf;
    sendWSABuf.buf = const_cast<char*>(message);
    sendWSABuf.len = (ULONG)strlen(message);
    OVERLAPPED sendOv = { 0 };
    DWORD sendBytes = 0;
    int ret = WSASend(clientSocket, &sendWSABuf, 1, &sendBytes, 0, &sendOv, nullptr);
    if (ret == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING) {
        cerr << "[Error] Client: WSASend failed." << endl;
        return -1;
    }
    DWORD bytesTransferred;
    ULONG_PTR compKey;
    LPOVERLAPPED lpOv;
    GetQueuedCompletionStatus(iocpHandle, &bytesTransferred, &compKey, &lpOv, INFINITE);
    cout << "[Info] Client: Sent message: " << message << endl;

    // Step 6: Post an asynchronous receive operation / 投递异步接收操作，等待服务器回显消息
    char recvBuffer[512] = { 0 };
    WSABUF recvWSABuf;
    recvWSABuf.buf = recvBuffer;
    recvWSABuf.len = sizeof(recvBuffer);
    OVERLAPPED recvOv = { 0 };
    DWORD flags = 0;
    DWORD recvBytes = 0;
    ret = WSARecv(clientSocket, &recvWSABuf, 1, &recvBytes, &flags, &recvOv, nullptr);
    if (ret == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING) {
        cerr << "[Error] Client: WSARecv failed." << endl;
        return -1;
    }
    GetQueuedCompletionStatus(iocpHandle, &bytesTransferred, &compKey, &lpOv, INFINITE);
    cout << "[Info] Client: Received echo from server: " << string(recvBuffer, bytesTransferred) << endl;

    // Step 7: Clean up resources / 关闭套接字、IOCP 句柄，并清理 Winsock 资源
    closesocket(clientSocket);
    CloseHandle(iocpHandle);
    WSACleanup();
    cout << "[Info] Client: Exiting." << endl;
    return 0;
}
