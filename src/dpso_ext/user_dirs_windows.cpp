
#include "user_dirs.h"

#include <string>

#include <shlobj.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "dpso_utils/error_set.h"
#include "dpso_utils/windows/error.h"
#include "dpso_utils/windows/utf.h"


const char* dpsoGetUserDir(DpsoUserDir userDir, const char* appName)
{
    // FOLDERID_RoamingAppData is actually better path for config, but
    // we keep using FOLDERID_LocalAppData for backward compatibility.
    (void)userDir;

    wchar_t* appDataPathUtf16{};
    const auto hresult = SHGetKnownFolderPath(
        FOLDERID_LocalAppData,
        KF_FLAG_CREATE,
        nullptr,
        &appDataPathUtf16);
    if (FAILED(hresult)) {
        dpso::setError(
            "SHGetKnownFolderPath(FOLDERID_LocalAppData, ...): {}",
            dpso::windows::getHresultMessage(hresult));
        // Docs say that we should call CoTaskMemFree() even if
        // SHGetKnownFolderPath() fails.
        CoTaskMemFree(appDataPathUtf16);
        return nullptr;
    }

    std::wstring pathUtf16 = appDataPathUtf16;
    CoTaskMemFree(appDataPathUtf16);

    pathUtf16 += L'\\';
    try {
        pathUtf16 += dpso::windows::utf8ToUtf16(appName);
    } catch (std::runtime_error& e) {
        dpso::setError(
            "Can't convert appName to UTF-16: {}", e.what());
        return nullptr;
    }

    static std::string path;
    try {
        path = dpso::windows::utf16ToUtf8(pathUtf16.c_str());
    } catch (std::runtime_error& e) {
        dpso::setError("Can't convert path to UTF-8: {}", e.what());
        return nullptr;
    }

    if (!CreateDirectoryW(pathUtf16.c_str(), nullptr)
            && GetLastError() != ERROR_ALREADY_EXISTS) {
        dpso::setError(
            "CreateDirectoryW(\"{}\"): {}",
            path, dpso::windows::getErrorMessage(GetLastError()));
        return nullptr;
    }

    return path.c_str();
}
