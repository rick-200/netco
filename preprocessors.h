#pragma once
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
[[noreturn]] inline void fallabort(const char* exp, const char* file, const char* func, int line, const char* msg) {
  if (msg == nullptr)
    fprintf(stderr, "assert failed: '%s'\n  at file:%s\n  at func:%s\n  at line:%d\n", exp, file, func, line);
  else
    fprintf(stderr, "assert failed: '%s'\n  at file:%s\n  at func:%s\n  at line:%d\n        msg:%s\n", exp, file, func, line, msg);
  /*if (errno != 0) */ fprintf(stderr, "  errno:%d '%s'\n", errno, strerror(errno));
  abort();
}
#ifdef _DEBUG
#define VERIFY(expr) static_cast<bool>(expr) ? void(0) : fallabort(#expr, __FILE__, __FUNCTION__, __LINE__, nullptr)
#define VERIFYX(expr, msg) static_cast<bool>(expr) ? void(0) : fallabort(#expr, __FILE__, __FUNCTION__, __LINE__, msg)

#define ASSERT(expr) VERIFY(expr)
#define ASSERTX(expr, msg) VERIFYX(expr, msg)
#else
#define VERIFY(expr) static_cast<bool>(expr) ? void(0) : fallabort(#expr, __FILE__, __FUNCTION__, __LINE__, nullptr)
#define VERIFYX(expr, msg) static_cast<bool>(expr) ? void(0) : fallabort(#expr, __FILE__, __FUNCTION__, __LINE__, msg)

#define ASSERT(expr) ((void)0)
#define ASSERTX(expr, msg) ((void)0)
#endif
#define MOVP(p, x) ((void*)(((char*)(p)) + x))