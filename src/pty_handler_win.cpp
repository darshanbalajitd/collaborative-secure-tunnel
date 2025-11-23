#include "pty_handler.hpp"
#include "utils.hpp"

#include <windows.h>
#include <shellapi.h>
#include <string>

typedef void* HPCON;
static const DWORD PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE_CONST = 0x00020016; // Fallback if header lacks the define

PTYHandler::PTYHandler() {}

PTYHandler::~PTYHandler() {
    terminate_child();
    if (pseudo_console_) {
        ClosePseudoConsole((HPCON)pseudo_console_);
        pseudo_console_ = nullptr;
    }
    if (in_write_) { CloseHandle((HANDLE)in_write_); in_write_ = nullptr; }
    if (out_read_) { CloseHandle((HANDLE)out_read_); out_read_ = nullptr; }
}

bool PTYHandler::create_pty_and_fork_shell() {
    SECURITY_ATTRIBUTES sa{ sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE };
    HANDLE inRead = nullptr, inWrite = nullptr;
    HANDLE outRead = nullptr, outWrite = nullptr;
    if (!CreatePipe(&inRead, &inWrite, &sa, 0)) { LOG_ERROR("CreatePipe(in) failed: %lu", GetLastError()); return false; }
    if (!CreatePipe(&outRead, &outWrite, &sa, 0)) { LOG_ERROR("CreatePipe(out) failed: %lu", GetLastError()); CloseHandle(inRead); CloseHandle(inWrite); return false; }

    SetHandleInformation(inRead, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(outWrite, HANDLE_FLAG_INHERIT, 0);

    COORD size{ 80, 24 };
    HPCON hPC = nullptr;
    if (CreatePseudoConsole(size, inRead, outWrite, 0, &hPC) != S_OK) {
        LOG_ERROR("CreatePseudoConsole failed: %lu", GetLastError());
        CloseHandle(inRead); CloseHandle(inWrite);
        CloseHandle(outRead); CloseHandle(outWrite);
        return false;
    }

    pseudo_console_ = hPC;
    in_write_ = inWrite;
    out_read_ = outRead;

    SIZE_T attrListSize = 0;
    STARTUPINFOEXA si{}; si.StartupInfo.cb = sizeof(STARTUPINFOEXA);
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attrListSize);
    si.lpAttributeList = (PPROC_THREAD_ATTRIBUTE_LIST)HeapAlloc(GetProcessHeap(), 0, attrListSize);
    if (!si.lpAttributeList) { LOG_ERROR("HeapAlloc attr list failed"); return false; }
    if (!InitializeProcThreadAttributeList(si.lpAttributeList, 1, 0, &attrListSize)) { LOG_ERROR("InitializeProcThreadAttributeList failed: %lu", GetLastError()); return false; }
    if (!UpdateProcThreadAttribute(si.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE_CONST, hPC, sizeof(HPCON), nullptr, nullptr)) { LOG_ERROR("UpdateProcThreadAttribute failed: %lu", GetLastError()); return false; }

    char* userProfile = nullptr;
    DWORD needed = GetEnvironmentVariableA("USERPROFILE", nullptr, 0);
    std::string currentDir;
    if (needed > 0) {
        currentDir.resize(needed);
        GetEnvironmentVariableA("USERPROFILE", currentDir.data(), needed);
        if (!currentDir.empty() && currentDir.back() == '\\') currentDir.pop_back();
    }

    PROCESS_INFORMATION pi{};
    const char* cmd = "cmd.exe";
    BOOL ok = CreateProcessA(
        nullptr,
        (LPSTR)cmd,
        nullptr,
        nullptr,
        FALSE,
        EXTENDED_STARTUPINFO_PRESENT,
        nullptr,
        currentDir.empty() ? nullptr : currentDir.c_str(),
        &si.StartupInfo,
        &pi
    );
    if (!ok) { LOG_ERROR("CreateProcessA(cmd.exe) failed: %lu", GetLastError()); return false; }

    child_pid_ = (unsigned long)pi.dwProcessId;
    LOG_INFO("ConPTY child started, pid=%lu", child_pid_);
    // The child handles will be closed on terminate
    return true;
}

int PTYHandler::get_master_fd() const { return (int)(intptr_t)out_read_; }
int PTYHandler::get_child_pid() const { return (int)child_pid_; }

long PTYHandler::pty_read_nonblocking(char* buf, size_t buf_size) {
    if (!out_read_) return -1;
    DWORD available = 0;
    if (!PeekNamedPipe((HANDLE)out_read_, nullptr, 0, nullptr, &available, nullptr)) return -1;
    if (available == 0) return 0;
    DWORD readn = 0;
    DWORD toRead = available < (DWORD)buf_size ? available : (DWORD)buf_size;
    if (!ReadFile((HANDLE)out_read_, buf, toRead, &readn, NULL)) return -1;
    return (long)readn;
}

long PTYHandler::pty_write(const char* buf, size_t len) {
    if (!in_write_) return -1;
    DWORD written = 0;
    if (!WriteFile((HANDLE)in_write_, buf, (DWORD)len, &written, nullptr)) return -1;
    return (long)written;
}

void PTYHandler::apply_window_size(int rows, int cols) {
    if (!pseudo_console_) return;
    COORD size{ (SHORT)cols, (SHORT)rows };
    ResizePseudoConsole((HPCON)pseudo_console_, size);
}

void PTYHandler::wait_for_child() {
    // No-op: handled by the console
}

void PTYHandler::terminate_child() {
    // Best-effort cleanup
    if (pseudo_console_) {
        ClosePseudoConsole((HPCON)pseudo_console_);
        pseudo_console_ = nullptr;
    }
}

void PTYHandler::configure_child_terminal() {}
void PTYHandler::execute_shell() {}
