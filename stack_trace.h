#include <string>

struct StackTrace {
    explicit StackTrace(size_t maxLineLength, size_t depth) {
        _maxLineLength = maxLineLength; 
        _depth = depth;
        _lines = new char*[_depth];
        for (size_t i = 0; i < _depth; ++i) {
            _lines[i] = new char[_maxLineLength];
        }
    }
  
    ~StackTrace() {
        for (size_t i = 0; i < _depth; ++i) {
            delete _lines[i];
        }
        delete _lines;
    }
 
    size_t _maxLineLength;
    size_t _depth;
    char** _lines;
};

std::string stack_trace(int ignoreFrames = 0);
size_t demangled_symbols(struct StackTrace& trace, size_t ignoreFrames = 0);

