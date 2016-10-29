#pragma once

#include <stdio.h>
#include <signal.h>
#include <string>

class CrashHandler {
public:
    static CrashHandler& instance() {
        static CrashHandler _instance;
        return _instance;
    }

    static void init();
    static void setFd(int fd);
    static void setStatusFile(const std::string& name);
    static void setLogPrefix(const std::string& name);

private:
    static int report_error(int err);
    static void handler(int signum, siginfo_t* info, void* context);
    static void deleteStatusFile();

    static int _fd;
    static std::string _statusFile;
    static std::string _logPrefix;

    CrashHandler() { this->init(); }
    CrashHandler(const CrashHandler&) { }
    virtual ~CrashHandler() { }
};

