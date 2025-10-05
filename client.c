#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#define PORT 7777
#define BUFFER_SIZE 1024

int main() {
    WSADATA wsa;
    SOCKET sock = 0;
    struct sockaddr_in serv_addr;
    char buffer[BUFFER_SIZE];
    char msg[BUFFER_SIZE];

    // Initialize Winsock
    if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) {
        printf("WSAStartup failed. Error Code: %d\n", WSAGetLastError());
        return 1;
    }

    // Step 1: Create socket
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) == INVALID_SOCKET) {
        printf("Socket creation error. Error Code: %d\n", WSAGetLastError());
        WSACleanup();
        return 1;
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);
    serv_addr.sin_addr.s_addr = inet_addr("127.0.0.1");  // ✅ use inet_addr
    // what if this another IP from another PC 

    // Step 2: Connect to server
        if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) == SOCKET_ERROR) {
        printf("Connection Failed. Error Code: %d\n", WSAGetLastError());
        closesocket(sock);
        WSACleanup();
        return 1;
    }
    printf("Connected to server. Type 'exit' to quit.\n");

    // Step 3: Chat loop
    while (1) {
        printf("Client: ");
        fgets(msg, BUFFER_SIZE, stdin);
        send(sock, msg, (int)strlen(msg), 0);

        if (strcmp(msg, "exit\n") == 0) {
            printf("Client ended chat.\n");
            break;
        }

        memset(buffer, 0, BUFFER_SIZE);
        int valread = recv(sock, buffer, BUFFER_SIZE, 0);
        if (valread <= 0) {
            printf("Server disconnected.\n");
            break;
        }
            // ensure buffer is null-terminated
            if (valread < BUFFER_SIZE) buffer[valread] = '\0';
            else buffer[BUFFER_SIZE - 1] = '\0';
        printf("Server: %s", buffer);

        if (strcmp(buffer, "exit\n") == 0) {
            printf("Server ended chat.\n");
            break;
        }
    }

    // Step 4: Close socket
    closesocket(sock);
    WSACleanup();
    return 0;
}
