// client.c
// Compile: gcc client.c -o client.exe -lws2_32 -lgdi32
// Run: client.exe <server_ip>

#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "gdi32.lib")

#define CHAT_PORT 7777
#define STREAM_PORT 8888
#define FRAME_INTERVAL_MS 100  // ~10 FPS

volatile int streaming = 0;
SOCKET streamSock = INVALID_SOCKET;

// ✅ send_all helper
int send_all(SOCKET s, char *buf, int len) {
    int total = 0;
    while (total < len) {
        int sent = send(s, buf + total, len - total, 0);
        if (sent == SOCKET_ERROR) return -1;
        total += sent;
    }
    return total;
}

// ✅ Capture screen into BMP (24-bit)
char *capture_screen_bmp(int *outLen) {
    HWND hwnd = GetDesktopWindow();
    HDC hdcScreen = GetDC(hwnd);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    int width = GetSystemMetrics(SM_CXSCREEN);
    int height = GetSystemMetrics(SM_CYSCREEN);
    HBITMAP hbm = CreateCompatibleBitmap(hdcScreen, width, height);
    HBITMAP old = (HBITMAP)SelectObject(hdcMem, hbm);
    BitBlt(hdcMem, 0, 0, width, height, hdcScreen, 0, 0, SRCCOPY);

    BITMAPINFOHEADER bi = {0};
    bi.biSize = sizeof(BITMAPINFOHEADER);
    bi.biWidth = width;
    bi.biHeight = height;
    bi.biPlanes = 1;
    bi.biBitCount = 24;
    bi.biCompression = BI_RGB;

    int rowSize = ((width * bi.biBitCount + 31) / 32) * 4;
    int pixelBytes = rowSize * height;
    int totalSize = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + pixelBytes;

    char *buf = (char*)malloc(totalSize);
    if (!buf) return NULL;

    BITMAPFILEHEADER bf = {0};
    bf.bfType = 0x4D42;
    bf.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    bf.bfSize = totalSize;

    memcpy(buf, &bf, sizeof(bf));
    memcpy(buf + sizeof(bf), &bi, sizeof(bi));

    char *pixelPtr = buf + bf.bfOffBits;
    if (GetDIBits(hdcScreen, hbm, 0, height, pixelPtr, (BITMAPINFO*)&bi, DIB_RGB_COLORS) == 0) {
        free(buf);
        buf = NULL;
    }

    SelectObject(hdcMem, old);
    DeleteObject(hbm);
    DeleteDC(hdcMem);
    ReleaseDC(hwnd, hdcScreen);

    if (buf) *outLen = totalSize;
    return buf;
}

// ✅ fallback for inet_pton on MinGW
int inet_pton_win(int af, const char *src, void *dst) {
    struct sockaddr_in sa;
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = inet_addr(src);
    if (sa.sin_addr.s_addr == INADDR_NONE) return 0;
    memcpy(dst, &sa.sin_addr, sizeof(struct in_addr));
    return 1;
}

// ✅ Screen capture thread
DWORD WINAPI capture_thread(LPVOID param) {
    (void)param;
    while (1) {
        if (!streaming) { Sleep(100); continue; }
        int len = 0;
        char *bmp = capture_screen_bmp(&len);
        if (!bmp) { Sleep(FRAME_INTERVAL_MS); continue; }

        uint32_t nlen = htonl((uint32_t)len);
        if (send_all(streamSock, (char*)&nlen, sizeof(nlen)) == -1) break;
        if (send_all(streamSock, bmp, len) == -1) break;

        free(bmp);
        Sleep(FRAME_INTERVAL_MS);
    }
    return 0;
}

// ✅ main()
int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: %s <server_ip>\n", argv[0]);
        return 1;
    }
    const char *server_ip = argv[1];

    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) {
        printf("WSAStartup failed.\n");
        return 1;
    }

    // --- Chat socket ---
    SOCKET chatSock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in servAddr = {0};
    servAddr.sin_family = AF_INET;
    servAddr.sin_port = htons(CHAT_PORT);
    inet_pton_win(AF_INET, server_ip, &servAddr.sin_addr);

    if (connect(chatSock, (struct sockaddr*)&servAddr, sizeof(servAddr)) == SOCKET_ERROR) {
        printf("Chat connect failed: %d\n", WSAGetLastError());
        return 1;
    }
    printf("Connected to chat server %s:%d\n", server_ip, CHAT_PORT);

    // --- Stream socket ---
    streamSock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in servStream = {0};
    servStream.sin_family = AF_INET;
    servStream.sin_port = htons(STREAM_PORT);
    inet_pton_win(AF_INET, server_ip, &servStream.sin_addr);

    if (connect(streamSock, (struct sockaddr*)&servStream, sizeof(servStream)) == SOCKET_ERROR) {
        printf("Stream connect failed: %d\n", WSAGetLastError());
        streamSock = INVALID_SOCKET;
    } else {
        printf("Connected to stream server %s:%d\n", server_ip, STREAM_PORT);
    }

    CreateThread(NULL, 0, capture_thread, NULL, 0, NULL);

    char buf[1024];
    while (1) {
        printf("Client: ");
        if (!fgets(buf, sizeof(buf), stdin)) break;

        if (strcmp(buf, "start\n") == 0) { streaming = 1; printf("Streaming started.\n"); continue; }
        if (strcmp(buf, "stop\n") == 0) { streaming = 0; printf("Streaming stopped.\n"); continue; }

        send(chatSock, buf, (int)strlen(buf), 0);
        if (strcmp(buf, "exit\n") == 0) break;

        int r = recv(chatSock, buf, sizeof(buf) - 1, 0);
        if (r <= 0) { printf("Server disconnected chat.\n"); break; }
        buf[r] = '\0';
        printf("Server: %s", buf);
    }

    streaming = 0;
    if (streamSock != INVALID_SOCKET) closesocket(streamSock);
    closesocket(chatSock);
    WSACleanup();
    return 0;
}
