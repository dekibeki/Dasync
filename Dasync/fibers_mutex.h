#pragma once

#include <mutex>
#include <deque>

#include <Dasync/fibers_impl.h>

namespace dasync {
  namespace impl {
    namespace fibers {
      /* A structure for a mutex that synchronizes fibers not threads*/
      template<typename Tag = impl::fibers::Default_tag>
      class Mutex {
        using Impl = impl::fibers::Impl<Tag>;
      public:
        /*only default constructor, no moving or copying*/
        Mutex() :
          thr_mutex_{},
          holder_{ nullptr },
          waiting_{} {

        }
        Mutex(Mutex const&) = delete;
        Mutex(Mutex&&) = delete;

        Mutex& operator=(Mutex const&) = delete;

        /*lock the mutex*/
        void lock() {
          /*we've been inited*/
          assert(Impl::threads);

          const size_t thread_index = Impl::ts_thread;

          /*we're running on a thread running fibers*/
          assert(thread_index < Impl::thread_count);

          std::unique_lock<typename Impl::Inter_thread_mutex> lock{ thr_mutex_ };

          impl::fibers::Fiber* const cur_fiber = Impl::threads[thread_index].current_fiber;

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
            Impl::yield(Impl::threads[thread_index], &lock);

            /*when we return, we should be the owner*/
            assert(holder_ == cur_fiber);
          }
        }
        /*unlock the mutex*/
        void unlock() {
          std::unique_lock<typename Impl::Inter_thread_mutex> lock{ thr_mutex_ };

          /*check we own it*/
          assert(holder_ == Impl::threads[impl::ts_thread].current_fiber);

          /*if there's someone waiting, dequeue them and set them runnable*/
          if (!waiting_.empty()) {
            holder_ = waiting_.front();
            waiting_.pop_back();
            Impl::run_fiber(*holder_);
          }
        }
      private:
        /*mutex to stop other threads from interfering*/
        typename Impl::Inter_thread_mutex thr_mutex_;
        /*the fiber that currently owns the mutex*/
        impl::fibers::Fiber* holder_;
        /*a list of waiting fibers*/
        std::deque<impl::fibers::Fiber*> waiting_;
      };
    }
  }
}