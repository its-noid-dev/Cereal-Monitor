
#include <windows.h>
#include <commdlg.h>
#include <string>
#include <thread>
#include <atomic>
#include <sstream>
#include <iomanip>
#include <mutex>

#include "resource.h"

#pragma comment(lib, "Comdlg32.lib")

// =============================================
// CEREAL MONITOR
// =============================================

#define ID_PORT       101
#define ID_BAUD       102
#define ID_CONNECT    103
#define ID_REFRESH    104
#define ID_OUTPUT     105
#define ID_INPUT      106
#define ID_SEND       107
#define ID_CLEAR      108
#define ID_SAVE       109
#define ID_TIMESTAMP  110
#define ID_HEX        111
#define ID_AUTOSCROLL 112
#define ID_STATUS     113
#define ID_RX         114
#define ID_TX         115

#define WM_SERIAL_DATA (WM_APP + 1)

HWND hwndMain = nullptr;

HWND portBox = nullptr;
HWND baudBox = nullptr;
HWND outputBox = nullptr;
HWND inputBox = nullptr;

HWND connectButton = nullptr;
HWND refreshButton = nullptr;
HWND sendButton = nullptr;
HWND clearButton = nullptr;
HWND saveButton = nullptr;

HWND timestampCheck = nullptr;
HWND hexCheck = nullptr;
HWND autoScrollCheck = nullptr;

HWND statusLabel = nullptr;
HWND rxLabel = nullptr;
HWND txLabel = nullptr;

HFONT fontNormal = nullptr;
HFONT fontTitle = nullptr;
HFONT fontMono = nullptr;

HBRUSH brushBackground = nullptr;
HBRUSH brushInput = nullptr;

HANDLE serialHandle = INVALID_HANDLE_VALUE;

std::thread serialThread;

std::atomic<bool> running(false);
std::atomic<bool> connected(false);
std::atomic<bool> showTimestamp(false);
std::atomic<bool> showHex(false);

std::atomic<unsigned long long> rxBytes(0);
std::atomic<unsigned long long> txBytes(0);

std::mutex serialMutex;

const COLORREF BG = RGB(13, 18, 30);
const COLORREF CARD = RGB(23, 32, 49);
const COLORREF INPUTBG = RGB(10, 16, 27);
const COLORREF BORDER = RGB(43, 57, 77);
const COLORREF TEXT = RGB(229, 237, 247);
const COLORREF MUTED = RGB(139, 155, 177);
const COLORREF GREEN = RGB(53, 211, 133);
const COLORREF RED = RGB(245, 91, 91);
const COLORREF BLUE = RGB(83, 151, 255);

// =============================================
// HELPERS
// =============================================

void SetRounded(HWND hwnd, int width, int height, int radius = 14)
{
    if (!hwnd || width <= 0 || height <= 0)
        return;

    HRGN region = CreateRoundRectRgn(
        0, 0, width + 1, height + 1,
        radius, radius
    );

    SetWindowRgn(hwnd, region, TRUE);
}

HWND MakeControl(
    const wchar_t* type,
    const wchar_t* text,
    DWORD style,
    int id,
    int x, int y,
    int width, int height)
{
    HWND control = CreateWindowExW(
        0,
        type,
        text,
        WS_CHILD | WS_VISIBLE | style,
        x, y, width, height,
        hwndMain,
        (HMENU)(INT_PTR)id,
        GetModuleHandleW(nullptr),
        nullptr
    );

    if (control && fontNormal)
        SendMessageW(control, WM_SETFONT,
            (WPARAM)fontNormal, TRUE);

    return control;
}

void MakeRoundControl(HWND hwnd, int width, int height, int radius = 12)
{
    SetRounded(hwnd, width, height, radius);
}

void SetStatus(const std::wstring& text)
{
    if (statusLabel)
        SetWindowTextW(statusLabel, text.c_str());

    InvalidateRect(hwndMain, nullptr, FALSE);
}

