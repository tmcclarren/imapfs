#include "crash_handler.h"

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include "stack_trace.h"

#if !defined(__USE_GNU)
#define __USE_GNU
#endif
#if !defined(_XOPEN_SOURCE)
#define _XOPEN_SOURCE
#endif
#include <ucontext.h>

extern const char* BUILD_VERSION;

using namespace std;

int CrashHandler::_fd = -1;
string CrashHandler::_statusFile;
string CrashHandler::_logPrefix;

static const char STACKDUMP_START[] = "[STACK DUMP START]\n";
static const char STACKDUMP_END[] = "[STACK DUMP END]\n";
static const char STACK_INDENT[] = "    ";
static const char CRASH_FILE[] = "crash.log";

// stacks will be 32 entries deep, at maximum, clipped at 512 characters per line
static StackTrace trace(512, 32);

// macro expansion for fixed-width string pointer and size
#define FWS(s) s, sizeof(s)-1

// only calls write
inline int CrashHandler::report_error(int err)
{
    static char endln[] = "\n";

    int error = 0;
    error = write(STDOUT_FILENO, "errno ", 6);
    char c;
    c = '0' + (err / 10);
    if (c != '0') {
        error = write(STDOUT_FILENO, &c, 1);
    }
    c = '0' + (err % 10);
    error = write(STDOUT_FILENO, &c, 1);
    error = write(STDOUT_FILENO, FWS(endln));
    fsync(STDOUT_FILENO);
    return error;
}

// TODO this won't work the same on 32-bit and 64-bit... needs to be
// cleaned up
// calls write, open, and fsync, which are all supposed to be safe
// it's not clearly spelled out, but backtrace_symbols_fd *should* be safe
void CrashHandler::handler(int signum, siginfo_t* /* info */, void* /* context */)
{
    // reset the default signal handler, so raise below
    // will dump a core and exit with proper status code
    signal(signum, SIG_DFL);

    // Remove dangling status file
    deleteStatusFile();

    int error = 0;
    if (_fd == -1) {
        error = open(CRASH_FILE, O_WRONLY | O_CREAT | O_APPEND, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH);
        if (error == -1) {
            error = report_error(errno);
            raise(signum);
        }
        _fd = error;
    }
    
    {   
        // print signal to log in case no core is produced
        struct timeval tval;
        gettimeofday(&tval, NULL);
        struct tm* lt = localtime(&tval.tv_sec);
        char buffer[32];
        // YYYYMMDD HH:MM:SS.mmm
        snprintf(buffer, sizeof(buffer), "%4d%02d%02d %02d:%02d:%02d%s%03d"
                 , lt->tm_year + 1900
                 , lt->tm_mon + 1
                 , lt->tm_mday
                 , lt->tm_hour
                 , lt->tm_min
                 , lt->tm_sec
                 , "."
                 , static_cast<int>(tval.tv_usec / 1000)
                 );

        error = write(_fd, _logPrefix.data(), _logPrefix.size());
        if ( _logPrefix.size() > 0 ) {
            error = write(_fd, FWS(" "));
        }
        error = write(_fd, buffer, strlen(buffer));
        error = write(_fd, FWS(" EMERG "));
        error = write(_fd, FWS(__FILE__));
        error = write(_fd, FWS(":"));

        snprintf(buffer, sizeof(buffer), "%d", __LINE__);
        error = write(_fd, buffer, strlen(buffer));

        error = write(_fd, FWS(" "));
        error = write(_fd, FWS(__FUNCTION__));
        error = write(_fd, FWS("() - handling signal: "));

        const char* sigdesc = strsignal(signum);
        error = write(_fd, sigdesc, strlen(sigdesc));

        error = write(_fd, FWS(". revision = "));
        error = write(_fd, BUILD_VERSION, strlen(BUILD_VERSION));

        error = write(_fd, "\n", 1);
    }

    error = write(_fd, FWS(STACKDUMP_START));
    if (error != -1) {
        size_t size = demangled_symbols(trace);
        for (size_t i = 0; i < size; ++i) {
            error = write(_fd, FWS(STACK_INDENT));
            error = write(_fd, trace._lines[i], strlen(trace._lines[i]));
            error = write(_fd, "\n", 1);
        }
        error = write(_fd, FWS(STACKDUMP_END));
        fsync(_fd);
    }
    raise(signum);
}

void CrashHandler::init()
{
    struct sigaction ignore;
    struct sigaction handle;

    ignore.sa_handler = SIG_IGN;
    sigemptyset(&ignore.sa_mask);
    ignore.sa_flags = 0x0;
  
    handle.sa_sigaction = CrashHandler::handler;
    sigemptyset(&handle.sa_mask);
    handle.sa_flags = SA_SIGINFO;     

    // we ignore SIGPIPE because we handle broken pipes
    sigaction(SIGPIPE, &ignore, NULL);

    // the rest we'll run the handler for
    sigaction(SIGILL, &handle, NULL);
    sigaction(SIGBUS, &handle, NULL);
    sigaction(SIGABRT, &handle, NULL);
    sigaction(SIGFPE, &handle, NULL);
    sigaction(SIGSEGV, &handle, NULL);
}

void CrashHandler::deleteStatusFile()
{
    if (!_statusFile.empty()) {
        remove(_statusFile.c_str());
    }
}

void CrashHandler::setFd(int fd)
{
    _fd = fd;
}

void CrashHandler::setStatusFile(const string& statusFile)
{
    _statusFile = statusFile;
}

void CrashHandler::setLogPrefix(const string& logPrefix)
{
    _logPrefix = logPrefix;
}
