#pragma once

#include <mutex>
#include <deque>
#include <Dasync/fibers_impl.h>
#include <Dasync/fibers_mutex.h>

namespace dasync {
  namespace fibers {
    /* A structure for a condition variable that synchronizes fibers not threads*/
    class Condition_variable {
    public:
      /*only default constructor, no moving or copying*/
      Condition_variable();
      Condition_variable(Condition_variable const&) = delete;
      Condition_variable(Condition_variable&&) = delete;

      Condition_variable& operator=(Condition_variable const&) = delete;

      /*wait to be notified, same as std::condition_variable*/
      void wait(std::unique_lock<Mutex>& lock);

      /*wait to be notified and f is true, same as std::condition_variable*/
      template<typename F>
      void wait(std::unique_lock<Mutex>& lock, F&& f) {
        while (!f()) {
          wait(lock);
        }
      }

      /*wake one waiting fiber*/
      void notify_one();
      /*wake all waiting fibers*/
      void notify_all();
    private:
      /*to synchronize between threads running fibers*/
      impl::Inter_thread_mutex thr_mutex_;
      /*queue of fibers waiting*/
      std::deque<impl::Fiber*> waiting_;
    };
  }
}