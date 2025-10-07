// server.c
// Compile: gcc server.c -o server.exe -lws2_32 -lgdi32
// Run: server.exe

#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>


#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "gdi32.lib")

#define CHAT_PORT 7777
#define STREAM_PORT 8888
#define BUFFER_SIZE 4096

// Globals for GUI
HBITMAP g_hBitmap = NULL;
CRITICAL_SECTION g_bitmapLock;
HWND g_hwnd = NULL;
int g_clientWidth = 0, g_clientHeight = 0;

// Utility: receive exact bytes
int recv_all(SOCKET s, char *buf, int len) {
    int total = 0;
    while (total < len) {
        int r = recv(s, buf + total, len - total, 0);
        if (r <= 0) return r;
        total += r;
    }
    return total;
}

// Stream receiver thread: receives frames and updates g_hBitmap
DWORD WINAPI stream_receiver_thread(LPVOID param) {
    SOCKET streamSock = (SOCKET)(size_t)param;
    while (1) {
        uint32_t netlen;
        int r = recv_all(streamSock, (char*)&netlen, sizeof(netlen));
        if (r <= 0) break;
        uint32_t len = ntohl(netlen);
        if (len == 0) continue;

        char *buf = (char*)malloc(len);
        if (!buf) break;
        r = recv_all(streamSock, buf, len);
        if (r <= 0) { free(buf); break; }

        // buf contains BMP file data: BITMAPFILEHEADER + BITMAPINFOHEADER + pixel data
        // Parse headers
        if (len < (int)(sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER))) {
            free(buf); continue;
        }
        BITMAPFILEHEADER *bf = (BITMAPFILEHEADER*)buf;
        BITMAPINFOHEADER *bi = (BITMAPINFOHEADER*)(buf + sizeof(BITMAPFILEHEADER));
        int width = bi->biWidth;
        int height = abs(bi->biHeight);
        int bits = bi->biBitCount;
        // pixel data offset:
        uint32_t pixOffset = bf->bfOffBits;
        if (pixOffset >= len) { free(buf); continue; }

        // Prepare BITMAPINFO for CreateDIBSection
        BITMAPINFO bmi;
        ZeroMemory(&bmi, sizeof(bmi));
        bmi.bmiHeader = *bi;

        // Create a DIB section and copy pixels
        HDC hdc = GetDC(NULL);
        void *pvBits = NULL;
        HBITMAP newBmp = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, &pvBits, NULL, 0);
        if (newBmp && pvBits) {
            // Copy pixel data: note BMP stores bottom-up if biHeight > 0
            char *srcPixels = buf + pixOffset;
            int rowSize = ((width * bits + 31) / 32) * 4;
            // If bi->biHeight positive (bottom-up): copy rows directly (BMP bottom-up).
            // CreateDIBSection returns a memory layout we can copy into row-wise.
            int dstRowSize = rowSize;
            // For bottom-up BMP, both use same row ordering, so copy directly:
            memcpy(pvBits, srcPixels, rowSize * height);

            // Replace global bitmap atomically
            EnterCriticalSection(&g_bitmapLock);
            if (g_hBitmap) DeleteObject(g_hBitmap);
            g_hBitmap = newBmp;
            g_clientWidth = width;
            g_clientHeight = height;
            LeaveCriticalSection(&g_bitmapLock);

            // Ask window to repaint
            if (g_hwnd) InvalidateRect(g_hwnd, NULL, FALSE);
        } else {
            if (newBmp) DeleteObject(newBmp);
        }
        ReleaseDC(NULL, hdc);
        free(buf);
    }

    closesocket(streamSock);
    return 0;
}

// Simple window proc: paints g_hBitmap
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        HDC memDC = CreateCompatibleDC(hdc);
        EnterCriticalSection(&g_bitmapLock);
        HBITMAP bmp = g_hBitmap;
        int w = g_clientWidth, h = g_clientHeight;
        LeaveCriticalSection(&g_bitmapLock);

        if (bmp) {
            HBITMAP old = (HBITMAP)SelectObject(memDC, bmp);
            BitBlt(hdc, 0, 0, w, h, memDC, 0, 0, SRCCOPY);
            SelectObject(memDC, old);
        } else {
            // fill background
            RECT rc;
            GetClientRect(hwnd, &rc);
            FillRect(hdc, &rc, (HBRUSH)(COLOR_WINDOW + 1));
            DrawTextA(hdc, "Waiting for stream...", -1, &ps.rcPaint, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }

        DeleteDC(memDC);
        EndPaint(hwnd, &ps);
    } return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default: return DefWindowProc(hwnd, msg, wParam, lParam);
    }
}

