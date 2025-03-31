// Server.cpp
// Windows IOCP �첽���̻߳��Է����� (Modern C++ ���)
// Windows IOCP asynchronous single-threaded echo server (Modern C++ style)
//
// ��������� RAII ������Դ��in-class ��Ա��ʼ����һ���Ի�ȡ AcceptEx ��չ����ָ�롣
// It uses RAII, in-class member initialization, and retrieves the AcceptEx pointer once.

#include <winsock2.h>
#include <mswsock.h>
#include <windows.h>
#include <iostream>
#include <stdexcept>

#pragma comment(lib, "Ws2_32.lib")

// ���� I/O ��������С / Define I/O buffer size
constexpr int IO_BUFFER_SIZE = 1024;
// ��������˿� / Define listening port
constexpr int PORT = 8888;
// ���� GetQueuedCompletionStatus �ĳ�ʱʱ�䣨���룩 / Define timeout for GetQueuedCompletionStatus (ms)
constexpr DWORD WAIT_TIMEOUT_MS = 1000;

// �첽��������ö�� / Enumeration for asynchronous I/O operations
enum class IO_OPERATION {
    ACCEPT,  // AcceptEx ���� / Accept operation
    RECV,    // ���ղ��� (ʹ�� WSARecv) / Receive operation (using WSARecv)
    SEND     // ���Ͳ��� (ʹ�� WSASend) / Send operation (using WSASend)
};

// �첽���������������ݽṹ / Context for each asynchronous operation
class PerIOData {
public:
    OVERLAPPED overlapped{};                // OVERLAPPED �ṹ�壬�����첽 I/O / OVERLAPPED for async I/O
    char buffer[IO_BUFFER_SIZE]{};            // ���ݻ����� / Data buffer
    WSABUF wsaBuf{  IO_BUFFER_SIZE, buffer };  // WSABUF �����������ݻ����� / WSABUF describing the buffer
    IO_OPERATION operationType{ IO_OPERATION::RECV }; // Ĭ�ϲ���Ϊ RECV / Default operation is RECV
    SOCKET socket{ INVALID_SOCKET };         // �������׽��� / Associated socket
    PerIOData() {}
    PerIOData(SOCKET s) :socket(s) {}
    // Ĭ�Ϲ��캯��ʹ�� in-class ��ʼ����������г�ʼ��
};

// ���������װ�� IOCP ����������Ҫ���� / Server class encapsulating main IOCP server functionality
class IocpServer {
public:
    IocpServer() : hIocp(nullptr), listenSocket(INVALID_SOCKET), acceptExFunc(nullptr) {}

    ~IocpServer() {
        if (listenSocket != INVALID_SOCKET)
            closesocket(listenSocket);
        if (hIocp)
            CloseHandle(hIocp);
        WSACleanup(); // ���� Winsock ��Դ / Clean up Winsock
    }

