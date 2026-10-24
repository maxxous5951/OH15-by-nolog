#include "gui_process.hpp"
#include <cstdlib>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#endif

namespace psgui {

Process::Process() = default;

namespace {
// Ajoute une ligne (coupée sur \n ou \r) ; garde au plus 5000 lignes en attente.
void pushChunk(std::string& partial, const char* data, size_t n, std::deque<std::string>& out, std::mutex& mu) {
    std::lock_guard<std::mutex> lk(mu);
    for (size_t i = 0; i < n; ++i) {
        const char c = data[i];
        if (c == '\n' || c == '\r') {
            if (!partial.empty()) { out.push_back(std::move(partial)); partial.clear(); }
            if (out.size() > 5000) out.pop_front();
        } else partial.push_back(c);
    }
}
}  // namespace

#ifdef _WIN32
// ============================================================ Windows
struct Process::Impl {
    HANDLE process = nullptr, job = nullptr, pipeRead = nullptr;
};

namespace {
std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}
// Quotage d'un argument selon les règles de CommandLineToArgvW
std::wstring quoteArg(const std::wstring& a) {
    if (!a.empty() && a.find_first_of(L" \t\n\v\"") == std::wstring::npos) return a;
    std::wstring r = L"\"";
    for (size_t i = 0;; ++i) {
        size_t bs = 0;
        while (i < a.size() && a[i] == L'\\') { ++bs; ++i; }
        if (i == a.size()) { r.append(bs * 2, L'\\'); break; }
        if (a[i] == L'"') { r.append(bs * 2 + 1, L'\\'); r.push_back(L'"'); }
        else { r.append(bs, L'\\'); r.push_back(a[i]); }
    }
    r.push_back(L'"');
    return r;
}
}  // namespace

bool Process::start(const std::vector<std::string>& argv, const std::string& cwd, std::string& error) {
    if (started_ || argv.empty()) { error = "processus deja lance"; return false; }
    SetEnvironmentVariableW(L"PYTHONUNBUFFERED", L"1");
    SetEnvironmentVariableW(L"PYTHONUTF8", L"1");
    SetEnvironmentVariableW(L"PYTHONIOENCODING", L"utf-8");

    SECURITY_ATTRIBUTES sa = {};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    HANDLE rd = nullptr, wr = nullptr;
    if (!CreatePipe(&rd, &wr, &sa, 0)) { error = "CreatePipe a echoue"; return false; }
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    HANDLE nul = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);

    std::wstring cmd;
    for (size_t i = 0; i < argv.size(); ++i) { if (i) cmd += L' '; cmd += quoteArg(widen(argv[i])); }
    std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
    cmdBuf.push_back(L'\0');
    const std::wstring wcwd = widen(cwd);

    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = nul != INVALID_HANDLE_VALUE ? nul : nullptr;
    si.hStdOutput = wr;
    si.hStdError = wr;
    PROCESS_INFORMATION pi = {};
    const BOOL ok = CreateProcessW(nullptr, cmdBuf.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr,
                                   wcwd.empty() ? nullptr : wcwd.c_str(), &si, &pi);
    CloseHandle(wr);
    if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
    if (!ok) {
        const DWORD e = GetLastError();
        CloseHandle(rd);
        error = "impossible de lancer '" + argv[0] + "' (erreur Windows " + std::to_string(e) + ") - Python est-il installe et dans le PATH ?";
        return false;
    }
    impl_ = std::make_unique<Impl>();
    impl_->process = pi.hProcess;
    impl_->pipeRead = rd;
    // Objet Job : fermer/terminer le job arrete aussi les processus enfants (pokersim lances par optimize.py)
    impl_->job = CreateJobObjectW(nullptr, nullptr);
    if (impl_->job) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION info = {};
        info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(impl_->job, JobObjectExtendedLimitInformation, &info, sizeof(info));
        AssignProcessToJobObject(impl_->job, pi.hProcess);
    }
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    started_ = true;
    reader_ = std::thread([this] { readerLoop(); });
    return true;
}

