#include <stdio.h>
#include <string.h> // strlen, etc
#include <ctype.h> // isspace
#include <map>

struct string {
    char * buffer = nullptr; // null-terminated
    int length = 0; // minimum length 1 (includes null terminator)
    // default
    string();
    // copy cstr wholly
    string(const char * byref);
    // copy cstr by length
    string(const char * byref, int count);
    // semantic copy
    string(const string & other);
    // semantic move
    string(string && other);
    // semantic move assignment
    string& operator=(string&& other);
    int operator<(const string & other) const;
    // destructor
    ~string();
};

enum {
    NONE,
    TEXT,
    VALUE
};

struct confval {
    bool mode = NONE;
    string text;
    float real = 0;
};

struct deconf {
    std::map<string, confval> list;
    bool has(const char * key);
    bool is_string(const char * key);
    char * get_string(const char * key);
    void make_string(const char * key, const char * value);
};

deconf deconf_load(const char * filename);

void dump_deconf(deconf data);