void AppendOutput(const std::wstring& text)
{
    if (!outputBox || text.empty())
        return;

    int length = GetWindowTextLengthW(outputBox);

    SendMessageW(outputBox, EM_SETSEL, length, length);
    SendMessageW(outputBox, EM_REPLACESEL, FALSE,
        (LPARAM)text.c_str());

    if (SendMessageW(autoScrollCheck, BM_GETCHECK, 0, 0)
        == BST_CHECKED)
    {
        SendMessageW(outputBox, EM_SCROLLCARET, 0, 0);
    }
}

void UpdateCounters()
{
    wchar_t rx[80], tx[80];

    swprintf_s(rx, L"RX  %llu bytes", rxBytes.load());
    swprintf_s(tx, L"TX  %llu bytes", txBytes.load());

    SetWindowTextW(rxLabel, rx);
    SetWindowTextW(txLabel, tx);
}

// =============================================
// ROUNDED BUTTON DRAWING
// =============================================

void DrawButton(const DRAWITEMSTRUCT* item)
{
    if (!item || !item->hwndItem)
        return;

    HDC dc = item->hDC;
    RECT r = item->rcItem;

    bool pressed = (item->itemState & ODS_SELECTED) != 0;
    bool disabled = (item->itemState & ODS_DISABLED) != 0;

    COLORREF color = CARD;

    int id = GetDlgCtrlID(item->hwndItem);

    if (id == ID_CONNECT)
        color = connected ? RED : GREEN;
    else if (id == ID_SEND)
        color = BLUE;
    else if (id == ID_REFRESH)
        color = RGB(44, 61, 85);
    else if (id == ID_SAVE || id == ID_CLEAR)
        color = RGB(37, 51, 70);

    if (pressed)
        color = RGB(
            GetRValue(color) * 0.75,
            GetGValue(color) * 0.75,
            GetBValue(color) * 0.75
        );

    if (disabled)
        color = RGB(55, 62, 73);

    HBRUSH background = CreateSolidBrush(color);
    HPEN pen = CreatePen(PS_SOLID, 1, color);

    HGDIOBJ oldBrush = SelectObject(dc, background);
    HGDIOBJ oldPen = SelectObject(dc, pen);

    RoundRect(dc,
        r.left + 1, r.top + 1,
        r.right - 1, r.bottom - 1,
        14, 14);

    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);

    DeleteObject(background);
    DeleteObject(pen);

    wchar_t label[128] = {};

    GetWindowTextW(
        item->hwndItem,
        label,
        128
    );

    SetBkMode(dc, TRANSPARENT);

    SetTextColor(
        dc,
        disabled ? MUTED :
        ((id == ID_CONNECT && !connected) ? BG : TEXT)
    );

    HFONT oldFont = (HFONT)SelectObject(dc, fontNormal);

    DrawTextW(
        dc,
        label,
        -1,
        &r,
        DT_CENTER | DT_VCENTER | DT_SINGLELINE
    );

    SelectObject(dc, oldFont);
}

// =============================================
// COM PORTS
// =============================================

void RefreshPorts()
{
    SendMessageW(portBox, CB_RESETCONTENT, 0, 0);

    for (int i = 1; i <= 256; i++)
    {
        std::wstring port =
            L"COM" + std::to_wstring(i);

        wchar_t target[512];

        if (QueryDosDeviceW(
            port.c_str(),
            target,
            512) != 0)
        {
            SendMessageW(
                portBox,
                CB_ADDSTRING,
                0,
                (LPARAM)port.c_str()
            );
        }
    }

    if (SendMessageW(portBox, CB_GETCOUNT, 0, 0) > 0)
    {
        SendMessageW(portBox, CB_SETCURSEL, 0, 0);
        SetStatus(L"Ready");
    }
    else
    {
        SetStatus(L"No COM ports found");
    }
}

// =============================================
// CONVERSION
// =============================================

std::wstring ConvertToWide(const char* data, int length)
{
    if (length <= 0)
        return L"";

    int size = MultiByteToWideChar(
        CP_UTF8, 0, data, length, nullptr, 0
    );

    UINT page = CP_UTF8;

    if (size <= 0)
    {
        page = CP_ACP;

        size = MultiByteToWideChar(
            page, 0, data, length, nullptr, 0
        );
    }

    if (size <= 0)
        return L"";

    std::wstring result(size, L'\0');

    MultiByteToWideChar(
        page, 0, data, length,
        &result[0], size
    );

    return result;
}

