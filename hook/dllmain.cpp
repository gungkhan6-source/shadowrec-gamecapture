// ═══════════════════════════════════════════════════════════════════════
// ShadowRec Game Capture Hook DLL v0.1
// İlk versiyon: Sadece "ben buradayım" mesajı verir
// İleride: Oyun process'ine inject olup DirectX Present()'i hook'layacak
// ═══════════════════════════════════════════════════════════════════════

#include <Windows.h>
#include <fstream>
#include <string>

// Log dosyası — DLL yüklendiğinde bir kanıt bırakır
// Çünkü oyun process'inden console'a yazamayız
void WriteLog(const std::string& message) {
    // ⭐ %TEMP% klasörüne yaz — her process buraya yazabilir
    // (C:\ köküne yazma yetkisi yoktur Windows 10/11'de)
    char tempPath[MAX_PATH];
    GetTempPathA(MAX_PATH, tempPath);
    std::string logPath = std::string(tempPath) + "ShadowRec_HookLog.txt";
    
    std::ofstream log(logPath, std::ios::app);
    if (log.is_open()) {
        // Timestamp ekle
        SYSTEMTIME st;
        GetLocalTime(&st);
        char timestamp[64];
        sprintf_s(timestamp, "[%02d:%02d:%02d.%03d] ",
            st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
        
        log << timestamp << message << std::endl;
        log.close();
    }
}

// ═══════════════════════════════════════════════════════════════════════
// DllMain — DLL yüklendiğinde, thread oluştuğunda, kaldırıldığında çağrılır
// ═══════════════════════════════════════════════════════════════════════
BOOL APIENTRY DllMain(HMODULE hModule, DWORD reasonForCall, LPVOID lpReserved)
{
    switch (reasonForCall)
    {
    case DLL_PROCESS_ATTACH:
        // ⭐ Bu DLL bir process'e yüklendi (inject edildiğinde tetiklenecek)
        {
            // Hangi process'e yüklendiğimizi öğren
            char processName[MAX_PATH] = { 0 };
            GetModuleFileNameA(NULL, processName, MAX_PATH);
            
            DWORD pid = GetCurrentProcessId();
            
            std::string msg = "ShadowRec Hook DLL yuklendi! PID=" + 
                              std::to_string(pid) + 
                              ", Process=" + processName;
            WriteLog(msg);
            
            // Daha sonra burada DirectX hooking yapacağız
            // Şimdilik sadece "buradayım" diyoruz
        }
        // Optimize: thread attach mesajlarını almaya gerek yok
        DisableThreadLibraryCalls(hModule);
        break;
        
    case DLL_PROCESS_DETACH:
        WriteLog("ShadowRec Hook DLL kaldirildi");
        break;
    }
    
    return TRUE;
}

// ═══════════════════════════════════════════════════════════════════════
// Export fonksiyonu — test amaçlı dışarıdan çağrılabilir
// ═══════════════════════════════════════════════════════════════════════
extern "C" __declspec(dllexport) int __stdcall HookTest()
{
    WriteLog("HookTest fonksiyonu cagrildi");
    return 42;
}
