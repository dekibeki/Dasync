#ifdef _WIN32

#include <cassert>

#include "platform_specific.h"

#define WINDOWS_LEAN_AND_MEAN

#include <Windows.h>

int dasync::platform_specific::pin_thread(size_t core, std::thread& thread) {
  /*windows doesn't let you pin to more than 64 cores*/
  assert(core < 64);
  /* try and pin*/
  if (SetThreadAffinityMask(thread.native_handle(), 1ull << core) == 0) {
    return -1; /*failure*/
  } else {
    return 0; /*success*/
  }
}

#endif