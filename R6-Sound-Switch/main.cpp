#include <windows.h>
#include <vector>
#include <string>
#include <fstream>
#include <iostream>
#include <algorithm> // for std::clamp
#include <thread>
#include <chrono>
#include <cstring> // für strstr

#include <tesseract/baseapi.h>
#include <leptonica/allheaders.h>

#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <audiopolicy.h>
#include <comdef.h>
#include <tlhelp32.h>

// Global persistent resources
static HDC screenDc = nullptr;
static HDC memDcOriginal = nullptr;
static HDC memDcScaled = nullptr;
static HDC memDcProcessed = nullptr;
static HBITMAP bitmapOriginal = nullptr;
static HBITMAP bitmapScaled = nullptr;
static HBITMAP bitmapProcessed = nullptr;
static std::vector<BYTE> pixelBuffer;
static BITMAPINFO bitmapInfo = {};

bool IsTargetProcess(DWORD pid, const std::wstring& targetName) {
  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snapshot == INVALID_HANDLE_VALUE) return false;

  PROCESSENTRY32 entry = { sizeof(PROCESSENTRY32) };
  if (Process32First(snapshot, &entry)) {
    do {
      if (entry.th32ProcessID == pid) {
        std::wstring name(entry.szExeFile);
        CloseHandle(snapshot);
        return _wcsicmp(name.c_str(), targetName.c_str()) == 0;
      }
    } while (Process32Next(snapshot, &entry));
  }

  CloseHandle(snapshot);
  return false;
}

void SetVolume(int volumePercent, const std::wstring& processName) {
  if (volumePercent < 0) volumePercent = 0;
  if (volumePercent > 100) volumePercent = 100;
  float volume = static_cast<float>(volumePercent) / 100.0f;

  CoInitialize(nullptr);

  IMMDeviceEnumerator* pEnumerator = nullptr;
  IMMDevice* pDevice = nullptr;
  IAudioSessionManager2* pSessionManager = nullptr;
  IAudioSessionEnumerator* pSessionEnumerator = nullptr;

  CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&pEnumerator));
  pEnumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &pDevice);
  pDevice->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr, (void**)&pSessionManager);
  pSessionManager->GetSessionEnumerator(&pSessionEnumerator);

  int count = 0;
  pSessionEnumerator->GetCount(&count);

  for (int i = 0; i < count; i++) {
    IAudioSessionControl* pSessionControl = nullptr;
    IAudioSessionControl2* pSessionControl2 = nullptr;

    pSessionEnumerator->GetSession(i, &pSessionControl);
    pSessionControl->QueryInterface(__uuidof(IAudioSessionControl2), (void**)&pSessionControl2);

    DWORD pid = 0;
    pSessionControl2->GetProcessId(&pid);

    if (IsTargetProcess(pid, processName)) {
      ISimpleAudioVolume* pVolume = nullptr;
      if (SUCCEEDED(pSessionControl->QueryInterface(__uuidof(ISimpleAudioVolume), (void**)&pVolume))) {
        pVolume->SetMasterVolume(volume, nullptr);
        std::wcout << L"Lautstärke von " << processName << L" auf " << volumePercent << L"% gesetzt.\n";
        pVolume->Release();
      }
    }

    pSessionControl2->Release();
    pSessionControl->Release();
  }

  pSessionEnumerator->Release();
  pSessionManager->Release();
  pDevice->Release();
  pEnumerator->Release();

  CoUninitialize();
}


enum COMMAND {
  PREPARATION,
  ACTION,
  LOST,
  WON,
  NONE
};


COMMAND ContainsAnyKeyword(const char* extractedText) {
  if (!extractedText) return COMMAND::NONE;

  const char* keywords[] = { "PREPARATION", "ACTION", "LOST", "WON" };
  
  if (std::strstr(extractedText, keywords[0]) != nullptr) {
    return COMMAND::PREPARATION;
  }

  if (std::strstr(extractedText, keywords[1]) != nullptr) {
    return COMMAND::ACTION;
  }

  if (std::strstr(extractedText, keywords[2]) != nullptr) {
    return COMMAND::LOST;
  }

  if (std::strstr(extractedText, keywords[3]) != nullptr) {
    return COMMAND::WON;
  }

  return COMMAND::NONE;
}

void PrepareString(char* extractedText) {
  if (!extractedText) return;

  int writePos = 0;
  for (int readPos = 0; extractedText[readPos] != '\0'; ++readPos) {
    char c = extractedText[readPos];
    if (c != '\n' && c != '\r' && c != ' ') {
      // Kleinbuchstaben (a-z) in Großbuchstaben (A-Z) umwandeln
      if (c >= 'a' && c <= 'z') {
        c = c - ('a' - 'A');
      }
      extractedText[writePos++] = c;
    }
  }
  extractedText[writePos] = '\0';
}