std::wstring ToHex(const unsigned char* data, DWORD length)
{
    std::wstringstream stream;

    stream << std::uppercase << std::hex
        << std::setfill(L'0');

    for (DWORD i = 0; i < length; i++)
    {
        stream << std::setw(2)
            << (int)data[i] << L' ';
    }

    return stream.str();
}

// =============================================
// SERIAL THREAD
// =============================================

void SerialReadLoop()
{
    char buffer[2048];

    while (running)
    {
        DWORD bytesRead = 0;

        HANDLE handle;

        {
            std::lock_guard<std::mutex> lock(serialMutex);
            handle = serialHandle;
        }

        if (handle == INVALID_HANDLE_VALUE)
            break;

        BOOL success = ReadFile(
            handle,
            buffer,
            sizeof(buffer),
            &bytesRead,
            nullptr
        );

        if (!running)
            break;

        if (!success)
        {
            if (running)
            {
                PostMessageW(
                    hwndMain,
                    WM_SERIAL_DATA,
                    0,
                    (LPARAM)new std::wstring(
                        L"\r\n[Serial read error]\r\n"
                    )
                );
            }

            break;
        }

        if (bytesRead == 0)
            continue;

        rxBytes += bytesRead;

        std::wstring received;

        if (showHex)
        {
            received = ToHex(
                reinterpret_cast<unsigned char*>(buffer),
                bytesRead
            );
        }
        else
        {
            received = ConvertToWide(
                buffer,
                (int)bytesRead
            );
        }

        if (showTimestamp)
        {
            SYSTEMTIME time;
            GetLocalTime(&time);

            wchar_t timestamp[64];

            swprintf_s(
                timestamp,
                L"[%02d:%02d:%02d.%03d] ",
                time.wHour,
                time.wMinute,
                time.wSecond,
                time.wMilliseconds
            );

            received = std::wstring(timestamp) + received;
        }

        PostMessageW(
            hwndMain,
            WM_SERIAL_DATA,
            0,
            (LPARAM)new std::wstring(received)
        );

        PostMessageW(
            hwndMain,
            WM_SERIAL_DATA,
            1,
            0
        );
    }
}

// =============================================
// DISCONNECT
// =============================================

void DisconnectSerial()
{
    if (!connected)
        return;

    running = false;

    if (serialThread.joinable())
        serialThread.join();

    {
        std::lock_guard<std::mutex> lock(serialMutex);

        if (serialHandle != INVALID_HANDLE_VALUE)
        {
            CloseHandle(serialHandle);
            serialHandle = INVALID_HANDLE_VALUE;
        }
    }

    connected = false;

    SetWindowTextW(connectButton, L"Connect");
    SetStatus(L"Disconnected");
    InvalidateRect(hwndMain, nullptr, FALSE);
}

// =============================================
// CONNECT
// =============================================

