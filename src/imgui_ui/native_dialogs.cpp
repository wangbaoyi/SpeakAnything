#include "imgui_ui/native_dialogs.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <shobjidl.h>
#include <wrl/client.h>

#include <string>

namespace sa::imgui_ui {
namespace {

using Microsoft::WRL::ComPtr;

class Apartment {
public:
    Apartment() {
        result_ = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    }
    ~Apartment() {
        if (SUCCEEDED(result_)) CoUninitialize();
    }

private:
    HRESULT result_ = E_FAIL;
};

std::optional<std::filesystem::path> resultPath(IFileDialog* dialog) {
    ComPtr<IShellItem> item;
    if (FAILED(dialog->GetResult(&item))) return std::nullopt;
    PWSTR path = nullptr;
    if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) || path == nullptr) {
        return std::nullopt;
    }
    const std::filesystem::path result(path);
    CoTaskMemFree(path);
    return result;
}

void setStartFolder(IFileDialog* dialog, const std::filesystem::path& start) {
    if (start.empty()) return;
    ComPtr<IShellItem> folder;
    if (FAILED(SHCreateItemFromParsingName(start.c_str(), nullptr, IID_PPV_ARGS(&folder)))) {
        return;
    }
    dialog->SetFolder(folder.Get());
}

} // namespace

std::optional<std::filesystem::path> pickFolder(const wchar_t* title,
                                                const std::filesystem::path& start) {
    Apartment apartment;
    ComPtr<IFileOpenDialog> dialog;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&dialog)))) {
        return std::nullopt;
    }
    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    dialog->SetTitle(title);
    setStartFolder(dialog.Get(), start);
    if (FAILED(dialog->Show(nullptr))) return std::nullopt;
    return resultPath(dialog.Get());
}

std::optional<std::filesystem::path> pickModelFile(const wchar_t* title,
                                                   const std::filesystem::path& start,
                                                   bool whisper) {
    Apartment apartment;
    ComPtr<IFileOpenDialog> dialog;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&dialog)))) {
        return std::nullopt;
    }
    COMDLG_FILTERSPEC filters[] = {
        {L"Model files", whisper ? L"*.bin" : L"*.gguf;*.bin"},
        {L"All files", L"*.*"},
    };
    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST);
    dialog->SetFileTypes(2, filters);
    dialog->SetFileTypeIndex(1);
    dialog->SetTitle(title);
    setStartFolder(dialog.Get(), start);
    if (FAILED(dialog->Show(nullptr))) return std::nullopt;
    return resultPath(dialog.Get());
}

} // namespace sa::imgui_ui
