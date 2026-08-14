#ifndef KAGURA_PRISMAPI_H
#define KAGURA_PRISMAPI_H
#pragma once

#include <stdint.h>

// Minimal subset of Prism's public C API, transcribed from upstream include/prism.h
// (https://github.com/ethindp/prism, MPL-2.0).
//
// It is copied rather than included so Kagura can load prism.dll at runtime instead
// of linking against it: the plugin lives in mods\Kagura\ and Windows resolves a
// statically imported DLL against the game's exe directory, not ours.
//
// PrismConfig's layout must match upstream exactly - prism_config_init returns it
// by value, which on x64 means a hidden return pointer. Declaring the real struct
// lets the compiler generate that call correctly instead of guessing at the ABI.

extern "C"
{
    typedef struct PrismContext  PrismContext;
    typedef struct PrismBackend  PrismBackend;
    typedef struct PrismRegistry PrismRegistry;
    typedef uint64_t             PrismBackendId;

    // Values must stay in upstream's declaration order - they are what the DLL
    // actually returns. prism_error_string turns any of them into text.
    typedef int PrismError;
    enum
    {
        PRISM_OK = 0,
        PRISM_ERROR_NOT_INITIALIZED,
        PRISM_ERROR_INVALID_PARAM,
        PRISM_ERROR_NOT_IMPLEMENTED,
        PRISM_ERROR_NO_VOICES,
        PRISM_ERROR_VOICE_NOT_FOUND,
        PRISM_ERROR_SPEAK_FAILURE,
        PRISM_ERROR_MEMORY_FAILURE,
        PRISM_ERROR_RANGE_OUT_OF_BOUNDS,
        PRISM_ERROR_INTERNAL,
        PRISM_ERROR_NOT_SPEAKING,
        PRISM_ERROR_NOT_PAUSED,
        PRISM_ERROR_ALREADY_PAUSED,
        PRISM_ERROR_INVALID_UTF8,
        PRISM_ERROR_INVALID_OPERATION,
        PRISM_ERROR_ALREADY_INITIALIZED,
        PRISM_ERROR_BACKEND_NOT_AVAILABLE,
        PRISM_ERROR_UNKNOWN
    };

    typedef void (__cdecl *PrismAvailabilityCallback)(void *userdata, PrismBackendId backend,
                                                      const char *name, bool available);

    typedef struct
    {
        uint8_t                   version;
        PrismRegistry            *registry;
        PrismAvailabilityCallback availability_callback;
        void                     *availability_userdata;
        uint32_t                  availability_poll_interval_ms;
        uint32_t                  availability_debounce_samples;
        uint32_t                  availability_backoff_max_ms;
        bool                      availability_auto_power_manage;
    } PrismConfig;

    #define PRISM_CONFIG_VERSION 3
}

#endif