void ConnectSerial()
{
    if (connected)
    {
        DisconnectSerial();
        return;
    }

    wchar_t port[64] = {};
    wchar_t baudText[32] = {};

    GetWindowTextW(portBox, port, 64);
    GetWindowTextW(baudBox, baudText, 32);

    if (!wcslen(port))
    {
        MessageBoxW(hwndMain,
            L"Select a COM port first.",
            L"Cereal Monitor",
            MB_OK | MB_ICONWARNING);
        return;
    }

    DWORD baud = _wtoi(baudText);

    if (!baud)
        baud = 115200;

    std::wstring path = L"\\\\.\\" + std::wstring(port);

    HANDLE handle = CreateFileW(
        path.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        0,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );

    if (handle == INVALID_HANDLE_VALUE)
    {
        MessageBoxW(hwndMain,
            L"Could not open COM port.\n\n"
            L"Check the USB cable, driver and port.",
            L"Connection Error",
            MB_OK | MB_ICONERROR);
        return;
    }

    DCB dcb = {};
    dcb.DCBlength = sizeof(dcb);

    if (!GetCommState(handle, &dcb))
    {
        CloseHandle(handle);
        SetStatus(L"Could not read port settings");
        return;
    }

    dcb.BaudRate = baud;
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fBinary = TRUE;
    dcb.fParity = FALSE;
    dcb.fDtrControl = DTR_CONTROL_ENABLE;
    dcb.fRtsControl = RTS_CONTROL_ENABLE;

    if (!SetCommState(handle, &dcb))
    {
        CloseHandle(handle);
        SetStatus(L"Could not configure port");
        return;
    }

    COMMTIMEOUTS timeouts = {};

    timeouts.ReadIntervalTimeout = 20;
    timeouts.ReadTotalTimeoutConstant = 50;
    timeouts.ReadTotalTimeoutMultiplier = 10;
    timeouts.WriteTotalTimeoutConstant = 100;
    timeouts.WriteTotalTimeoutMultiplier = 10;

    if (!SetCommTimeouts(handle, &timeouts))
    {
        CloseHandle(handle);
        SetStatus(L"Could not set timeouts");
        return;
    }

    SetupComm(handle, 4096, 4096);

    PurgeComm(
        handle,
        PURGE_RXCLEAR | PURGE_TXCLEAR
    );

    {
        std::lock_guard<std::mutex> lock(serialMutex);
        serialHandle = handle;
    }

    rxBytes = 0;
    txBytes = 0;

    UpdateCounters();

    running = true;
    connected = true;

    serialThread = std::thread(SerialReadLoop);

    SetWindowTextW(connectButton, L"Disconnect");
    SetStatus(L"Connected");
    InvalidateRect(hwndMain, nullptr, FALSE);
}

// =============================================
// SEND
// =============================================

void SendSerial()
{
    if (!connected)
    {
        SetStatus(L"Connect to a device first");
        return;
    }

    int length = GetWindowTextLengthW(inputBox);

    if (length <= 0)
        return;

    std::wstring message(length + 1, L'\0');

    GetWindowTextW(inputBox, &message[0], length + 1);
    message.resize(length);

    int bytes = WideCharToMultiByte(
        CP_UTF8, 0,
        message.c_str(), length,
        nullptr, 0, nullptr, nullptr
    );

    if (bytes <= 0)
        return;

    std::string data(bytes, '\0');

    WideCharToMultiByte(
        CP_UTF8, 0,
        message.c_str(), length,
        &data[0], bytes, nullptr, nullptr
    );

    data += "\r\n";

    DWORD written = 0;
    BOOL success = FALSE;

    {
        std::lock_guard<std::mutex> lock(serialMutex);

        if (serialHandle != INVALID_HANDLE_VALUE)
        {
            success = WriteFile(
                serialHandle,
                data.data(),
                (DWORD)data.size(),
                &written,
                nullptr
            );
        }
    }

    if (!success || written != data.size())
    {
        SetStatus(L"Send failed");
        return;
    }

    txBytes += written;

    AppendOutput(L"> " + message + L"\r\n");

    UpdateCounters();

    SetWindowTextW(inputBox, L"");
}

// =============================================
// SAVE LOG
// =============================================

void SaveLog()
{
    wchar_t filename[MAX_PATH] = L"cool_cereal_log.txt";

    OPENFILENAMEW dialog = {};

    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = hwndMain;
    dialog.lpstrFilter =
        L"Text Files (*.txt)\0*.txt\0All Files (*.*)\0*.*\0";
    dialog.lpstrFile = filename;
    dialog.nMaxFile = MAX_PATH;
    dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    dialog.lpstrDefExt = L"txt";

    if (!GetSaveFileNameW(&dialog))
        return;

    int length = GetWindowTextLengthW(outputBox);

    std::wstring content(length + 1, L'\0');

    GetWindowTextW(outputBox, &content[0], length + 1);
    content.resize(length);

    int bytes = WideCharToMultiByte(
        CP_UTF8, 0,
        content.c_str(), length,
        nullptr, 0, nullptr, nullptr
    );

    HANDLE file = CreateFileW(
        filename,
        GENERIC_WRITE,
        0,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );

    if (file == INVALID_HANDLE_VALUE)
    {
        SetStatus(L"Save failed");
        return;
    }

    const unsigned char bom[] = { 0xEF, 0xBB, 0xBF };
    DWORD written = 0;

    WriteFile(file, bom, 3, &written, nullptr);

    if (bytes > 0)
    {
        std::string utf8(bytes, '\0');

        WideCharToMultiByte(
            CP_UTF8, 0,
            content.c_str(), length,
            &utf8[0], bytes, nullptr, nullptr
        );

        WriteFile(file, utf8.data(), bytes, &written, nullptr);
    }

    CloseHandle(file);

    SetStatus(L"Log saved");
}

