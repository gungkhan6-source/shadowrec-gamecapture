// ═══════════════════════════════════════════════════════════════════════
// ShadowRec Injector v0.1
// Hedef process'e DLL inject eder (CreateRemoteThread + LoadLibrary)
// ═══════════════════════════════════════════════════════════════════════

#include <Windows.h>
#include <TlHelp32.h>
#include <iostream>
#include <string>

// ── Process ismine göre PID bul ───────────────────────────────────
DWORD FindProcessId(const std::wstring& processName) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return 0;
    
    PROCESSENTRY32W entry;
    entry.dwSize = sizeof(entry);
    
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (processName == entry.szExeFile) {
                CloseHandle(snapshot);
                return entry.th32ProcessID;
            }
        } while (Process32NextW(snapshot, &entry));
    }
    
    CloseHandle(snapshot);
    return 0;
}

// ── DLL inject ────────────────────────────────────────────────────
bool InjectDll(DWORD pid, const std::string& dllPath) {
    // 1. Process'i aç (tüm yetkilerle)
    HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (!hProcess) {
        std::cerr << "OpenProcess basarisiz, Error=" << GetLastError() << "\n";
        return false;
    }
    
    // 2. Hedef process'in belleğinde DLL yolu için yer ayır
    SIZE_T pathSize = dllPath.size() + 1;
    LPVOID remotePath = VirtualAllocEx(hProcess, NULL, pathSize, 
                                        MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remotePath) {
        std::cerr << "VirtualAllocEx basarisiz\n";
        CloseHandle(hProcess);
        return false;
    }
    
    // 3. DLL yolunu hedef belleğe yaz
    if (!WriteProcessMemory(hProcess, remotePath, dllPath.c_str(), pathSize, NULL)) {
        std::cerr << "WriteProcessMemory basarisiz\n";
        VirtualFreeEx(hProcess, remotePath, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }
    
    // 4. LoadLibraryA fonksiyon adresini al (kernel32.dll'den)
    // Bu adres tüm process'lerde aynı (ASLR aynı sayfa)
    HMODULE hKernel32 = GetModuleHandleW(L"kernel32.dll");
    LPVOID loadLibraryAddr = (LPVOID)GetProcAddress(hKernel32, "LoadLibraryA");
    
    // 5. Hedef process'te yeni thread oluştur — LoadLibraryA(dllPath) çağırır
    HANDLE hThread = CreateRemoteThread(hProcess, NULL, 0,
        (LPTHREAD_START_ROUTINE)loadLibraryAddr, remotePath, 0, NULL);
    
    if (!hThread) {
        std::cerr << "CreateRemoteThread basarisiz, Error=" << GetLastError() << "\n";
        VirtualFreeEx(hProcess, remotePath, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }
    
    std::cout << "✅ Thread olusturuldu, DLL yukleniyor...\n";
    
    // 6. Thread bitsin (DLL yüklenmiş demektir)
    WaitForSingleObject(hThread, 5000);
    
    // 7. Temizlik
    VirtualFreeEx(hProcess, remotePath, 0, MEM_RELEASE);
    CloseHandle(hThread);
    CloseHandle(hProcess);
    
    return true;
}

int main(int argc, char* argv[]) {
    std::cout << "═══════════════════════════════════════\n";
    std::cout << "  ShadowRec Injector v0.1\n";
    std::cout << "═══════════════════════════════════════\n\n";
    
    if (argc < 3) {
        std::cout << "Kullanim: injector.exe <process_adi.exe> <dll_yolu>\n";
        std::cout << "Ornek: injector.exe notepad.exe F:\\game-capture\\hook\\build\\Release\\shadowrec_hook.dll\n";
        return 1;
    }
    
    // Argümanları çevir
    std::string targetExe = argv[1];
    std::string dllPath = argv[2];
    
    // Process adını wide string'e çevir
    std::wstring wTargetExe(targetExe.begin(), targetExe.end());
    
    std::cout << "Hedef process: " << targetExe << "\n";
    std::cout << "DLL yolu: " << dllPath << "\n\n";
    
    // PID bul
    DWORD pid = FindProcessId(wTargetExe);
    if (!pid) {
        std::cerr << "❌ Process bulunamadi: " << targetExe << "\n";
        std::cerr << "   Once o uygulamayi acin!\n";
        return 1;
    }
    std::cout << "✅ Process bulundu, PID=" << pid << "\n\n";
    
    // Inject et
    if (InjectDll(pid, dllPath)) {
        std::cout << "\n✅ Injection tamamlandi!\n";
        std::cout << "   Kontrol: C:\\ShadowRec_HookLog.txt dosyasini ac\n";
        return 0;
    } else {
        std::cerr << "\n❌ Injection basarisiz\n";
        return 1;
    }
}