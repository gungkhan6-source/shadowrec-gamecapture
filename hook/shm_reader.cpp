// ═══════════════════════════════════════════════════════════════════════
// ShadowRec SHM Reader — Shared memory'den frame okur
// Test amacli: 10 frame oku, BMP olarak kaydet
// ═══════════════════════════════════════════════════════════════════════

#include <Windows.h>
#include <iostream>
#include <fstream>
#include <string>

#define SHM_NAME L"ShadowRecFrameBuffer"
#define EVENT_NAME L"ShadowRecFrameEvent"
#define MAX_WIDTH 3840
#define MAX_HEIGHT 2160
#define MAX_FRAME_SIZE (MAX_WIDTH * MAX_HEIGHT * 4)

struct SharedFrameHeader {
    UINT32 width;
    UINT32 height;
    UINT32 frameNumber;
    UINT32 pixelFormat;
    UINT64 timestamp;
    UINT32 dataSize;
    UINT32 reserved;
};

#define SHM_SIZE (sizeof(SharedFrameHeader) + MAX_FRAME_SIZE)

void SaveBMP(const std::string& filename, int width, int height, const void* pixels) {
    int rowSize = width * 4;
    int dataSize = rowSize * height;
    int fileSize = 54 + dataSize;
    
    unsigned char header[54] = {
        'B','M', 0,0,0,0, 0,0, 0,0, 54,0,0,0,
        40,0,0,0, 0,0,0,0, 0,0,0,0, 1,0, 32,0,
        0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0
    };
    
    *(int*)&header[2] = fileSize;
    *(int*)&header[18] = width;
    *(int*)&header[22] = -height;
    *(int*)&header[34] = dataSize;
    
    std::ofstream f(filename, std::ios::binary);
    f.write((char*)header, 54);
    f.write((char*)pixels, dataSize);
    f.close();
}

int main() {
    std::cout << "ShadowRec SHM Reader v0.1\n";
    std::cout << "Shared memory'e baglaniliyor...\n\n";
    
    // Shared memory'i ac (DLL zaten olusturdu)
    HANDLE hMap = OpenFileMappingW(FILE_MAP_READ, FALSE, SHM_NAME);
    if (!hMap) {
        // DLL henuz baslamamis olabilir, olusturalim
        hMap = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE,
                                   0, SHM_SIZE, SHM_NAME);
        if (!hMap) {
            std::cerr << "Shared memory acilamadi. Error=" << GetLastError() << "\n";
            std::cerr << "Once test_app'i acin ve DLL'i inject edin.\n";
            return 1;
        }
        std::cout << "Shared memory olusturuldu (DLL bekleniyor)\n";
    } else {
        std::cout << "Shared memory'e baglandi (DLL aktif)\n";
    }
    
    void* mapped = MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, SHM_SIZE);
    if (!mapped) {
        // Tekrar dene full access
        CloseHandle(hMap);
        hMap = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, SHM_NAME);
        mapped = MapViewOfFile(hMap, FILE_MAP_ALL_ACCESS, 0, 0, SHM_SIZE);
    }
    
    if (!mapped) {
        std::cerr << "MapViewOfFile basarisiz\n";
        return 1;
    }
    
    SharedFrameHeader* hdr = (SharedFrameHeader*)mapped;
    UINT8* pixels = (UINT8*)mapped + sizeof(SharedFrameHeader);
    
    // Event'i ac
    HANDLE hEvent = OpenEventW(SYNCHRONIZE, FALSE, EVENT_NAME);
    if (!hEvent) {
        std::cout << "Event acilamadi, polling moduna geciliyor\n";
    }
    
    char tempPath[MAX_PATH];
    GetTempPathA(MAX_PATH, tempPath);
    
    std::cout << "10 frame yakalamaya hazir, bekleniyor...\n\n";
    
    UINT32 lastFrame = 0;
    int captured = 0;
    DWORD startTick = GetTickCount();
    
    while (captured < 10) {
        // Event ile bekle (max 5 saniye)
        if (hEvent) {
            WaitForSingleObject(hEvent, 5000);
        } else {
            Sleep(16);  // ~60 FPS polling
        }
        
        // Timeout check
        if (GetTickCount() - startTick > 30000) {
            std::cout << "Timeout: 30 saniye boyunca frame gelmedi\n";
            break;
        }
        
        UINT32 currentFrame = hdr->frameNumber;
        if (currentFrame != lastFrame && hdr->width > 0 && hdr->height > 0) {
            lastFrame = currentFrame;
            
            // Sadece 30 frame'de bir kaydet (10 BMP olusturmak icin)
            if (currentFrame % 30 != 0) continue;
            
            captured++;
            
            char bmpPath[MAX_PATH];
            sprintf_s(bmpPath, "%sShadowRec_SHM_Frame%03d.bmp", tempPath, captured);
            
            SaveBMP(bmpPath, hdr->width, hdr->height, pixels);
            
            std::cout << "Frame " << captured << "/10 alindi: "
                      << hdr->width << "x" << hdr->height
                      << " (game frame#=" << currentFrame << ")"
                      << " -> " << bmpPath << "\n";
        }
    }
    
    std::cout << "\n" << captured << " frame yakalandi\n";
    std::cout << "BMP'leri kontrol et: " << tempPath << "ShadowRec_SHM_Frame*.bmp\n";
    
    UnmapViewOfFile(mapped);
    CloseHandle(hMap);
    if (hEvent) CloseHandle(hEvent);
    
    return 0;
}
