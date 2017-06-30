#include "fibers_impl.h"

#include "platform_specific.h"

namespace {
  
  
}

dasync::impl::fibers::Fiber::Fiber() {

}

/*thread constructor, just set everything to defaults*/
dasync::impl::fibers::Thread::Thread() :
  initial_context{},
  unlocking{ nullptr },
  current_fiber{ nullptr },
  new_fiber{ nullptr }{
}

/*initialize everything to defaults*/
dasync::impl::fibers::Counter::Counter() :
  count{ 0 },
  waiting{ nullptr },
  waiting_for{ 0 } {

}