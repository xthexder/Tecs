#pragma once

#ifndef TECS_EXPORT
    #ifdef TECS_SHARED_INTERNAL
        /* We are building this library */
        #ifdef _WIN32
            #define TECS_EXPORT __declspec(dllexport)
        #else
            #define TECS_EXPORT __attribute__((__visibility__("default")))
        #endif
    #else
        /* We are using this library */
        #ifdef _WIN32
            #define TECS_EXPORT __declspec(dllimport)
        #else
            #define TECS_EXPORT
        #endif
    #endif
#endif
