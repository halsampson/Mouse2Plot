// MouseVelo.cpp

// plots movement of second connected mouse 
// used to analyze motion using a reflective optical mouse

// could use code to support two different mouse cursors for shared screen interactions (MouseMux)

#include "stdafx.h"
#include "Resource.h"
#include <stdio.h>
#include "shellapi.h"
#include "time.h"
#include "math.h"
#include "windowsx.h"

#define MAX_LOADSTRING 100

int mouseCountsPerInch = 1000;  // DPI   

// Global Variables:
HINSTANCE hInst;						          		// current instance
TCHAR szTitle[MAX_LOADSTRING];				   	// The title bar text
TCHAR szWindowClass[MAX_LOADSTRING];			// the main window class name

HDC plotDC;
RECT plotRect;
HBRUSH hbrWht;

FILE* fMoves;

HHOOK miHook;

bool newPos;
long xPos, yPos, mainxPos, mainyPos;

WPARAM event;

LRESULT CALLBACK LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
  if (nCode == HC_ACTION) {
    MSLLHOOKSTRUCT& msll = *(reinterpret_cast<MSLLHOOKSTRUCT*>(lParam));
    if (wParam == WM_MOUSEMOVE) {
      if (!newPos) {
        newPos = true; //until processed
        xPos = msll.pt.x;
        yPos = msll.pt.y;
      }
      SetCursorPos(mainxPos, mainyPos);
      return -1; // don't know which mouse yet - ignore all for now
    } else { // button from main mouse, but coordinate passed includes sensor movement - how remove??
      if (msll.pt.x != mainxPos || msll.pt.y != mainyPos) {
        event = wParam;
        return -1;
      }
    }
  }

  return CallNextHookEx(miHook, nCode, wParam, lParam);
}

BOOL InitInstance(HINSTANCE hInstance, int nCmdShow) {
  hInst = hInstance; // Store instance handle in our global variable

  LPWSTR* szArglist;
  int nArgs;
  szArglist = CommandLineToArgvW(GetCommandLineW(), &nArgs);
  switch (nArgs - 1) {
    case 1: mouseCountsPerInch = _wtoi(szArglist[1]);
  }

  HWND hWnd = CreateWindow(szWindowClass, szTitle, WS_OVERLAPPEDWINDOW,
    256, 64, 1412, 2048, NULL, NULL, hInstance, NULL);
  if (!hWnd) return FALSE;

  ShowWindow(hWnd, nCmdShow);
  UpdateWindow(hWnd);
  hbrWht = CreateSolidBrush(RGB(255, 255, 255));

  SetWindowsHookEx(WH_MOUSE_LL, reinterpret_cast<HOOKPROC>(&LowLevelMouseProc), hInstance, 0);

#ifndef HID_USAGE_PAGE_GENERIC
  #define HID_USAGE_PAGE_GENERIC         ((USHORT) 0x01)
#endif

#ifndef HID_USAGE_GENERIC_MOUSE
  #define HID_USAGE_GENERIC_MOUSE        ((USHORT) 0x02)
#endif

  RAWINPUTDEVICE Rid[1];
  Rid[0].usUsagePage = HID_USAGE_PAGE_GENERIC;
  Rid[0].usUsage = HID_USAGE_GENERIC_MOUSE;
  Rid[0].dwFlags = RIDEV_INPUTSINK;
  Rid[0].hwndTarget = hWnd;
  RegisterRawInputDevices(Rid, 1, sizeof(Rid[0]));

  fopen_s(&fMoves, "Movement.csv", "a+t");

  SetTimer(hWnd, 1, 100, NULL);

  return TRUE;
}


bool once;
static double avgDx, avgDy;

void plotSensor(long dx, long dy) {
  // optical sensor input
  static long x, y;
  x += dx;
  y -= dy;

  static long rx, ry;
  rx += dx;
  ry -= dy;

  static long minRx, maxRx, minRy, maxRy;
  if (ry < minRx) minRx = rx;
  if (ry > maxRx) maxRx = rx;
  if (ry < minRy) minRy = ry;
  if (ry > maxRy) maxRy = ry;

  // detect change of direction
  static bool dir = dx + dy >= 0;   // turntable CCW -> both positive
  static long xCW = 0, yCW = 0, xCCW = 0, yCCW = 0;

  if (!dir && ry > minRy + 64) {
    dir = true;
    xCW = -minRx; yCW = -minRy;
    rx = ry = minRx = minRy = maxRx = maxRy = 0;
    SetDCPenColor(plotDC, RGB(0, 255, 0));
  }

  if (dir && ry < maxRy - 64) {
    dir = false;
    xCCW = maxRx; yCCW = maxRy;
    rx = ry = minRx = minRy = maxRx = maxRy = 0;
    SetDCPenColor(plotDC, RGB(255, 0, 0));

    if (xCW) {
      double travCW = sqrt(pow(xCW, 2) + pow(yCW, 2));
      double travCCW = sqrt(pow(xCCW, 2) + pow(yCCW, 2));
      TCHAR str[256];
      swprintf(str, sizeof(str) / 2, L"%5d %5d %5.4f %5.0f   %5d %5d %5.4f %5.0f\r",
        xCW, yCW, (double)yCW / xCW, travCW, xCCW, yCCW, (double)yCCW / xCCW, travCCW);
      OutputDebugString(str);
      if (!once) {
        once = true;
        FillRect(plotDC, &plotRect, hbrWht);
        MoveToEx(plotDC, x = 0, y = 0, NULL);
      }
    }
  }

  // plot average velocity vs. time, detect stall, alarm
  const int nAvg = 256;
  if (avgDx == 0) avgDx = abs(dx);
  if (avgDy == 0) avgDy = abs(dy);

  avgDx += (abs(dx) - avgDx) / nAvg;
  avgDy += (abs(dy) - avgDy) / nAvg;
}

