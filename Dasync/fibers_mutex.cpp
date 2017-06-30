#include "fibers_mutex.h"

dasync::fibers::Mutex::Mutex() :
  thr_mutex_{},
  holder_{ nullptr },
  waiting_{} {

}

void dasync::fibers::Mutex::lock() {
  /*we've been inited*/
  assert(impl::threads);
  
  const size_t thread_index = impl::ts_thread;

  /*we're running on a thread running fibers*/
  assert(thread_index < impl::thread_count);

  std::unique_lock<impl::Inter_thread_mutex> lock{ thr_mutex_ };

  impl::Fiber* const cur_fiber = impl::threads[thread_index].current_fiber;

  if (holder_ == nullptr) {
    /*we're now the owner, return*/
    holder_ = cur_fiber;
    return;
  } else {
    /* this isn't recursive*/
    assert(holder_ != cur_fiber);

    /*put ourselves in the queue*/
    waiting_.emplace_back(cur_fiber);

    /*and yield*/
    impl::yield(impl::threads[thread_index], &lock);

    /*when we return, we should be the owner*/
    assert(holder_ == cur_fiber);
  }
}

void dasync::fibers::Mutex::unlock() {
  std::unique_lock<impl::Inter_thread_mutex> lock{ thr_mutex_ };

  /*check we own it*/
  assert(holder_ == impl::threads[impl::ts_thread].current_fiber);

  /*if there's someone waiting, dequeue them and set them runnable*/
  if (!waiting_.empty()) {
    holder_ = waiting_.front();
    waiting_.pop_back();
    impl::run_fiber(*holder_);
  }
}