bool InitializeScreenshotBuffers(int width, int height) {
  screenDc = GetDC(nullptr);
  if (!screenDc) return false;

  memDcOriginal = CreateCompatibleDC(screenDc);
  memDcScaled = CreateCompatibleDC(screenDc);
  memDcProcessed = CreateCompatibleDC(screenDc);

  bitmapOriginal = CreateCompatibleBitmap(screenDc, width, height);
  if (!bitmapOriginal) return false;

  int scaledWidth = width / 2;
  int scaledHeight = height / 2;

  bitmapScaled = CreateCompatibleBitmap(screenDc, scaledWidth, scaledHeight);
  if (!bitmapScaled) return false;

  bitmapProcessed = CreateCompatibleBitmap(screenDc, scaledWidth, scaledHeight);
  if (!bitmapProcessed) return false;

  SelectObject(memDcOriginal, bitmapOriginal);
  SelectObject(memDcScaled, bitmapScaled);
  SelectObject(memDcProcessed, bitmapProcessed);

  bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bitmapInfo.bmiHeader.biWidth = scaledWidth;
  bitmapInfo.bmiHeader.biHeight = -scaledHeight;
  bitmapInfo.bmiHeader.biPlanes = 1;
  bitmapInfo.bmiHeader.biBitCount = 32;
  bitmapInfo.bmiHeader.biCompression = BI_RGB;

  pixelBuffer.resize(scaledWidth * scaledHeight * 4);

  return true;
}

void ReleaseScreenshotBuffers() {
  if (bitmapOriginal) DeleteObject(bitmapOriginal);
  if (bitmapScaled) DeleteObject(bitmapScaled);
  if (bitmapProcessed) DeleteObject(bitmapProcessed);
  if (memDcOriginal) DeleteDC(memDcOriginal);
  if (memDcScaled) DeleteDC(memDcScaled);
  if (memDcProcessed) DeleteDC(memDcProcessed);
  if (screenDc) ReleaseDC(nullptr, screenDc);
}

bool SaveBitmapToFile(HBITMAP hBitmap, HDC hdc, const std::string& filename) {
  BITMAP bitmap = {};
  if (!GetObject(hBitmap, sizeof(BITMAP), &bitmap)) return false;

  BITMAPINFOHEADER header = {};
  header.biSize = sizeof(BITMAPINFOHEADER);
  header.biWidth = bitmap.bmWidth;
  header.biHeight = bitmap.bmHeight;
  header.biPlanes = 1;
  header.biBitCount = 32;
  header.biCompression = BI_RGB;

  int dataSize = ((bitmap.bmWidth * 32 + 31) / 32) * 4 * bitmap.bmHeight;
  std::vector<BYTE> dataBuffer(dataSize);

  if (!GetDIBits(hdc, hBitmap, 0, bitmap.bmHeight, dataBuffer.data(), (BITMAPINFO*)&header, DIB_RGB_COLORS)) {
    return false;
  }

  BITMAPFILEHEADER fileHeader = {};
  fileHeader.bfType = 0x4D42;
  fileHeader.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
  fileHeader.bfSize = fileHeader.bfOffBits + dataSize;

  FILE* file = nullptr;
  if (fopen_s(&file, filename.c_str(), "wb") != 0 || !file) return false;
  if (!file) return false;

  fwrite(&fileHeader, sizeof(fileHeader), 1, file);
  fwrite(&header, sizeof(header), 1, file);
  fwrite(dataBuffer.data(), 1, dataSize, file);
  fclose(file);
  return true;
}

