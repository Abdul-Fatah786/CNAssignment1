/* Simple TCP server (Winsock) - single-client chat example
 *
 * Flow:
 * 1) Initialize Winsock (WSAStartup)
 * 2) Create a TCP socket
 * 3) Bind the socket to a local IP/port
 * 4) Listen for incoming connections
 * 5) Accept one client connection
 * 6) Exchange text messages until either side types "exit"
 * 7) Clean up sockets and Winsock
 *
 * Notes:
 * - This example is Windows-specific (uses Winsock2). Link with -lws2_32 when compiling.
 * - The server currently handles only one client at a time. For multiple clients use threads or asynchronous I/O.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#define PORT 7777           // TCP port the server listens on
#define BUFFER_SIZE 1024    // maximum message size for this example

int main() {
    WSADATA wsa;                 // Winsock initialization structure
    SOCKET server_fd, new_socket; // server socket and peer socket
    struct sockaddr_in address;  // IPv4 address structure for bind/accept
    char buffer[BUFFER_SIZE];    // receive buffer
    char msg[BUFFER_SIZE];       // send buffer (server input)
    int addrlen = sizeof(address);

    /* Initialize Winsock (version 2.2). On success WSAStartup returns 0. */
    if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) {
        printf("WSAStartup failed. Error Code: %d\n", WSAGetLastError());
        return 1;
    }

    /* Step 1: Create a TCP socket (IPv4) */
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == INVALID_SOCKET) {
        printf("Socket creation failed. Error Code: %d\n", WSAGetLastError());
        WSACleanup();
        return 1;
    }

    /* Step 2: Bind socket to local IP/Port
     * - sin_family: AF_INET for IPv4
     * - sin_addr.s_addr: when set to INADDR_ANY, server listens on all local interfaces
     *   (you can set a specific IP here to bind only to that interface)
     * - sin_port: network byte order; use htons to convert host to network order
     */
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;  // accept connections from any local IP/interface
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) == SOCKET_ERROR) {
        printf("Bind failed. Error Code: %d\n", WSAGetLastError());
        closesocket(server_fd);
        WSACleanup();
        return 1;
    }

    /* Step 3: Listen for incoming connection requests
     * backlog parameter is 3 (max number of pending connections queued)
     */
    if (listen(server_fd, 3) == SOCKET_ERROR) {
        printf("Listen failed. Error Code: %d\n", WSAGetLastError());
        closesocket(server_fd);
        WSACleanup();
        return 1;
    }
    printf("Server is listening on port %d...\n", PORT);

    /* Step 4: Accept one client connection (blocking call)
     * accept fills 'address' with the client's address and returns a new socket
     * which should be used for subsequent communication with that client.
     */
    new_socket = accept(server_fd, (struct sockaddr *)&address, &addrlen);
    if (new_socket == INVALID_SOCKET) {
        printf("Accept failed. Error Code: %d\n", WSAGetLastError());
        closesocket(server_fd);
        WSACleanup();
        return 1;
    }
    printf("Client connected!\n");

    /* Step 5: Simple chat loop
     * - recv() reads bytes from the client. It returns the number of bytes read
     *   or <= 0 on error/disconnect.
     * - recv does NOT null-terminate the received data, so we add a '\0' at
     *   the position returned by recv() to treat the buffer as a C string for printing.
     */
    while (1) {
        memset(buffer, 0, BUFFER_SIZE);
        int valread = recv(new_socket, buffer, BUFFER_SIZE, 0);
        if (valread <= 0) {
            /* valread == 0 means connection closed gracefully; < 0 indicates error */
            printf("Client disconnected.\n");
            break;
        }
        /* Ensure buffer is null-terminated before using string functions */
        if (valread < BUFFER_SIZE) buffer[valread] = '\0';
        else buffer[BUFFER_SIZE - 1] = '\0';

        /* If client sends "exit\n" we break out and close the connection */
        if (strcmp(buffer, "exit\n") == 0) {
            printf("Client ended chat.\n");
            break;
        }
        printf("Client: %s", buffer);

        /* Read server operator input from stdin and send it to the client */
        printf("Server: ");
        fgets(msg, BUFFER_SIZE, stdin);            // fgets includes the '\n'
        send(new_socket, msg, (int)strlen(msg), 0); // not checking partial send for simplicity

        if (strcmp(msg, "exit\n") == 0) {
            printf("Server ended chat.\n");
            break;
        }
    }

    /* Step 6: Cleanup sockets and Winsock */
    closesocket(new_socket);
    closesocket(server_fd);
    WSACleanup();
    return 0;
}


// compile command for both
// client : gcc client.c -o client.exe -lws2_32
// server : gcc server.c -o server.exe -lws2_32