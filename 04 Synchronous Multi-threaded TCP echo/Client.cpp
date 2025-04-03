// Client.cpp
// 说明：这是一个基于 Windows Socket 的客户端示例。
// 功能：客户端连接到服务器，提示用户输入消息，发送用户输入的数据给服务器；
//      同时启动一个线程负责接收服务器的回复（服务器回复前缀 "Server:"）。
//      用户输入 "exit" 时主动结束会话。
// 编译时请确保链接 ws2_32.lib

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iostream>
#include <thread>
#include <string>

#pragma comment(lib, "ws2_32.lib")

// ------------------- RAII 类 -------------------------

// WSAInitializer：初始化 WinSock 库
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

// Socket 类：封装 SOCKET 句柄，自动释放资源
class Socket {
public:
    explicit Socket(SOCKET s = INVALID_SOCKET) : sock(s) {}
    ~Socket() {
        if (sock != INVALID_SOCKET) {
            closesocket(sock);
        }
    }
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
    Socket(Socket&& other) noexcept : sock(other.sock) {
        other.sock = INVALID_SOCKET;
    }
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
    SOCKET get() const { return sock; }
private:
    SOCKET sock;
};

// ------------------- 接收线程函数 -------------------------

// receive_thread：专门负责从服务器接收数据，并打印接收到的消息
void receive_thread(Socket& clientSocket) {
    const int bufSize = 1024;
    char buffer[bufSize];
    while (true) {
        int bytesReceived = recv(clientSocket.get(), buffer, bufSize - 1, 0);
        if (bytesReceived > 0) {
            buffer[bytesReceived] = '\0'; // 确保字符串以 '\0' 结尾
            std::cout << "Received: " << buffer << std::endl;
        }
        else if (bytesReceived == 0) {
            std::cout << "Server closed connection." << std::endl;
            break;
        }
        else {
            std::cerr << "recv() failed with error: " << WSAGetLastError() << std::endl;
            break;
        }
    }
}

// ------------------- 主函数 -------------------------

int main() {
    try {
        WSAInitializer wsa; // 初始化 WinSock

        // 创建客户端 Socket
        Socket clientSocket(socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
        if (clientSocket.get() == INVALID_SOCKET) {
            throw std::runtime_error("socket() failed with error: " + std::to_string(WSAGetLastError()));
        }

        // 设置服务器地址（这里以127.0.0.1，端口8888为例）
        sockaddr_in serverAddr{};
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_port = htons(8888);
        // 使用 InetPtonA 将 IPv4 地址字符串转换为二进制格式
        if (InetPtonA(AF_INET, "127.0.0.1", &serverAddr.sin_addr) != 1) {
            std::cerr << "Invalid address format" << std::endl;
            return 1;
        }

        // 连接到服务器
        if (connect(clientSocket.get(), reinterpret_cast<sockaddr*>(&serverAddr), sizeof(serverAddr)) == SOCKET_ERROR) {
            throw std::runtime_error("connect() failed with error: " + std::to_string(WSAGetLastError()));
        }
        std::cout << "Connected to server." << std::endl;

        // 启动一个线程用于接收服务器消息
        std::thread receiver(receive_thread, std::ref(clientSocket));

        // 主线程提示用户输入消息，并将用户输入发送给服务器
        std::string input;
        std::cout << "Enter messages to send to the server. Type 'exit' to quit." << std::endl;
        while (true) {
            std::getline(std::cin, input);
            if (input == "exit") {
                break; // 用户输入 exit 时退出循环，结束会话
            }
            // 发送用户输入的消息
            int bytesSent = send(clientSocket.get(), input.c_str(), (int)input.size(), 0);
            if (bytesSent == SOCKET_ERROR) {
                std::cerr << "send() failed with error: " << WSAGetLastError() << std::endl;
                break;
            }
        }
        // 用户结束输入后，关闭发送方向，通知服务器不再发送数据
        if (shutdown(clientSocket.get(), SD_SEND) == SOCKET_ERROR) {
            std::cerr << "shutdown() failed with error: " << WSAGetLastError() << std::endl;
        }
        // 等待接收线程结束
        if (receiver.joinable()) {
            receiver.join();
        }
    }
    catch (const std::exception& ex) {
        std::cerr << "Exception in client: " << ex.what() << std::endl;
        return 1;
    }
    return 0;
}
