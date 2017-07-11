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
          impl::fibers::Globals& globals{ Global_holder<Tag>::globals };
          impl::fibers::Ts_globals& ts{ Global_holder<Tag>::ts_globals };

          /*we've been inited*/
          assert(globals.threads);
          /*we're running on a thread running fibers*/
          assert(ts.thread);

          std::unique_lock<Inter_thread_mutex> lock{ thr_mutex_ };

          Job* const cur_job = ts.cur_job;

          /*a non job can't block/use a mutex*/
          assert(cur_job);

          if (holder_ == nullptr) {
            /*we're now the owner, return*/
            holder_ = cur_job;
            return;
          } else {
            /* this isn't recursive*/
            assert(holder_ != cur_job);

            /*put ourselves in the queue*/
            waiting_.emplace_back(cur_job);

            /*set up the mutex to be unlocked post switch*/
            ts.locks.emplace_back(&lock);
            /*and yield*/
            yield<Global_holder<Tag>>(globals, ts);

            /*when we return, we should be the owner*/
            assert(holder_ == cur_fiber);
          }
        }
        /*unlock the mutex*/
        void unlock() {

          Globals& globals = Global_holder<Tag>::globals;
          Ts_globals& ts = Global_holder<Tag>::ts_globals;

          std::unique_lock<impl::fibers::Inter_thread_mutex> lock{ thr_mutex_ };

          /*check we own it*/
          assert(holder_ == ts.cur_job);

          /*if there's someone waiting, dequeue them and set them runnable*/
          if (!waiting_.empty()) {
            holder_ = waiting_.front();
            waiting_.pop_front();
            job_runnable(globals, ts, *holder_);
          }
        }
      private:
        /*mutex to stop other threads from interfering*/
        Inter_thread_mutex thr_mutex_;
        /*the job that currently owns the mutex*/
        Job* holder_;
        /*a list of waiting fibers*/
        std::deque<Job*> waiting_;
      };
    }
  }
}