#include <windows.h>
#include <iostream>
#include <string>
#include <chrono>
#include <thread>
#include <vector>
#include <algorithm>

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
  const int scaledWidth = width / 2;
  const int scaledHeight = height / 2;

  HDC hScreen = GetDC(nullptr);
  HDC hMemOriginal = CreateCompatibleDC(hScreen);
  HBITMAP hBitmapOriginal = CreateCompatibleBitmap(hScreen, width, height);
  SelectObject(hMemOriginal, hBitmapOriginal);

  BitBlt(hMemOriginal, 0, 0, width, height, hScreen, x, y, SRCCOPY);

  // Skalieren
  HDC hMemScaled = CreateCompatibleDC(hScreen);
  HBITMAP hBitmapScaled = CreateCompatibleBitmap(hScreen, scaledWidth, scaledHeight);
  SelectObject(hMemScaled, hBitmapScaled);
  SetStretchBltMode(hMemScaled, HALFTONE);
  StretchBlt(hMemScaled, 0, 0, scaledWidth, scaledHeight,
    hMemOriginal, 0, 0, width, height, SRCCOPY);

  // Pixel verarbeiten
  BITMAPINFO bmi = {};
  bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bmi.bmiHeader.biWidth = scaledWidth;
  bmi.bmiHeader.biHeight = -scaledHeight; // Top-down
  bmi.bmiHeader.biPlanes = 1;
  bmi.bmiHeader.biBitCount = 32;
  bmi.bmiHeader.biCompression = BI_RGB;

  int imageSize = scaledWidth * scaledHeight * 4;
  std::vector<BYTE> pixels(imageSize);
  GetDIBits(hMemScaled, hBitmapScaled, 0, scaledHeight, pixels.data(), &bmi, DIB_RGB_COLORS);

  // Einstellungen für Helligkeit & Kontrast
  float brightness = -0.2f;  // -1.0 = sehr dunkel, 0.0 = neutral, +1.0 = heller
  float contrast = 1.5f;     // 1.0 = neutral, >1 = mehr Kontrast, <1 = flacher

  for (int i = 0; i < scaledWidth * scaledHeight; ++i) {
    BYTE* px = &pixels[i * 4];

    // RGB nach Graustufe (Helligkeit) – einfacher Luma-Algorithmus
    float gray = 0.299f * px[2] + 0.587f * px[1] + 0.114f * px[0];

    // Helligkeit & Kontrast anwenden
    gray = (gray / 255.0f - 0.5f);        // in [-0.5, +0.5]
    gray = gray * contrast + brightness; // anwenden
    gray = (gray + 0.5f) * 255.0f;        // zurückskalieren
    gray = std::clamp(gray, 0.0f, 255.0f);

    BYTE g = static_cast<BYTE>(gray);
    px[0] = g; // B
    px[1] = g; // G
    px[2] = g; // R
  }

  // Neues Bitmap mit bearbeiteten Pixeln erstellen
  HBITMAP hBitmapProcessed = CreateCompatibleBitmap(hScreen, scaledWidth, scaledHeight);
  HDC hMemProcessed = CreateCompatibleDC(hScreen);
  SelectObject(hMemProcessed, hBitmapProcessed);
  SetDIBits(hMemProcessed, hBitmapProcessed, 0, scaledHeight, pixels.data(), &bmi, DIB_RGB_COLORS);

  std::string filename = "screenshot_" + std::to_string(count) + ".bmp";
  SaveBitmapToFile(hBitmapProcessed, hMemProcessed, filename);
  std::cout << "Schwarz-Weiß Screenshot gespeichert: " << filename << "\n";

  // Aufräumen
  DeleteObject(hBitmapOriginal);
  DeleteObject(hBitmapScaled);
  DeleteObject(hBitmapProcessed);
  DeleteDC(hMemOriginal);
  DeleteDC(hMemScaled);
  DeleteDC(hMemProcessed);
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