void Process::readerLoop() {
    std::string partial;
    char buf[4096];
    DWORD n = 0;
    while (ReadFile(impl_->pipeRead, buf, sizeof(buf), &n, nullptr) && n > 0) pushChunk(partial, buf, n, lines_, mu_);
    if (!partial.empty()) { std::lock_guard<std::mutex> lk(mu_); lines_.push_back(partial); }
    WaitForSingleObject(impl_->process, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(impl_->process, &code);
    exitCode_ = static_cast<int>(code);
    finished_ = true;
}

void Process::terminate() {
    if (!started_ || finished_.load() || !impl_) return;
    if (impl_->job) TerminateJobObject(impl_->job, 1);
    else if (impl_->process) TerminateProcess(impl_->process, 1);
}

void Process::join() {
    if (reader_.joinable()) reader_.join();
}

Process::~Process() {
    if (started_ && !finished_.load()) terminate();
    join();
    if (impl_) {
        if (impl_->pipeRead) CloseHandle(impl_->pipeRead);
        if (impl_->process) CloseHandle(impl_->process);
        if (impl_->job) CloseHandle(impl_->job);
    }
}

#else
// ============================================================ POSIX
struct Process::Impl {
    pid_t pid = -1;
    int fd = -1;
};

bool Process::start(const std::vector<std::string>& argv, const std::string& cwd, std::string& error) {
    if (started_ || argv.empty()) { error = "processus deja lance"; return false; }
    setenv("PYTHONUNBUFFERED", "1", 1);
    setenv("PYTHONUTF8", "1", 1);
    setenv("PYTHONIOENCODING", "utf-8", 1);
    int pfd[2];
    if (pipe(pfd) != 0) { error = std::string("pipe a echoue : ") + std::strerror(errno); return false; }
    int errPipe[2];                                   // pour savoir si exec a echoue
    if (pipe(errPipe) != 0) { close(pfd[0]); close(pfd[1]); error = "pipe a echoue"; return false; }
    fcntl(errPipe[1], F_SETFD, FD_CLOEXEC);
    std::vector<char*> args;
    for (const auto& a : argv) args.push_back(const_cast<char*>(a.c_str()));
    args.push_back(nullptr);
    const char* cwdC = cwd.empty() ? nullptr : cwd.c_str();
    const pid_t pid = fork();
    if (pid < 0) { close(pfd[0]); close(pfd[1]); close(errPipe[0]); close(errPipe[1]); error = "fork a echoue"; return false; }
    if (pid == 0) {                                   // enfant : uniquement des appels sûrs avant exec
        setsid();                                     // nouveau groupe : killpg arrête aussi les petits-enfants
        dup2(pfd[1], 1);
        dup2(pfd[1], 2);
        const int devnull = open("/dev/null", O_RDONLY);
        if (devnull >= 0) dup2(devnull, 0);
        close(pfd[0]); close(pfd[1]); close(errPipe[0]);
        if (cwdC && chdir(cwdC) != 0) { const int e = errno; if (write(errPipe[1], &e, sizeof e) < 0) {} _exit(127); }
        execvp(args[0], args.data());
        const int e = errno;
        if (write(errPipe[1], &e, sizeof e) < 0) {}
        _exit(127);
    }
    close(pfd[1]);
    close(errPipe[1]);
    int childErr = 0;
    const ssize_t got = read(errPipe[0], &childErr, sizeof childErr);
    close(errPipe[0]);
    if (got > 0) {
        close(pfd[0]);
        waitpid(pid, nullptr, 0);
        error = "impossible de lancer '" + argv[0] + "' : " + std::strerror(childErr) + " - Python est-il installe et dans le PATH ?";
        return false;
    }
    impl_ = std::make_unique<Impl>();
    impl_->pid = pid;
    impl_->fd = pfd[0];
    started_ = true;
    reader_ = std::thread([this] { readerLoop(); });
    return true;
}

void Process::readerLoop() {
    std::string partial;
    char buf[4096];
    for (;;) {
        const ssize_t n = read(impl_->fd, buf, sizeof buf);
        if (n > 0) pushChunk(partial, buf, static_cast<size_t>(n), lines_, mu_);
        else if (n == 0 || errno != EINTR) break;
    }
    if (!partial.empty()) { std::lock_guard<std::mutex> lk(mu_); lines_.push_back(partial); }
    int status = 0;
    waitpid(impl_->pid, &status, 0);
    exitCode_ = WIFEXITED(status) ? WEXITSTATUS(status) : (WIFSIGNALED(status) ? 128 + WTERMSIG(status) : -1);
    finished_ = true;
}

void Process::terminate() {
    if (!started_ || finished_.load() || !impl_ || impl_->pid <= 0) return;
    killpg(impl_->pid, SIGTERM);   // Python et les pokersim lances par optimize.py sont dans le meme groupe
}

void Process::join() {
    if (reader_.joinable()) reader_.join();
}

Process::~Process() {
    if (started_ && !finished_.load() && impl_ && impl_->pid > 0) killpg(impl_->pid, SIGKILL);
    join();
    if (impl_ && impl_->fd >= 0) close(impl_->fd);
}
#endif

std::vector<std::string> Process::drainLines() {
    std::lock_guard<std::mutex> lk(mu_);
    std::vector<std::string> out(lines_.begin(), lines_.end());
    lines_.clear();
    return out;
}

}  // namespace psgui