    // ��ʼ������������ʼ�� Winsock������/��/�����׽��֡����� IOCP������ȡ AcceptEx ��չ����ָ��
    // Initialize server: start Winsock, create/bind/listen socket, create IOCP, and retrieve AcceptEx pointer.
    bool initialize() {
        WSADATA wsaData;
        int iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
        if (iResult != 0) {
            std::cerr << "WSAStartup failed. Error: " << iResult << std::endl;
            return false;
        }
        // ���������׽��� / Create listening socket
        listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (listenSocket == INVALID_SOCKET) {
            std::cerr << "Failed to create listening socket. Error: " << WSAGetLastError() << std::endl;
            return false;
        }
        // ���ò��󶨵�ַ / Configure and bind address
        sockaddr_in serverAddr{};
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_addr.s_addr = htonl(INADDR_ANY); // ������������ / Listen on all interfaces
        serverAddr.sin_port = htons(PORT);              // ���ö˿� / Set port
        if (bind(listenSocket, reinterpret_cast<sockaddr*>(&serverAddr), sizeof(serverAddr)) == SOCKET_ERROR) {
            std::cerr << "Bind failed. Error: " << WSAGetLastError() << std::endl;
            return false;
        }
        // ��ʼ���� / Start listening
        if (listen(listenSocket, SOMAXCONN) == SOCKET_ERROR) {
            std::cerr << "Listen failed. Error: " << WSAGetLastError() << std::endl;
            return false;
        }
        // ���� IOCP ��� / Create IOCP handle
        hIocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
        if (!hIocp) {
            std::cerr << "CreateIoCompletionPort failed. Error: " << GetLastError() << std::endl;
            return false;
        }
        // �������׽��ֹ����� IOCP / Associate listening socket with IOCP
        if (!CreateIoCompletionPort(reinterpret_cast<HANDLE>(listenSocket), hIocp,
            static_cast<ULONG_PTR>(listenSocket), 0)) {
            std::cerr << "Failed to associate listening socket with IOCP. Error: " << GetLastError() << std::endl;
            return false;
        }
        // һ���Ի�ȡ AcceptEx ��չ����ָ�� / Retrieve AcceptEx pointer once
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

    // ��ѭ����ʹ�����޵ȴ�ʱ����� GetQueuedCompletionStatus�������ɸ���������¼�
    // Main loop: use finite timeout for GetQueuedCompletionStatus and dispatch I/O events.
    void run() {
        // Ͷ�ݳ�ʼ AcceptEx ���� / Post initial AcceptEx operation
        postAccept();

        DWORD bytesTransferred = 0;
        ULONG_PTR completionKey = 0;
        LPOVERLAPPED lpOverlapped = nullptr;

        while (true) {
            BOOL result = GetQueuedCompletionStatus(hIocp, &bytesTransferred, &completionKey, &lpOverlapped, WAIT_TIMEOUT_MS);
            if (!result) {
                // ��ʱ����ʱ�������� / Timeout or transient error; continue looping
                if (lpOverlapped == nullptr && GetLastError() == WAIT_TIMEOUT) {
                    continue; // �� I/O �¼��������ȴ� / No I/O event, continue waiting
                }
                else {
                    std::cerr << "GetQueuedCompletionStatus failed. Error: " << GetLastError() << std::endl;
                    continue;
                }
            }
            // ��ȡ��ɵ��첽���������� / Retrieve completed operation context
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
    HANDLE hIocp;              // IOCP ��� / IOCP handle
    SOCKET listenSocket;       // �����׽��� / Listening socket
    LPFN_ACCEPTEX acceptExFunc; // AcceptEx ����ָ�� / Pointer to AcceptEx

    // Ͷ��һ���첽 AcceptEx ���������ڽ���������
    // Post an asynchronous AcceptEx operation to accept a new connection.
    void postAccept() {
        // Ϊ�����Ӵ���һ���׽��� / Create a new socket for the incoming connection.
        SOCKET acceptSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (acceptSocket == INVALID_SOCKET) {
            std::cerr << "Failed to create accept socket. Error: " << WSAGetLastError() << std::endl;
            return;
        }
        // ���䲢��ʼ�������Ķ��� / Allocate and initialize the context object.
        auto* pIOData = new PerIOData();
        pIOData->operationType = IO_OPERATION::ACCEPT;
        pIOData->socket = acceptSocket;
        // ���� AcceptEx �����첽���� / Call AcceptEx to initiate asynchronous accept.
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

    // ���� AcceptEx ����¼� / Handle completion of an AcceptEx operation.
    void handleAccept(PerIOData* pIOData) {
        SOCKET clientSocket = pIOData->socket;
        // ���¿ͻ����׽��ֹ����� IOCP / Associate the accepted socket with IOCP.
        if (!CreateIoCompletionPort(reinterpret_cast<HANDLE>(clientSocket), hIocp,
            static_cast<ULONG_PTR>(clientSocket), 0)) {
            std::cerr << "Failed to associate client socket with IOCP. Error: " << GetLastError() << std::endl;
            closesocket(clientSocket);
            delete pIOData;
            return;
        }
        // �����׽��������� (������� setsockopt(SO_UPDATE_ACCEPT_CONTEXT) )
        // Update socket context by calling setsockopt(SO_UPDATE_ACCEPT_CONTEXT)
        if (setsockopt(clientSocket, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT,
            reinterpret_cast<char*>(&listenSocket), sizeof(listenSocket)) == SOCKET_ERROR) {
            std::cerr << "setsockopt(SO_UPDATE_ACCEPT_CONTEXT) failed. Error: " << WSAGetLastError() << std::endl;
            closesocket(clientSocket);
            delete pIOData;
            return;
        }
        std::cout << "Accepted a new connection. Client socket: " << clientSocket << std::endl;
        // Ϊ������Ͷ�ݽ��ղ��� / Post a receive operation on the new connection.
        postRecv(clientSocket);
        // �ٴ�Ͷ�� AcceptEx �Ա���ܸ������� / Post another AcceptEx for subsequent connections.
        postAccept();
        // �ͷŵ�ǰ�����Ķ��� / Free the current context object.
        delete pIOData;
    }

    // �����첽��������¼���WSARecv ����ɣ� / Handle completion of a receive operation.
    void handleRecv(PerIOData* pIOData, DWORD bytesTransferred) {
        if (bytesTransferred == 0) {
            std::cout << "Client disconnected. Socket: " << pIOData->socket << std::endl;
            closesocket(pIOData->socket);
            delete pIOData;
            return;
        }
        // ��ֹ�ַ��� / Null-terminate the received data.
        pIOData->buffer[bytesTransferred] = '\0';
        std::cout << "Received data from socket " << pIOData->socket << ": " << pIOData->buffer << std::endl;
        // ���յ����ݺ��޸Ĳ�������Ϊ SEND��׼�������ݻ��Ը��ͻ���
        // After receiving data, change operation type to SEND to echo data back.
        pIOData->operationType = IO_OPERATION::SEND;
        pIOData->wsaBuf.len = bytesTransferred; // ���͵����ݳ�������յ���һ�� / Set send length equal to received bytes.
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
        // ע�⣺���ﲻɾ�� pIOData����Ϊ�������ڷ�����ɺ�Ĵ���
    }

    // �����첽��������¼� / Handle completion of a send operation.
    void handleSend(PerIOData* pIOData) {
        // ������ɺ�Ϊ��ǰ��������Ͷ�ݽ��ղ��� / After sending, post a new receive to continue communication.
        postRecv(pIOData->socket);
        delete pIOData;
    }

    // Ͷ���첽���ղ�����WSARecv�� / Post an asynchronous receive (WSARecv) operation on socket s.
    void postRecv(SOCKET s) {
        auto* pIOData = new PerIOData(s);
        pIOData->operationType = IO_OPERATION::RECV; // ���Ϊ RECV ���� / Mark as RECV.
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
