#include <windows.h>
#include <iostream>
#include <string>
#include <chrono>
#include <thread>

bool SaveBitmapToFile(HBITMAP hBitmap, HDC hDC, const std::string& filename) {
  BITMAP bmp;
  GetObject(hBitmap, sizeof(BITMAP), &bmp);

  BITMAPFILEHEADER bmfHeader;
  BITMAPINFOHEADER bi;

  bi.biSize = sizeof(BITMAPINFOHEADER);
  bi.biWidth = bmp.bmWidth;
  bi.biHeight = -bmp.bmHeight;  // Top-down bitmap
  bi.biPlanes = 1;
  bi.biBitCount = 32;
  bi.biCompression = BI_RGB;
  bi.biSizeImage = 0;
  bi.biXPelsPerMeter = 0;
  bi.biYPelsPerMeter = 0;
  bi.biClrUsed = 0;
  bi.biClrImportant = 0;

  DWORD bmpSize = ((bmp.bmWidth * bi.biBitCount + 31) / 32) * 4 * bmp.bmHeight;
  HANDLE hDIB = GlobalAlloc(GHND, bmpSize);
  char* lpbitmap = (char*)GlobalLock(hDIB);

  GetDIBits(hDC, hBitmap, 0, bmp.bmHeight, lpbitmap, (BITMAPINFO*)&bi, DIB_RGB_COLORS);

  HANDLE hFile = CreateFileA(filename.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (hFile == INVALID_HANDLE_VALUE) return false;

  DWORD totalSize = bmpSize + sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);

  bmfHeader.bfType = 0x4D42;
  bmfHeader.bfSize = totalSize;
  bmfHeader.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);

  DWORD bytesWritten;
  WriteFile(hFile, &bmfHeader, sizeof(bmfHeader), &bytesWritten, nullptr);
  WriteFile(hFile, &bi, sizeof(bi), &bytesWritten, nullptr);
  WriteFile(hFile, lpbitmap, bmpSize, &bytesWritten, nullptr);

  CloseHandle(hFile);
  GlobalUnlock(hDIB);
  GlobalFree(hDIB);

  return true;
}

void TakeScreenshotRegion(int x, int y, int width, int height, int count) {
  HDC hScreen = GetDC(nullptr);
  HDC hMem = CreateCompatibleDC(hScreen);
  HBITMAP hBitmap = CreateCompatibleBitmap(hScreen, width, height);
  SelectObject(hMem, hBitmap);

  BitBlt(hMem, 0, 0, width, height, hScreen, x, y, SRCCOPY);

  std::string filename = "screenshot_" + std::to_string(count) + ".bmp";
  SaveBitmapToFile(hBitmap, hMem, filename);

  std::cout << "Screenshot gespeichert: " << filename << "\n";

  DeleteObject(hBitmap);
  DeleteDC(hMem);
  ReleaseDC(nullptr, hScreen);
}

int main() {
  const int regionWidth = 1080;
  const int regionHeight = 500;
  const int posX = 1380;
  const int posY = 550;
  const int intervalSeconds = 1;

  int count = 1;
  std::cout << "Starte Screenshot-Programm...\n";
  while (true) {
    TakeScreenshotRegion(posX, posY, regionWidth, regionHeight, count++);
    std::this_thread::sleep_for(std::chrono::seconds(intervalSeconds));
  }

  return 0;
}