// =============================================
// INTERFACE
// =============================================

void CreateInterface(HWND hwnd)
{
    hwndMain = hwnd;

    fontNormal = CreateFontW(
        16, 0, 0, 0, FW_NORMAL,
        FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH, L"Segoe UI"
    );

    fontTitle = CreateFontW(
        25, 0, 0, 0, FW_BOLD,
        FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH, L"Segoe UI"
    );

    fontMono = CreateFontW(
        15, 0, 0, 0, FW_NORMAL,
        FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        FIXED_PITCH, L"Cascadia Mono"
    );

    HWND title = MakeControl(
        L"STATIC", L"Cereal Monitor",
        SS_LEFT, 0, 24, 18, 350, 38
    );

    SendMessageW(title, WM_SETFONT, (WPARAM)fontTitle, TRUE);

    MakeControl(
        L"STATIC", L"USB SERIAL COMMUNICATION",
        SS_LEFT, 0, 26, 56, 300, 22
    );

    MakeControl(
        L"STATIC", L"PORT",
        SS_LEFT, 0, 25, 105, 100, 20
    );

    portBox = MakeControl(
        L"COMBOBOX", L"",
        CBS_DROPDOWNLIST | WS_VSCROLL,
        ID_PORT, 25, 130, 150, 280
    );


    MakeControl(
        L"STATIC", L"BAUD RATE",
        SS_LEFT, 0, 195, 105, 120, 20
    );

    baudBox = MakeControl(
        L"COMBOBOX", L"",
        CBS_DROPDOWNLIST,
        ID_BAUD, 195, 130, 125, 280
    );


    const wchar_t* rates[] = {
        L"300", L"1200", L"2400", L"4800",
        L"9600", L"19200", L"38400",
        L"57600", L"115200", L"230400",
        L"460800", L"921600"
    };

    for (auto rate : rates)
        SendMessageW(baudBox, CB_ADDSTRING, 0, (LPARAM)rate);

    SendMessageW(baudBox, CB_SETCURSEL, 8, 0);

    refreshButton = MakeControl(
        L"BUTTON", L"Refresh",
        BS_OWNERDRAW,
        ID_REFRESH, 345, 126, 95, 38
    );

    connectButton = MakeControl(
        L"BUTTON", L"Connect",
        BS_OWNERDRAW,
        ID_CONNECT, 450, 126, 125, 38
    );

    MakeControl(
        L"STATIC", L"SERIAL TERMINAL",
        SS_LEFT, 0, 25, 190, 220, 24
    );

    outputBox = MakeControl(
        L"EDIT", L"",
        ES_MULTILINE | ES_AUTOVSCROLL |
        ES_READONLY | WS_VSCROLL,
        ID_OUTPUT, 25, 220, 830, 340
    );

    SendMessageW(outputBox, WM_SETFONT, (WPARAM)fontMono, TRUE);
    MakeRoundControl(outputBox, 830, 340, 14);

    inputBox = MakeControl(
        L"EDIT", L"",
        ES_AUTOHSCROLL,
        ID_INPUT, 25, 580, 700, 42
    );

    MakeRoundControl(inputBox, 700, 42, 12);

    sendButton = MakeControl(
        L"BUTTON", L"Send",
        BS_OWNERDRAW,
        ID_SEND, 745, 580, 110, 42
    );

    clearButton = MakeControl(
        L"BUTTON", L"Clear",
        BS_OWNERDRAW,
        ID_CLEAR, 25, 645, 85, 36
    );

    saveButton = MakeControl(
        L"BUTTON", L"Save Log",
        BS_OWNERDRAW,
        ID_SAVE, 120, 645, 105, 36
    );

    timestampCheck = MakeControl(
        L"BUTTON", L"Timestamps",
        BS_AUTOCHECKBOX,
        ID_TIMESTAMP, 255, 650, 110, 25
    );

    hexCheck = MakeControl(
        L"BUTTON", L"HEX Mode",
        BS_AUTOCHECKBOX,
        ID_HEX, 375, 650, 100, 25
    );

    autoScrollCheck = MakeControl(
        L"BUTTON", L"Auto Scroll",
        BS_AUTOCHECKBOX,
        ID_AUTOSCROLL, 485, 650, 110, 25
    );

    SendMessageW(autoScrollCheck, BM_SETCHECK, BST_CHECKED, 0);

    rxLabel = MakeControl(
        L"STATIC", L"RX  0 bytes",
        SS_LEFT, ID_RX, 630, 650, 105, 25
    );

    txLabel = MakeControl(
        L"STATIC", L"TX  0 bytes",
        SS_LEFT, ID_TX, 740, 650, 105, 25
    );

    statusLabel = MakeControl(
        L"STATIC", L"Disconnected",
        SS_LEFT, ID_STATUS, 25, 695, 300, 25
    );

    RefreshPorts();
}

