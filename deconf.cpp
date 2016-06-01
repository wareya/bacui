#include "deconf.hpp"

string::string() { }
string::string(const char * byref)
{
    if(byref != nullptr)
    {
        length = strlen(byref);
        buffer = (char*)malloc(length+1);
        memcpy(buffer, byref, length);
        buffer[length] = 0;
    }
}
// copy cstr by length
string::string(const char * byref, int count)
{
    if(byref != nullptr)
    {
        length = strlen(byref);
        length = length > count ? count : length;
        buffer = (char*)malloc(length+1);
        memcpy(buffer, byref, length);
        buffer[length] = 0;
    }
}
// semantic copy
string::string(const string & other)
{
    buffer = (char*)malloc(other.length);
    length = other.length;
    memcpy(buffer, other.buffer, length);
}
// semantic move
string::string(string && other)
{
    buffer = other.buffer;
    length = other.length;
    other.buffer = nullptr;
}
// semantic move assignment
string& string::operator=(string&& other)
{
    buffer = other.buffer;
    length = other.length;
    other.buffer = nullptr;
    return *this;
}
int string::operator<(const string & other) const
{
    if(buffer == nullptr and other.buffer == nullptr) return 0;
    if(buffer == nullptr) return true;
    if(other.buffer == nullptr) return 0;
    return strcmp(buffer, other.buffer) < 0;
}
// destructor
string::~string()
{
    if(buffer)
        free(buffer);
}

// outputs a NEW string
string trim (const string & str)
{
    if(str.length <= 0 or str.buffer == nullptr) // zero-length nonterminated string
        return string(nullptr);
    int s = 0;
    while (s < str.length and isspace(str.buffer[s]))
        s++;
    int e = str.length-1-1; // one for zero-indexed, one for null terminator
    if(e < 0) // string consists of a single character only (hopefully the null terminator)
        return string("");
    while (e > 0 and isspace(str.buffer[e]))
        e--;
    //printf("'%s' | %d | %d\n", str.buffer, s, e);
    //'a' | 0 | 0
    //'a ' | 0 | 0
    //' ' | 1 | 0
    //'  ' | 2 | 0
    //' a' | 1 | 1
    auto span = e-s+1; // for off-by-one, not null terminator
    if(span <= 0) // string consists of nothing but whitespace
        return string("");
    
    string output{};
    output.length = span+1; // for null terminator
    output.buffer = (char*)malloc(output.length);
    
    memcpy(output.buffer, str.buffer+s, span);
    output.buffer[span] = 0;
    
    return output;
}

string getline(FILE * f)
{
    if(!f) return string();
    if(ferror(f) or feof(f)) return string();
    
    auto start = ftell(f);
    int c;
    while(c = fgetc(f), c != EOF and c != '\n');
    
    if(ferror(f) or feof(f)) return string();
    
    auto end = ftell(f) - (c == EOF);
    int len = end-start;
    auto str = (char*)malloc(len);
    
    fseek(f, start, SEEK_SET);
    int i = 0;
    while(i < len) str[i++] = fgetc(f);
    str[i] = 0;
    
    return string(str, len);
}

bool deconf::has(const char * key)
{
    auto len = strlen(key);
    for(auto e : list)
    {
        if(strncmp(key, e.first.buffer, len) == 0)
            return true;
        else
            continue;
    }
    return false;
}

bool deconf::is_string(const char * key)
{
    if(!has(key)) return false;
    return list[string(key)].mode == TEXT;
}

char * deconf::get_string(const char * key)
{
    if(!is_string(key)) return nullptr;
    return list[string(key)].text.buffer;
}

void deconf::make_string(const char * key, const char * value)
{
    list[string(key)] = confval({TEXT, string(value), 0});
}

deconf deconf_load(const char * filename)
{
    deconf data;
    auto f = fopen(filename, "r");
    if(!f) return data;
    for(auto str = getline(f); str.buffer; str = getline(f))
    {
        if(trim(str).buffer[0] == '\0')
            continue;
        
        auto index = strchr(str.buffer, '=');
        if(index == nullptr)
            continue;
        
        auto left = trim(string(str.buffer, index-str.buffer+1));
        auto right = trim(string(index+1));
        if(left.length == 0 or right.length == 0)
            continue;
        
        //printf("'%s':'%s'\n", left.buffer, right.buffer);
        
        float rval;
        int mode = TEXT;
        try
        {
            rval = std::stof(right.buffer, NULL);
            mode = VALUE;
        }
        catch (std::invalid_argument)
        {
            rval = 0;
            mode = TEXT;
        }
        catch (std::out_of_range)
        {
            rval = 0;
            mode = TEXT;
        }
        
        if(mode == TEXT)
            data.list[left] = confval({TEXT, right, 0});
        if(mode == VALUE)
            data.list[left] = confval({TEXT, string(), rval});
        
        free(str.buffer);
    }
    fclose(f);
    return data;
}

void dump_deconf(deconf data)
{
    puts("Dumping deconf.");
    for(auto e : data.list)
    {
        printf("%s\t", e.first.buffer);
        if(e.second.text.buffer)
            printf(": TEXT %s\t", e.second.text.buffer);
        printf(": REAL %f\t", e.second.real);
        puts("");
    }
    puts("Done.");
}