void ResetPlot() {
  FillRect(plotDC, &plotRect, hbrWht);
  once = false;
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
  switch (message) {

  case WM_INPUT: {
    static RAWINPUT raw;
    UINT dwSize = sizeof(raw);
    GetRawInputData((HRAWINPUT)lParam, RID_INPUT, &raw, &dwSize, sizeof(RAWINPUTHEADER));
    // or GetRawInputBuffer for lots of movement

    if (raw.header.dwType != RIM_TYPEMOUSE) return DefWindowProc(hWnd, message, wParam, lParam);

    HANDLE mouse = raw.header.hDevice;  // sometimes 0 (from SendInput() ?)
    static HANDLE mainMouse = (HANDLE)0x20000;
    if (mouse && (unsigned int)mouse < (unsigned int)mainMouse) { // low handle = first loaded driver = main mouse
      mainMouse = mouse;
    }
    if (mouse == mainMouse) {
      if (newPos) SetCursorPos(mainxPos = xPos, mainyPos = yPos);
      LRESULT res = DefWindowProc(hWnd, message, wParam, lParam);
      newPos = false;
      return res;
    }

    plotSensor(raw.data.mouse.lLastX, raw.data.mouse.lLastY);
    SetCursorPos(mainxPos, mainyPos);
    newPos = false;    
    return 0; // handled here
  }

  case WM_COMMAND: {
    int wmId    = LOWORD(wParam);
    int wmEvent = HIWORD(wParam);
    // Parse the menu selections:
    switch (wmId) {
      case IDM_EXIT:
        DestroyWindow(hWnd);
        break;
      default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
  } break;

  case WM_SIZE :
    DefWindowProc(hWnd, message, wParam, lParam);
    GetClientRect(hWnd, &plotRect);
    plotDC = GetDC(hWnd);
    SelectObject(plotDC, GetStockObject(DC_PEN));
    ResetPlot();
    break;

  case WM_CHAR :
    ResetPlot();
    break;

  case WM_PAINT: {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hWnd, &ps);
    EndPaint(hWnd, &ps);
  } break;

  case WM_DESTROY:
    fclose(fMoves);
    UnhookWindowsHookEx(miHook);
    PostQuitMessage(0);
    break;

  case WM_TIMER:
    if (event) {
      SetCursorPos(mainxPos, mainyPos);

      static INPUT input = { 0 };
      input.type = INPUT_MOUSE;
      input.mi.dwFlags |= MOUSEEVENTF_ABSOLUTE;
      input.mi.dx = mainxPos;
      input.mi.dy = mainyPos;
      switch (event) {
        case WM_LBUTTONDOWN: input.mi.dwFlags = MOUSEEVENTF_LEFTDOWN; break;
        case WM_LBUTTONUP: input.mi.dwFlags = MOUSEEVENTF_LEFTUP; break;
        case WM_RBUTTONDOWN: input.mi.dwFlags = MOUSEEVENTF_RIGHTDOWN; break;
        case WM_RBUTTONUP: input.mi.dwFlags = MOUSEEVENTF_RIGHTUP; break;
        // more
      }
      event = 0;
      SendInput(1, &input, sizeof(INPUT));
    }

    static long xs;
    LineTo(plotDC, xs, avgDy * 100);
    if (++xs >= plotRect.right) xs = 0;

    avgDy -= avgDy / 32;
    // if (avgDy < 1) Beep(880, 20);

    break;

  default: return DefWindowProc(hWnd, message, wParam, lParam);
  }
  return 0;
}


ATOM MyRegisterClass(HINSTANCE hInstance) {
  WNDCLASSEX wcex;
  wcex.cbSize = sizeof(WNDCLASSEX);
  wcex.style = CS_HREDRAW | CS_VREDRAW;
  wcex.lpfnWndProc = WndProc;
  wcex.cbClsExtra = 0;
  wcex.cbWndExtra = 0;
  wcex.hInstance = hInstance;
  wcex.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_MOUSERAW));
  wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
  wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
  wcex.lpszMenuName = MAKEINTRESOURCE(IDC_MOUSERAW);
  wcex.lpszClassName = szWindowClass;
  wcex.hIconSm = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

  return RegisterClassEx(&wcex);
}

int APIENTRY _tWinMain(_In_ HINSTANCE hInstance,
  _In_opt_ HINSTANCE hPrevInstance,
  _In_ LPTSTR    lpCmdLine,
  _In_ int       nCmdShow) {
  UNREFERENCED_PARAMETER(hPrevInstance);
  UNREFERENCED_PARAMETER(lpCmdLine);

  // Initialize global strings
  LoadString(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
  LoadString(hInstance, IDC_MOUSERAW, szWindowClass, MAX_LOADSTRING);
  MyRegisterClass(hInstance);

  // Perform application initialization:
  nCmdShow = SW_SHOWDEFAULT; //  SW_SHOWMAXIMIZED;
  if (!InitInstance(hInstance, nCmdShow)) return FALSE;

  // Main message loop:
  MSG msg;
  while (GetMessage(&msg, NULL, 0, 0)) {
    TranslateMessage(&msg);
    DispatchMessage(&msg);
  }

  return (int)msg.wParam;
}