// =============================================
// WINDOW PROCEDURE
// =============================================

LRESULT CALLBACK WindowProc(
    HWND hwnd,
    UINT msg,
    WPARAM wParam,
    LPARAM lParam)
{
    switch (msg)
    {
    case WM_CREATE:
        CreateInterface(hwnd);
        return 0;

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);

        RECT client;
        GetClientRect(hwnd, &client);

        HBRUSH bg = CreateSolidBrush(BG);
        FillRect(dc, &client, bg);
        DeleteObject(bg);

        HPEN border = CreatePen(PS_SOLID, 1, BORDER);
        HGDIOBJ oldPen = SelectObject(dc, border);
        HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));

        RoundRect(dc, 14, 90, client.right - 14, 175, 15, 15);
        RoundRect(dc, 14, 205, client.right - 14, client.bottom - 90, 18, 18);
        RoundRect(dc, 14, client.bottom - 80, client.right - 14, client.bottom - 10, 15, 15);

        SelectObject(dc, oldBrush);
        SelectObject(dc, oldPen);
        DeleteObject(border);

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_DRAWITEM:
        DrawButton((DRAWITEMSTRUCT*)lParam);
        return TRUE;

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case ID_CONNECT:
            ConnectSerial();
            break;

        case ID_REFRESH:
            if (!connected)
                RefreshPorts();
            break;

        case ID_SEND:
            SendSerial();
            break;

        case ID_CLEAR:
            SetWindowTextW(outputBox, L"");
            break;

        case ID_SAVE:
            SaveLog();
            break;

        case ID_TIMESTAMP:
            showTimestamp =
                SendMessageW(timestampCheck, BM_GETCHECK, 0, 0)
                == BST_CHECKED;
            break;

        case ID_HEX:
            showHex =
                SendMessageW(hexCheck, BM_GETCHECK, 0, 0)
                == BST_CHECKED;
            break;
        }

        return 0;

    case WM_KEYDOWN:
        return 0;

    case WM_SERIAL_DATA:
    {
        if (wParam == 1)
        {
            UpdateCounters();
            return 0;
        }

        std::wstring* data =
            reinterpret_cast<std::wstring*>(lParam);

        if (data)
        {
            AppendOutput(*data);
            delete data;
        }

        return 0;
    }

    case WM_CTLCOLOREDIT:
    {
        HDC dc = (HDC)wParam;

        SetTextColor(dc, GREEN);
        SetBkColor(dc, INPUTBG);

        return (LRESULT)brushInput;
    }

    case WM_CTLCOLORSTATIC:
    {
        HDC dc = (HDC)wParam;

        SetTextColor(dc, TEXT);
        SetBkColor(dc, BG);

        return (LRESULT)brushBackground;
    }

    case WM_CTLCOLORLISTBOX:
    {
        HDC dc = (HDC)wParam;

        SetTextColor(dc, TEXT);
        SetBkColor(dc, INPUTBG);

        return (LRESULT)brushInput;
    }

    case WM_CTLCOLORBTN:
    {
        HDC dc = (HDC)wParam;
        SetTextColor(dc, TEXT);
        SetBkColor(dc, BG);
        return (LRESULT)brushBackground;
    }

    case WM_SIZE:
    {
        int width = LOWORD(lParam);
        int height = HIWORD(lParam);

        if (outputBox)
        {
            int terminalHeight = height - 365;

            if (terminalHeight < 150)
                terminalHeight = 150;

            MoveWindow(
                outputBox,
                25, 220,
                width - 50,
                terminalHeight,
                TRUE
            );

            MakeRoundControl(outputBox, width - 50, terminalHeight, 14);

            MoveWindow(
                inputBox,
                25, height - 100,
                width - 190,
                42,
                TRUE
            );

            MakeRoundControl(inputBox, width - 190, 42, 12);

            MoveWindow(
                sendButton,
                width - 155, height - 100,
                110, 42,
                TRUE
            );

            MoveWindow(clearButton, 25, height - 35, 85, 30, TRUE);
            MoveWindow(saveButton, 120, height - 35, 105, 30, TRUE);

            MoveWindow(timestampCheck, 255, height - 30, 110, 25, TRUE);
            MoveWindow(hexCheck, 375, height - 30, 100, 25, TRUE);
            MoveWindow(autoScrollCheck, 485, height - 30, 110, 25, TRUE);

            MoveWindow(rxLabel, width - 270, height - 30, 115, 25, TRUE);
            MoveWindow(txLabel, width - 145, height - 30, 115, 25, TRUE);

            InvalidateRect(hwnd, nullptr, FALSE);
        }

        return 0;
    }

    case WM_GETMINMAXINFO:
    {
        MINMAXINFO* info = (MINMAXINFO*)lParam;
        info->ptMinTrackSize.x = 800;
        info->ptMinTrackSize.y = 650;
        return 0;
    }

    case WM_DESTROY:
    {
        DisconnectSerial();

        MSG pending;

        while (PeekMessageW(
            &pending,
            hwnd,
            WM_SERIAL_DATA,
            WM_SERIAL_DATA,
            PM_REMOVE))
        {
            if (pending.wParam == 0)
                delete reinterpret_cast<std::wstring*>(pending.lParam);
        }

        if (fontNormal) DeleteObject(fontNormal);
        if (fontTitle) DeleteObject(fontTitle);
        if (fontMono) DeleteObject(fontMono);

        if (brushBackground) DeleteObject(brushBackground);
        if (brushInput) DeleteObject(brushInput);

        PostQuitMessage(0);
        return 0;
    }
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// =============================================
// MAIN
// =============================================