void CaptureAndProcessRegion(int x, int y, int width, int height, int index) {
  int scaledWidth = width / 2;
  int scaledHeight = height / 2;

  BitBlt(memDcOriginal, 0, 0, width, height, screenDc, x, y, SRCCOPY);

  SetStretchBltMode(memDcScaled, HALFTONE);
  StretchBlt(memDcScaled, 0, 0, scaledWidth, scaledHeight,
    memDcOriginal, 0, 0, width, height, SRCCOPY);

  if (!GetDIBits(memDcScaled, bitmapScaled, 0, scaledHeight, pixelBuffer.data(), &bitmapInfo, DIB_RGB_COLORS)) {
    std::cerr << "GetDIBits failed\n";
    return;
  }

  float brightness = -0.25f;
  float contrast = 0.75f;

  for (int i = 0; i < scaledWidth * scaledHeight; ++i) {
    BYTE* px = &pixelBuffer[i * 4];
    float gray = 0.299f * px[2] + 0.587f * px[1] + 0.114f * px[0];
    gray = (gray / 255.0f - 0.5f) * contrast + brightness;
    gray = (gray + 0.5f) * 255.0f;
    gray = std::clamp(gray, 0.0f, 255.0f);
    BYTE value = static_cast<BYTE>(gray);
    px[0] = px[1] = px[2] = value;
  }

  if (!SetDIBits(memDcProcessed, bitmapProcessed, 0, scaledHeight, pixelBuffer.data(), &bitmapInfo, DIB_RGB_COLORS)) {
    std::cerr << "SetDIBits failed\n";
    return;
  }

#ifdef _DEBUG
  std::string imageFilename = "screenshot_" + std::to_string(index) + ".bmp";
  SaveBitmapToFile(bitmapProcessed, memDcProcessed, imageFilename);
#endif

  tesseract::TessBaseAPI ocrApi;
  if (ocrApi.Init(nullptr, "eng")) {
    std::cerr << "Tesseract initialization failed\n";
    return;
  }

  Pix* image = pixCreate(scaledWidth, scaledHeight, 32);
  if (!image) {
    std::cerr << "pixCreate failed\n";
    ocrApi.End();
    return;
  }

  l_uint32* imageData = pixGetData(image);
  int wordsPerLine = pixGetWpl(image);
  for (int row = 0; row < scaledHeight; ++row) {
    l_uint32* line = imageData + row * wordsPerLine;
    for (int col = 0; col < scaledWidth; ++col) {
      int idx = (row * scaledWidth + col) * 4;
      BYTE blue = pixelBuffer[idx + 0];
      BYTE green = pixelBuffer[idx + 1];
      BYTE red = pixelBuffer[idx + 2];
      l_uint32 pixel = (255 << 24) | (red << 16) | (green << 8) | blue;
      line[col] = pixel;
    }
  }

  ocrApi.SetImage(image);
  char* extractedText = ocrApi.GetUTF8Text();
  PrepareString(extractedText);

#ifdef _DEBUG
  std::string textFilename = "screenshot_" + std::to_string(index) + ".txt";
  std::ofstream textFile(textFilename, std::ios::out | std::ios::binary);
  if (textFile.is_open()) {
    textFile.write(extractedText, strlen(extractedText));
    textFile.close();
    std::cout << "OCR output saved to: " << textFilename << "\n";
  }
  else {
    std::cerr << "Failed to write text file\n";
  }
#endif

  switch (ContainsAnyKeyword(extractedText)) {
  case COMMAND::PREPARATION:
    std::cout << "PREPARATION" << std::endl;

    SetVolume(75, L"RainbowSix.exe");
    SetVolume(75, L"RainbowSix_Vulkan.exe");
    SetVolume(75, L"RainbowSix_BE.exe");
    SetVolume(60, L"Discord.exe");

    break;
  case COMMAND::ACTION:
    std::cout << "ACTION" << std::endl;

    SetVolume(100, L"RainbowSix.exe");
    SetVolume(100, L"RainbowSix_Vulkan.exe");
    SetVolume(100, L"RainbowSix_BE.exe");
    SetVolume(50, L"Discord.exe");

    break;
  case COMMAND::WON:
    std::cout << "WON" << std::endl;

    SetVolume(50, L"RainbowSix.exe");
    SetVolume(50, L"RainbowSix_Vulkan.exe");
    SetVolume(50, L"RainbowSix_BE.exe");
    SetVolume(70, L"Discord.exe");

    break;
  case COMMAND::LOST:
    std::cout << "LOST" << std::endl;

    SetVolume(50, L"RainbowSix.exe");
    SetVolume(50, L"RainbowSix_Vulkan.exe");
    SetVolume(50, L"RainbowSix_BE.exe");
    SetVolume(70, L"Discord.exe");

    break;
  case COMMAND::NONE:
  default:
    break;
  }

  delete[] extractedText;
  pixDestroy(&image);
  ocrApi.End();
}

int main() {
  const int captureX = 1380;
  const int captureY = 550;
  const int captureWidth = 1080;
  const int captureHeight = 500;

  if (!InitializeScreenshotBuffers(captureWidth, captureHeight)) {
    std::cerr << "Failed to initialize buffers\n";
    return 1;
  }

  int index = 0;
  while (true) {
    CaptureAndProcessRegion(captureX, captureY, captureWidth, captureHeight, index++);
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }

  ReleaseScreenshotBuffers();
  return 0;
}
