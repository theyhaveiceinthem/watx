#ifndef WATX_EXPORT_H
#define WATX_EXPORT_H
#if defined(_WIN32) && defined(WATX_SHARED)
#if defined(WATX_BUILD)
#define WATX_API __declspec(dllexport)
#else
#define WATX_API __declspec(dllimport)
#endif
#else
#define WATX_API
#endif
#endif