int WINAPI wWinMain(
    HINSTANCE instance,
    HINSTANCE,
    PWSTR,
    int show)
{
    brushBackground = CreateSolidBrush(BG);
    brushInput = CreateSolidBrush(INPUTBG);

    WNDCLASSEXW wc = {};

    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = instance;
    wc.lpszClassName = L"CerealMonitorWindow";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(
        instance,
        MAKEINTRESOURCEW(IDI_CEREALMONITOR)
    );

    if (!wc.hIcon)
        wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);

    wc.hIconSm = wc.hIcon;
    wc.hbrBackground = brushBackground;

    if (!RegisterClassExW(&wc))
        return 1;

    hwndMain = CreateWindowExW(
        0,
        wc.lpszClassName,
        L"Cereal Monitor",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        920,
        780,
        nullptr,
        nullptr,
        instance,
        nullptr
    );

    if (!hwndMain)
        return 1;

    SendMessageW(hwndMain, WM_SETICON, ICON_BIG, (LPARAM)wc.hIcon);
    SendMessageW(hwndMain, WM_SETICON, ICON_SMALL, (LPARAM)wc.hIconSm);

    ShowWindow(hwndMain, show);
    UpdateWindow(hwndMain);

    MSG msg = {};

    while (GetMessageW(&msg, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return (int)msg.wParam;
}
