#pragma once

#include <thread>

namespace dasync {
  namespace platform_specific {

    /*pin the thread to the core,
    
      returns 0 on success, -1 on failure
    
      if you get link errors for this not being defined,
      your platform currently isn't supported. Feel free
      to implement the function yourself*/
    int pin_thread(size_t core, std::thread& thread);
  }
}