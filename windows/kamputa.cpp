// kamputa.cpp – аналог sudo для Windows
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <shlwapi.h>
#include <stdio.h>
#include <tchar.h>
#include <vector>
#include <string>

#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")

// ---------- вспомогательные функции ----------
std::wstring GetCurrentUser() {
    wchar_t username[UNLEN + 1];
    DWORD size = UNLEN + 1;
    if (GetUserNameW(username, &size))
        return std::wstring(username);
    return L"";
}

std::wstring GetCurrentUserSid() {
    HANDLE hToken = NULL;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken))
        return L"";
    DWORD size = 0;
    GetTokenInformation(hToken, TokenUser, NULL, 0, &size);
    if (size == 0) { CloseHandle(hToken); return L""; }
    std::vector<BYTE> buf(size);
    if (!GetTokenInformation(hToken, TokenUser, buf.data(), size, &size)) {
        CloseHandle(hToken);
        return L"";
    }
    CloseHandle(hToken);
    PTOKEN_USER pUser = (PTOKEN_USER)buf.data();
    LPWSTR sidStr = NULL;
    if (!ConvertSidToStringSidW(pUser->User.Sid, &sidStr))
        return L"";
    std::wstring result(sidStr);
    LocalFree(sidStr);
    return result;
}

bool IsUserInGroup(const std::wstring& groupName) {
    PSID groupSid = NULL;
    SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;
    DWORD sidSize = SECURITY_MAX_SID_SIZE;
    wchar_t domain[256];
    DWORD domainSize = 256;
    SID_NAME_USE sidType;
    if (!LookupAccountNameW(NULL, groupName.c_str(), NULL, &sidSize, domain, &domainSize, &sidType)) {
        if (groupName == L"Administrators") {
            AllocateAndInitializeSid(&ntAuthority, 2,
                SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS,
                0,0,0,0,0,0, &groupSid);
        }
        else return false;
    }
    else {
        groupSid = (PSID)malloc(sidSize);
        if (!LookupAccountNameW(NULL, groupName.c_str(), groupSid, &sidSize, domain, &domainSize, &sidType)) {
            free(groupSid);
            return false;
        }
    }
    if (!groupSid) return false;
    BOOL isMember = FALSE;
    if (!CheckTokenMembership(NULL, groupSid, &isMember)) {
        isMember = FALSE;
    }
    FreeSid(groupSid);
    return isMember != FALSE;
}

bool IsAuthorized(const std::wstring& configPath) {
    if (!PathFileExistsW(configPath.c_str()))
        return false;
    FILE* f = _wfopen(configPath.c_str(), L"r, ccs=UTF-8");
    if (!f) return false;
    char line[1024];
    std::wstring currentUser = GetCurrentUser();
    std::wstring currentSid = GetCurrentUserSid();
    bool authorized = false;
    while (fgets(line, sizeof(line), f)) {
        size_t len = strlen(line);
        if (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r'))
            line[--len] = '\0';
        if (len > 0 && line[len-1] == '\r')
            line[--len] = '\0';
        if (len == 0) continue;
        if (line[0] == '#') continue;
        std::wstring entry;
        for (char* p = line; *p; ++p)
            entry += (wchar_t)(unsigned char)*p;
        if (entry[0] == L'@') {
            std::wstring group = entry.substr(1);
            if (IsUserInGroup(group)) {
                authorized = true;
                break;
            }
        } else {
            if (entry == currentUser || entry == currentSid) {
                authorized = true;
                break;
            }
        }
    }
    fclose(f);
    return authorized;
}

bool RunAsAdmin(const std::wstring& cmdLine) {
    std::wstring app, params;
    size_t pos = cmdLine.find_first_of(L" \t");
    if (pos != std::wstring::npos) {
        app = cmdLine.substr(0, pos);
        params = cmdLine.substr(pos + 1);
        while (!params.empty() && params[0] == L' ') params.erase(0,1);
    } else {
        app = cmdLine;
        params = L"";
    }
    HINSTANCE h = ShellExecuteW(NULL, L"runas", app.c_str(), params.c_str(), NULL, SW_SHOW);
    if ((INT_PTR)h > 32)
        return true;
    else {
        DWORD err = GetLastError();
        if (err == ERROR_CANCELLED)
            wprintf(L"User canceled UAC prompt.\n");
        else
            wprintf(L"Failed to launch: error %lu\n", err);
        return false;
    }
}

int wmain(int argc, wchar_t* argv[]) {
    if (argc < 2) {
        wprintf(L"Usage: kamputa.exe <command> [args...]\n");
        wprintf(L"Example: kamputa.exe cmd.exe /c dir\n");
        return 1;
    }
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    PathRemoveFileSpecW(exePath);
    wchar_t configPath[MAX_PATH];
    wcscpy(configPath, exePath);
    PathAppendW(configPath, L"kamputa.conf");
    if (!IsAuthorized(configPath)) {
        wprintf(L"Access denied: user is not allowed to run elevated commands.\n");
        return 2;
    }
    std::wstring cmdLine;
    for (int i = 1; i < argc; ++i) {
        if (i > 1) cmdLine += L' ';
        if (wcschr(argv[i], L' ') != NULL) {
            cmdLine += L'\"';
            cmdLine += argv[i];
            cmdLine += L'\"';
        } else {
            cmdLine += argv[i];
        }
    }
    bool ok = RunAsAdmin(cmdLine);
    return ok ? 0 : 3;
}
