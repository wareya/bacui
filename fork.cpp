/*
PUBLIC LICENSE

Copyright 2016, Alexander 'Wareya' Nadeau <wareya@gmail.com>

This software is provided 'as-is', without any express or implied warranty. In
no event will the authors be held liable for any damages arising from the use
of this software.

Permission is granted to anyone to use this software and/or its source code for
any purpose, including commercial applications, and to alter it and distribute
it freely, provided that the above copyright notice and this permission notice
appear in all source code distributions.

It is not necessary for this license to appear in non-substantial portions of
source code, and not necessary for this license to appear in end-user products.

END PUBLIC LICENSE
*/

#include <SDL2/SDL.h>
#include <iostream>
#include <atomic>
#include <stdint.h>
#include <stdio.h>
#include <ncurses.h>
#include <vector>

#include "include/m64p_config.h"
#include "include/m64p_common.h"
#include "include/m64p_frontend.h"
#include "include/m64p_debugger.h"

#include "coreapi.h"

#include "deconf.hpp"

#define XM(X) ptr_##X X;
COREAPI
#undef XM

int initcore(void *& core)
{
    #define XM(X) \
    if(!(X = (ptr_##X)SDL_LoadFunction(core, #X)))\
        return printf("%s\n", SDL_GetError()), -1;
    COREAPI
    #undef XM
}

#define TRY_OR_DIE(X, Y) \
    if(auto error = X) \
		return printf("Error: %s\n", Y(error))&-1;
#define VERSION(Mj, Mn) ((Mj)<<16|(Mn))

char * romdata;
uint32_t romsize;

int loadrom(const char * fname)
{
    auto start = SDL_GetTicks();
    puts("Loading ROM... (This usually takes about two seconds)");
    FILE * rom = fopen(fname, "rb");
    if(!rom) return puts("ROM file does not exist. Doublecheck the filename."), -1;
    
    fseek(rom, 0, SEEK_END);
    romsize = ftell(rom);
    fseek(rom, 0, SEEK_SET);
    romdata = (char*)malloc(romsize);
    
    if(!romdata) return puts("Allocation error when loading ROM."), fclose(rom), -1;
    if(fread(romdata, 1024, romsize/1024, rom)*1024 != romsize) return puts("Failed to load ROM data into RAM. (ROM filesize might not be a 1024-byte multiple)"), free(romdata), fclose(rom), -1;
    
    fclose(rom);
    
    if(auto error = CoreDoCommand(M64CMD_ROM_OPEN, romsize, romdata))
    {
        free(romdata);
        printf("Error: %s\n",CoreErrorMessage(error));
        return -1;
    }
    
    puts("Done loading ROM.");
    
    auto end = SDL_GetTicks();
    
    printf("Time to load ROM: %.3f\n", (end-start)/1000.0f);
    
    free(romdata); // The core copies the ROM buffer so we can free it immediately even if we don't error out.
    
    return 0;
}

void(*real_print)(const char *, int, const char *);

#include <deque>
#include <string>
#include <sstream>

SDL_mutex * logmutex;
std::deque<std::string> msglog;

#define msglog_height 6

void print_terminal(const char * ctx, int level, const char * msg)
{
    printf("%s: %s\n", ctx, msg);
    std::ostringstream temp;
    temp << ctx << ": "
    << ((level == M64MSG_WARNING) ? "WARN : " : (level == M64MSG_ERROR) ? "ERROR : " : "")
    << msg;
    msglog.push_back(temp.str());
    while(msglog.size() > msglog_height) msglog.pop_front();
}

void print_curses(const char * ctx, int level, const char * msg)
{
    printf("%s: %s\n", ctx, msg);
    std::ostringstream temp;
    temp << ctx << ": " << msg;
    msglog.push_back(temp.str());
    while(msglog.size() > msglog_height) msglog.pop_front();
}

void debug(void * ctx, int level, const char * msg)
{
    // video plugin messages are *important*
    if(level <= M64MSG_WARNING or (strcmp((const char *)ctx, "Video") == 0 and level <= M64MSG_STATUS))
    {
        if(SDL_LockMutex(logmutex) == 0)
        {
            real_print((const char *)ctx, level, msg);
            SDL_UnlockMutex(logmutex);
        }
    }
    fflush(stdout);
    fflush(stderr);
}

namespace Plug
{
    void * Video;
    ptr_PluginStartup VideoStartup;
    
    void * Audio;
    ptr_PluginStartup AudioStartup;
    
    void * RSP;
    ptr_PluginStartup RSPStartup;
    
    void * Input;
    ptr_PluginStartup InputStartup;
}

template<typename funcptr>
funcptr LoadFunction ( const char * funcname, void * object )
{
    if(auto function = (funcptr)SDL_LoadFunction(object, funcname))
        return function;
    else
        return printf( "Could not find reference %s in %s dynamic library\n"
                     , SDL_GetError()
                     , (object == Plug::Video?"Video":
                        object == Plug::Audio?"Audio":
                        object == Plug::RSP  ?"RSP":
                        object == Plug::Input?"Input":
                        "Core?"))
             , nullptr;
}

std::atomic<bool> emulating;

int emulate()
{
    TRY_OR_DIE(CoreDoCommand(M64CMD_EXECUTE, 0, NULL), CoreErrorMessage)
    emulating = 0;
    return 0;
}

#define C_INVALID '.'
#define C_UNPRINTABLE '.'

void blit_char(char c, bool underline = false)
{
    unsigned char uc = (unsigned char)c;
    if(uc < 32) return (void)addch(c+32 | A_BOLD | (A_UNDERLINE * underline));
    if(uc == 127) return (void)addch('.' | A_BOLD | (A_UNDERLINE * underline));
    if(uc > 127) return blit_char(c-128, true);
    return (void)addch(c);
}

void print_custom(const char * str, uint32_t len)
{
    if(len == 0)
    {
        for(auto i = 0; str[i] != 0; i++)
            blit_char(str[i]);
    }
    else
    {
        for(auto i = 0; i < len; i++)
            blit_char(str[i]);
    }
}

#ifdef _WIN32
#define TERMINAL "con"
#else  //  _WIN32
#define TERMINAL "/dev/tty"
#endif //  _WIN32

#define VideoType M64PLUGIN_GFX
#define AudioType M64PLUGIN_AUDIO
#define InputType M64PLUGIN_INPUT
#define RSPType   M64PLUGIN_RSP

FILE * real_stdout;

int init()
{
    // environment
    
    freopen("log.txt", "w", stdout);
    freopen("err.txt", "w", stderr);
    real_print = print_terminal;
    
    // set up curses
    
    real_stdout = fopen(TERMINAL, "w");
    if(!real_stdout) return puts("Could not open file handle to stdout. Good job."), -1;
    auto s = newterm(NULL, real_stdout, stdin);
    
    start_color();
    use_default_colors();
    
    init_pair(1, COLOR_GREEN, COLOR_BLACK);
    init_pair(2, -1, -1);
    init_pair(3, COLOR_WHITE, COLOR_BLACK);
    
    clear();
    cbreak();
    noecho();
    nonl();
    
    // debug
    
    fflush(stdout);
    fflush(stderr);
    
    printw("Loading....");
    refresh();
    
    // internal
    
    if(SDL_Init(SDL_INIT_TIMER)) return puts("Could not initialize SDL. Check your OS."), -1;
    int version_conf, version_debug, version_video, version_extra;
    version_conf = version_debug = version_video = version_extra = -1;
    
    logmutex = SDL_CreateMutex();
    if(!logmutex) return puts("Could not initialize SDL mutex. Check your OS."), -1;
    
    auto settings = deconf_load("config.txt");
    
    // set up emulator

    if(!settings.is_string("core")) settings.make_string("core", "libmupen64plus.so.2");
    auto core = SDL_LoadObject(settings.get_string("core"));
    if(!core) return printf("Failed to load core. %s\n",SDL_GetError()), -1;
    
    initcore(core);
    
    fflush(stdout);
    fflush(stderr);
    
    TRY_OR_DIE(CoreGetAPIVersions(&version_conf, &version_debug, &version_video, &version_extra), CoreErrorMessage)
    
    printf("Debug version: %X.%X\n", version_debug>>16, version_debug&0xFFFF);
    
    TRY_OR_DIE(CoreStartup(VERSION(2,0), "config/", "config/", (void*)"Core", &debug, NULL, NULL), CoreErrorMessage)
    TRY_OR_DIE(DebugSetCallbacks(NULL, NULL, NULL), CoreErrorMessage)
    
    // plugins
    
    #define LOAD_PLUGIN(type) \
    Plug::type = SDL_LoadObject(type##plugin); \
    if(!Plug::type) return printf("Failed to load a plugin.\n%s\n",SDL_GetError()), -1; \
    if(!(Plug::type##Startup = LoadFunction<ptr_PluginStartup>("PluginStartup", Plug::type))) \
        return puts(#type " plugin is not a valid m64p plugin (no startup)."), -1; \
    if(auto error = Plug::type##Startup(core, (void *) #type, &debug)) \
        return printf(#type " plugin errored while starting up: %s\n", CoreErrorMessage(error)), -1; \
    else  puts(#type " plugin loaded successfully.");
    
    if(!settings.is_string("video")) settings.make_string("video", "mupen64plus-video-glide64mk2.so");
    if(!settings.is_string("audio")) settings.make_string("audio", "mupen64plus-audio-sdl.so");
    if(!settings.is_string("input")) settings.make_string("input", "mupen64plus-input-sdl.so");
    if(!settings.is_string("rsp"  )) settings.make_string("rsp"  , "mupen64plus-rsp-hle.so");
    auto Videoplugin = settings.get_string("video");
    auto Audioplugin = settings.get_string("audio");
    auto Inputplugin = settings.get_string("input");
    auto RSPplugin   = settings.get_string("rsp"  );
    
    LOAD_PLUGIN(Video)
    LOAD_PLUGIN(Audio)
    LOAD_PLUGIN(Input)
    LOAD_PLUGIN(RSP)
    
    ConfigSaveFile();
    
    if(!settings.is_string("rom")) settings.make_string("rom", "zelda.z64");
    if(loadrom(settings.get_string("rom"))) return puts("Failed ro load ROM."), -1;
    
    #define ATTACH(x) \
        if(auto error = CoreAttachPlugin(x##Type, Plug::x)) \
            return printf(#x " plugin errored while attaching: %s\n", CoreErrorMessage(error)), 0;
    
    ATTACH(Video)
    ATTACH(Audio)
    ATTACH(Input)
    ATTACH(RSP)
    
    ConfigSaveFile();
    
    return 0;
}

int runui(void * unused)
{
    enum {
        int_,
        float_,
        char_
    };
    struct watchlist_entry {
        uint32_t addr;
        uint32_t mode;
    };
    
    std::vector<watchlist_entry> watchlist;
    watchlist.push_back({0x802245B0+8   , float_});
    watchlist.push_back({0x802245B0+8+4 , float_});
    watchlist.push_back({0x802245B0+8+8 , float_});
    watchlist.push_back({0x802245B0+0x44, int_});
    watchlist.push_back({0x80200000     , char_});
    
    real_print = print_curses;
    char str[] = "0x80123456 : 00000000";
    uint32_t len_str = sizeof(str)-1;
    puts("Got here.");
    while(1)
    {
        if(!emulating)
        {
            puts("Emulator stopped.");
            break;
        }
        
        clear();
        int y, x, h, w;
        getmaxyx(stdscr, h, w);
        
        #define TITLEBAR(_y, text) move(_y, 0); attron(COLOR_PAIR(1)); hline(ACS_HLINE, w); attron(COLOR_PAIR(3)); printw(" " text " "); attron(COLOR_PAIR(2));
        TITLEBAR(0, "Debugger")
        
        y = 1;
        x = w-len_str-1;
        move(y++, x);
        printw("Watchlist:");
        for(auto e : watchlist)
        {
            if(e.mode == int_)
            {
                uint32_t value = DebugMemRead32(e.addr);
                snprintf(str, len_str+1, "0x%08X : %08X", e.addr, value);
            }
            if(e.mode == char_)
            {
                char * base = (char *) DebugMemGetPointer(M64P_DBG_PTR_RDRAM);
                if(base == nullptr) continue;
                sprintf(str, "0x%08X : ", e.addr);
                memcpy(str+13, (base+(e.addr&0x00FFFFFF)), 8);
            }
            if(e.mode == float_)
            {
                uint32_t value = DebugMemRead32(e.addr);
                float val = *(float*)&value;
                if(val == 0.0f)
                    sprintf(str, "0x%08X : 00000.00", e.addr);
                else if (val > 0)
                {
                    if     (val >= 1000000.f)
                        snprintf(str, len_str+1, "0x%08X : %.2e", e.addr, val);
                    else if(val >= 100000.0f)
                        snprintf(str, len_str+1, "0x%08X : %08.1f", e.addr, val);
                    else if(val < 0.000001f)
                        snprintf(str, len_str+1, "0x%08X : ~ +00.00", e.addr, val);
                    else if(val < 00000.01f)
                        snprintf(str, len_str+1, "0x%08X : %08.6f", e.addr, val);
                    else
                        snprintf(str, len_str+1, "0x%08X : %08f", e.addr, val);
                }
                else // val < 0
                {
                    if     (val <= -100000.f)
                        snprintf(str, len_str+1, "0x%08X : %.2e", e.addr, val);
                    else if(val <= -10000.0f)
                        snprintf(str, len_str+1, "0x%08X : %08.1f", e.addr, val);
                    else if(val > -0.00001f)
                        snprintf(str, len_str+1, "0x%08X : ~ -00.00", e.addr, val);
                    else if(val > -0000.01f)
                        snprintf(str, len_str+1, "0x%08X : %08.6f", e.addr, val);
                    else
                        snprintf(str, len_str+1, "0x%08X : %08.2f", e.addr, val);
                }
            }
            move(y++, x);
            print_custom(str, len_str);
        }
        
        if(SDL_TryLockMutex(logmutex) == 0)
        {
            TITLEBAR(h - msglog_height - 1, "Messages")
            x = 0;
            y = h - msglog.size();
            for(auto s : msglog)
                mvprintw(y++, x, s.data());
            SDL_UnlockMutex(logmutex);
        }
        refresh();
        
        SDL_Delay(16);
    }
    endwin();
}

int main()
{
    if(int r = init()) return puts("Init failed."), r;
    
    // boot
    emulating = 1;
    auto uithread = SDL_CreateThread(runui, "Interface Thread", NULL);
    
    // enter core loop
    if(emulate() != 0) emulating = 0;
    
    // shutdown
    SDL_WaitThread(uithread, nullptr);
    SDL_DestroyMutex(logmutex);
    
    fflush(stdout);
    fflush(stderr);
    
    fclose(real_stdout);
    freopen(TERMINAL, "w", stdout);
    puts("Emulator has stopped.");
    
    return 0;
}