// Chat thread: handles simple chat on chatSock
DWORD WINAPI chat_thread(LPVOID param) {
    SOCKET chatSock = (SOCKET)(size_t)param;
    char buffer[BUFFER_SIZE];
    while (1) {
        int r = recv(chatSock, buffer, BUFFER_SIZE - 1, 0);
        if (r <= 0) break;
        buffer[r] = '\0';
        printf("Client: %s", buffer);
        // echo server operator input or auto-reply? For demonstration prompt server operator:
        printf("Server: ");
        if (!fgets(buffer, BUFFER_SIZE, stdin)) break;
        send(chatSock, buffer, (int)strlen(buffer), 0);
        if (strcmp(buffer, "exit\n") == 0) break;
    }
    closesocket(chatSock);
    return 0;
}

int main() {
    // Init
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) {
        printf("WSAStartup failed\n");
        return 1;
    }
    InitializeCriticalSection(&g_bitmapLock);

    // 1) Create listening sockets for chat and stream
    SOCKET listenChat = socket(AF_INET, SOCK_STREAM, 0);
    SOCKET listenStream = socket(AF_INET, SOCK_STREAM, 0);
    if (listenChat == INVALID_SOCKET || listenStream == INVALID_SOCKET) {
        printf("socket failed\n"); WSACleanup(); return 1;
    }

    struct sockaddr_in addrChat, addrStream;
    ZeroMemory(&addrChat, sizeof(addrChat));
    ZeroMemory(&addrStream, sizeof(addrStream));
    addrChat.sin_family = AF_INET; addrChat.sin_addr.s_addr = INADDR_ANY; addrChat.sin_port = htons(CHAT_PORT);
    addrStream.sin_family = AF_INET; addrStream.sin_addr.s_addr = INADDR_ANY; addrStream.sin_port = htons(STREAM_PORT);

    if (bind(listenChat, (struct sockaddr*)&addrChat, sizeof(addrChat)) == SOCKET_ERROR ||
        bind(listenStream, (struct sockaddr*)&addrStream, sizeof(addrStream)) == SOCKET_ERROR) {
        printf("bind failed: %d\n", WSAGetLastError()); closesocket(listenChat); closesocket(listenStream); WSACleanup(); return 1;
    }

    if (listen(listenChat, 1) == SOCKET_ERROR || listen(listenStream, 1) == SOCKET_ERROR) {
        printf("listen failed\n"); closesocket(listenChat); closesocket(listenStream); WSACleanup(); return 1;
    }

    printf("Server listening: chat port %d, stream port %d\n", CHAT_PORT, STREAM_PORT);
    printf("Waiting for chat client...\n");

    // Accept chat connection (blocking)
    struct sockaddr_in clientAddr; int len = sizeof(clientAddr);
    SOCKET chatClient = accept(listenChat, (struct sockaddr*)&clientAddr, &len);
    if (chatClient == INVALID_SOCKET) { printf("accept chat failed\n"); return 1; }
    printf("Chat client connected: %s\n", inet_ntoa(clientAddr.sin_addr));

    // Start chat thread
    CreateThread(NULL, 0, chat_thread, (LPVOID)(size_t)chatClient, 0, NULL);

    // Accept stream connection (blocking)
    printf("Waiting for stream client (port %d)...\n", STREAM_PORT);
    len = sizeof(clientAddr);
    SOCKET streamClient = accept(listenStream, (struct sockaddr*)&clientAddr, &len);
    if (streamClient == INVALID_SOCKET) { printf("accept stream failed\n"); return 1; }
    printf("Stream client connected: %s\n", inet_ntoa(clientAddr.sin_addr));

    // Create window for showing frames
    HINSTANCE hInst = GetModuleHandle(NULL);
    const char *className = "RemoteViewClass";
    WNDCLASS wc = { 0 };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = className;
    RegisterClass(&wc);

    // Create a sizable window; size will be updated after first frame arrives
    g_hwnd = CreateWindowA(className, "Remote Screen Viewer",
                           WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                           800, 600, NULL, NULL, hInst, NULL);
    ShowWindow(g_hwnd, SW_SHOW);

    // Start stream receiver thread
    CreateThread(NULL, 0, stream_receiver_thread, (LPVOID)(size_t)streamClient, 0, NULL);

    // Message loop for the window
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // Cleanup
    EnterCriticalSection(&g_bitmapLock);
    if (g_hBitmap) DeleteObject(g_hBitmap);
    LeaveCriticalSection(&g_bitmapLock);
    DeleteCriticalSection(&g_bitmapLock);
    closesocket(listenChat);
    closesocket(listenStream);
    WSACleanup();
    return 0;
}
