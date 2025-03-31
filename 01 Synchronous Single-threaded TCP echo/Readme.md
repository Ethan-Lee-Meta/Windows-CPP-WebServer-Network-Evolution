# 详细解释（服务器部分）
# Detailed Explanation (Server Part)

## 头文件说明
## Header Files Explanation

- <winsock2.h>：提供 Winsock API 的基本函数与数据类型。
  - Provides core Winsock API functions and types.
- <ws2tcpip.h>：提供 IP 地址转换函数等扩展。
  - Provides additional functions like IP address conversion.
- <iostream> 与 <string>：用于标准输入输出和字符串操作。
  - For standard I/O and string operations.
  
## 步骤说明
## Step-by-Step Explanation

1. 初始化 Winsock：调用 WSAStartup 并检查返回值。
   - Initializes the Winsock library.
2. 创建监听套接字：调用 socket 创建 TCP 套接字。
   - Creates a TCP socket.
3. 绑定套接字：使用 bind 将套接字绑定到所有接口上的端口 8080。
   - Binds the socket to IP (all interfaces) and port 8080.
4. 监听连接：使用 listen 开始监听传入连接。
   - Puts the socket into listening mode.
5. 接受连接：使用 accept 阻塞等待客户端连接。
   - Accepts an incoming connection (blocking).
6. 通信循环：通过 recv 和 send 进行数据交换（echo 服务）。
   - Receives data from the client and echoes it back.
7. 关闭连接：使用 shutdown 关闭连接的发送方向，然后清理所有资源。
   - Shuts down and cleans up.


# 详细解释（客户端部分）
# Detailed Explanation (Client Part)

## 头文件说明
## Header Files Explanation


- <winsock2.h> 与 <ws2tcpip.h>：提供 Winsock API 和辅助函数，如 inet_pton。
  - Provide the Winsock API and auxiliary functions (e.g., inet_pton) for network programming.
- <iostream> 与 <string>：用于输入输出和字符串操作。
  - Used for input/output streams and string operations.
  
## 步骤说明
## Step-by-Step Explanation


1. 初始化 Winsock：调用 WSAStartup 初始化网络库。
   - Initialize Winsock: Call WSAStartup to initialize the network library.
3. 创建套接字：使用 socket() 创建 TCP 套接字。
   - Create a Socket: Use socket() to create a TCP socket.
5. 设置服务器地址：构造 sockaddr_in 结构，设置地址族、端口（8080）和 IP 地址（127.0.0.1）。
   - Set Up Server Address: Construct a sockaddr_in structure.
   - Set the address family (e.g., AF_INET), the port (27015), and the IP address (127.0.0.1).
7. 连接到服务器：调用 connect() 阻塞连接到服务器。
   - Connect to the Server: Call connect() to establish a blocking connection to the server.
9. 通信循环：用户输入数据，通过 send() 发送给服务器，然后用 recv() 接收服务器回显数据。
    - Communication Loop: The user inputs data, which is sent to the server using send().
    - Then, use recv() to receive the echo response from the server.
11. 清理资源：关闭套接字和调用 WSACleanup 释放网络资源。
    - Cleanup Resources: Close the socket and call WSACleanup() to release the network resources.


# 标准步骤：初始化 Winsock → 创建套接字 → 绑定地址 → 监听 → 接受连接 → 通信循环 → 清理资源。
  # Standard Steps:
  - Initialize Winsock → Create Socket → Bind Address → Listen → Accept Connection → Communication Loop → Cleanup Resources.

## 每个头文件：
  ## Header Files:
  
- <winsock2.h> 和 <ws2tcpip.h> 用于网络编程；
  - <winsock2.h> and <ws2tcpip.h>: Used for network programming.

- <iostream> 和 <string> 用于输入输出和字符串操作。
  - <iostream> and <string>: Used for input/output operations and string handling.

## 每个函数：
## Functions:

* WSAStartup：初始化网络库；
  * WSAStartup: Initializes the network library.
* socket：创建套接字；
  * socket: Creates a socket.
* bind：绑定 IP 和端口；
  * bind: Binds an IP address and port to the socket.
* listen：监听连接；
  * listen: Puts the socket into a listening state for incoming connections.
* accept：接受连接（阻塞）；
  * accept: Accepts an incoming connection (blocking call).
* recv：接收数据（阻塞）；
  * recv: Receives data (blocking call).
* send：发送数据（阻塞）；
  * send: Sends data (blocking call).
* shutdown：关闭连接；
  * shutdown: Shuts down the connection.
* closesocket 和 WSACleanup：释放资源。
  * closesocket and WSACleanup: Release resources.








