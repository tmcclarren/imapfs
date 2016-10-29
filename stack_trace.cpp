#include "stack_trace.h"
#include <string.h>
#include <execinfo.h>
#include <cxxabi.h>
#include <iostream>
#include <sstream>
#include <fstream>

size_t demangled_symbols(StackTrace& trace, size_t ignoreFrames)
{
#ifdef __linux__
    static const char* SYMBOL_SSCANF = "%*[^(]%*[^_]%511[^)+]";
#elif __APPLE__
    static const char* SYMBOL_SSCANF ="%*59c%511s";
#endif

    size_t size = 1;
    const size_t MAX_SYMBOL_LENGTH = trace._maxLineLength; 
    // add an ignore frame for crash handling frames
    ignoreFrames += 2;
    void* buffer[trace._depth];
    const size_t bufferSize = backtrace(buffer, trace._depth);

    if (bufferSize == 0) {
        strncpy(trace._lines[0], "empty backtrace", MAX_SYMBOL_LENGTH);
        return size;
    }

    if (ignoreFrames >= bufferSize) {
        strncpy(trace._lines[0], "ignored entire backtrace", MAX_SYMBOL_LENGTH);
        return size;
    }

    char** symbols = backtrace_symbols(buffer, bufferSize);
    if (!symbols) {
        strncpy(trace._lines[0], "NULL backtrace", MAX_SYMBOL_LENGTH);
        return size;
    }
    for (size_t i = 0; i < ignoreFrames; ++i, ++symbols);

    // now set size to the correct value
    size = bufferSize - ignoreFrames;

    for (size_t iter = 0; iter < size; ++iter, ++symbols) {
        char* symbol = *symbols;
        if (!symbol) {
            strncpy(trace._lines[iter], "NULL symbol", MAX_SYMBOL_LENGTH);
            continue;
        }
        int status;
        char temp[MAX_SYMBOL_LENGTH];
        char* demangled = NULL;
        if (1 == (status = sscanf(symbol, SYMBOL_SSCANF, temp))) {
            if (NULL != (demangled = abi::__cxa_demangle(temp, NULL, NULL, &status))) {
                strncpy(trace._lines[iter], demangled, MAX_SYMBOL_LENGTH);
                free(demangled);
            }
            else {
                strncpy(trace._lines[iter], temp, MAX_SYMBOL_LENGTH);
            }
        }
        else {
            strncpy(trace._lines[iter], symbol, MAX_SYMBOL_LENGTH);
        }
        trace._lines[iter][MAX_SYMBOL_LENGTH - 1] = '\0';
    }
    return size;
}

using namespace std;
string stack_trace(int ignoreFrames)
{
    static const char STACK_INDENT[] = "    ";
    static const char BACKTRACE_START[] = "[BACKTRACE START]\n";
    static const char BACKTRACE_END[] = "[BACKTRACE END]\n";

    ostringstream out;
    StackTrace trace(512, 32);
    size_t size = demangled_symbols(trace, ignoreFrames);
    out << string(BACKTRACE_START) << flush;
    for (size_t i = 0; i < size; ++i) {
        out << STACK_INDENT << trace._lines[i] << endl;
    }
    out << string(BACKTRACE_END) << flush;
    return out.str();
}

