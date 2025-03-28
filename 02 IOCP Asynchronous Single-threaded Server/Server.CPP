// server.cpp
// IOCP 异步单线程服务器示例（仅处理一次数据交互，端口 8888）
// IOCP Asynchronous Single-threaded Server Example (one data exchange, port 8888)

#include <winsock2.h>
#include <mswsock.h>   // 包含 AcceptEx 与 GetAcceptExSockaddrs 定义
#include <ws2tcpip.h>
#include <iostream>
#include <string>
#include <cstring>
#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "Mswsock.lib")

int main() {
    // Step 1: Initialize Winsock.
    // 步骤1：初始化 Winsock
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    // Step 2: Create a listening socket.
    // 步骤2：创建监听 socket
    SOCKET listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    // Step 3: Bind the listening socket to port 8888.
    // 步骤3：将监听 socket 绑定到端口 8888
    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY; // 绑定所有网络接口
    serverAddr.sin_port = htons(8888);
    bind(listenSocket, (sockaddr*)&serverAddr, sizeof(serverAddr));

    // Step 4: Start listening for incoming connections.
    // 步骤4：开始监听客户端连接
    listen(listenSocket, SOMAXCONN);
    std::cout << "Server: Listening on port 8888..." << std::endl;

    // Step 5: Create an IOCP handle.
    // 步骤5：创建 IOCP 完成端口
    HANDLE hIOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
    // 可选：将监听 socket 也关联到 IOCP（便于调试或扩展）
    CreateIoCompletionPort((HANDLE)listenSocket, hIOCP, (ULONG_PTR)listenSocket, 0);

    // Step 6: Retrieve the AcceptEx extension function pointer.
    // 步骤6：获取 AcceptEx 扩展函数指针
    LPFN_ACCEPTEX lpfnAcceptEx = NULL;
    GUID GuidAcceptEx = WSAID_ACCEPTEX;
    DWORD dwBytes = 0;
    WSAIoctl(listenSocket, SIO_GET_EXTENSION_FUNCTION_POINTER,
        &GuidAcceptEx, sizeof(GuidAcceptEx),
        &lpfnAcceptEx, sizeof(lpfnAcceptEx),
        &dwBytes, NULL, NULL);

    // Step 7: Create an accept socket for the incoming connection.
    // 步骤7：为接入连接创建一个接收 socket
    SOCKET acceptSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    // AcceptEx 要求接收 socket 预先创建，但不需要绑定

    // Step 8: Prepare a buffer and an OVERLAPPED structure for AcceptEx.
    // 步骤8：准备 AcceptEx 的缓冲区和 OVERLAPPED 结构
    // 缓冲区用于存放本地与远程地址信息
    char acceptBuffer[1024] = { 0 };
    OVERLAPPED acceptOv = { 0 };

    // Step 9: Post an asynchronous AcceptEx call.
    // 步骤9：投递异步 AcceptEx 操作（立即返回，完成由 IOCP 通知）
    BOOL bRet = lpfnAcceptEx(listenSocket, acceptSocket, acceptBuffer, 0,
        sizeof(sockaddr_in) + 16, sizeof(sockaddr_in) + 16,
        &dwBytes, &acceptOv);
    // 如果返回 FALSE 且错误为 ERROR_IO_PENDING，则说明操作挂起

    // Optionally, associate acceptSocket with IOCP.
    // 可选：将 acceptSocket 与 IOCP 关联
    CreateIoCompletionPort((HANDLE)acceptSocket, hIOCP, (ULONG_PTR)acceptSocket, 0);

    // Step 10: Wait for the AcceptEx operation to complete.
    // 步骤10：等待 AcceptEx 操作完成（阻塞等待 IOCP 通知）
    DWORD bytesTransferred;
    ULONG_PTR completionKey;
    LPOVERLAPPED lpOv;
    GetQueuedCompletionStatus(hIOCP, &bytesTransferred, &completionKey, &lpOv, INFINITE);
    std::cout << "Server: AcceptEx completed." << std::endl;

    // --- 新增步骤：提取远程客户端地址信息 ---
    // Step 10.1: Retrieve the GetAcceptExSockaddrs function pointer.
    // 步骤10.1：获取 GetAcceptExSockaddrs 扩展函数指针
    LPFN_GETACCEPTEXSOCKADDRS lpfnGetAcceptExSockaddrs = NULL;
    GUID guidGetAcceptExSockaddrs = WSAID_GETACCEPTEXSOCKADDRS;
    WSAIoctl(listenSocket, SIO_GET_EXTENSION_FUNCTION_POINTER,
        &guidGetAcceptExSockaddrs, sizeof(guidGetAcceptExSockaddrs),
        &lpfnGetAcceptExSockaddrs, sizeof(lpfnGetAcceptExSockaddrs),
        &dwBytes, NULL, NULL);

    // Step 10.2: Use GetAcceptExSockaddrs to parse acceptBuffer and get remote address.
    // 步骤10.2：调用 GetAcceptExSockaddrs 从缓冲区中解析出远程客户端地址信息
    sockaddr* localAddr = NULL;
    sockaddr* remoteAddr = NULL;
    int localAddrLen = 0, remoteAddrLen = 0;
    lpfnGetAcceptExSockaddrs(acceptBuffer, 0,
        sizeof(sockaddr_in) + 16, sizeof(sockaddr_in) + 16,
        &localAddr, &localAddrLen,
        &remoteAddr, &remoteAddrLen);
    // 转换 remoteAddr 为 IPv4 地址
    sockaddr_in* clientAddr = reinterpret_cast<sockaddr_in*>(remoteAddr);
    char clientIP[INET_ADDRSTRLEN] = { 0 };
    inet_ntop(AF_INET, &(clientAddr->sin_addr), clientIP, INET_ADDRSTRLEN);
    int clientPort = ntohs(clientAddr->sin_port);
    std::cout << "Server: Connected client: " << clientIP << ":" << clientPort << std::endl;

    // Step 11: Update the accepted socket's context.
    // 步骤11：更新 acceptSocket 的上下文，使其成为正常的已连接 socket
    setsockopt(acceptSocket, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT, reinterpret_cast<char*>(&listenSocket), sizeof(listenSocket));

    // Step 12: Post an asynchronous receive operation on the accepted socket.
    // 步骤12：在已连接 socket 上投递异步接收操作
    char recvBuffer[512] = { 0 };
    WSABUF recvWSABuf;
    recvWSABuf.buf = recvBuffer;
    recvWSABuf.len = sizeof(recvBuffer);
    OVERLAPPED recvOv = { 0 };
    DWORD flags = 0;
    DWORD recvBytes = 0;
    int retVal = WSARecv(acceptSocket, &recvWSABuf, 1, &recvBytes, &flags, &recvOv, NULL);
    if (retVal == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING) {
        std::cerr << "Server: WSARecv failed." << std::endl;
        return -1;
    }

    // Step 13: Wait for the WSARecv operation to complete.
    // 步骤13：等待异步接收操作完成
    GetQueuedCompletionStatus(hIOCP, &bytesTransferred, &completionKey, &lpOv, INFINITE);
    std::cout << "Server: Received data: " << std::string(recvBuffer, bytesTransferred) << std::endl;

    // Step 14: Post an asynchronous send operation to echo the received data.
    // 步骤14：投递异步发送操作，将接收到的数据回显给客户端
    // 在回显数据前，在数据前加上 "Server:" 前缀
    std::string receivedData(recvBuffer, bytesTransferred);
    std::string echoMsg = "Server:" + receivedData;
    WSABUF sendWSABuf;
    sendWSABuf.buf = const_cast<char*>(echoMsg.c_str());
    sendWSABuf.len = static_cast<ULONG>(echoMsg.size());
    OVERLAPPED sendOv = { 0 };
    DWORD sendBytes = 0;
    retVal = WSASend(acceptSocket, &sendWSABuf, 1, &sendBytes, 0, &sendOv, NULL);
    if (retVal == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING) {
        std::cerr << "Server: WSASend failed." << std::endl;
        return -1;
    }

    // Step 15: Wait for the WSASend operation to complete.
    // 步骤15：等待异步发送操作完成
    GetQueuedCompletionStatus(hIOCP, &bytesTransferred, &completionKey, &lpOv, INFINITE);
    std::cout << "Server: Echoed data back to client." << std::endl;

    // Step 16: Clean up resources.
    // 步骤16：关闭所有 socket 和 IOCP 句柄，并清理 Winsock 资源
    closesocket(acceptSocket);
    closesocket(listenSocket);
    CloseHandle(hIOCP);
    WSACleanup();
    std::cout << "Server: Exiting." << std::endl;
    return 0;
}
