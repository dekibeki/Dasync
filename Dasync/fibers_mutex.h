#pragma once

#include <mutex>
#include <deque>

#include <Dasync/fibers_impl.h>

namespace dasync {
  namespace fibers {
    /* A structure for a mutex that synchronizes fibers not threads*/
    class Mutex {
    public:
      /*only default constructor, no moving or copying*/
      Mutex();
      Mutex(Mutex const&) = delete;
      Mutex(Mutex&&) = delete;

      Mutex& operator=(Mutex const&) = delete;

      /*lock the mutex*/
      void lock();
      /*unlock the mutex*/
      void unlock();
    private:
      /*mutex to stop other threads from interfering*/
      impl::Inter_thread_mutex thr_mutex_;
      /*the fiber that currently owns the mutex*/
      impl::Fiber* holder_;
      /*a list of waiting fibers*/
      std::deque<impl::Fiber*> waiting_;
    };
  }
}