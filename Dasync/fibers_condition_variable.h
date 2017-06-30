#pragma once

#include <mutex>
#include <deque>
#include <Dasync/fibers_impl.h>
#include <Dasync/fibers_mutex.h>

namespace dasync {
  namespace impl {
    namespace fibers {
      /* A structure for a condition variable that synchronizes fibers not threads*/
      template<typename Tag = impl::fibers::Default_tag>
      class Condition_variable {
        using Impl = impl::fibers::Impl<Tag>;
      public:
        /*only default constructor, no moving or copying*/
        Condition_variable() :
          thr_mutex_{},
          waiting_{} {

        }
        Condition_variable(Condition_variable const&) = delete;
        Condition_variable(Condition_variable&&) = delete;

        Condition_variable& operator=(Condition_variable const&) = delete;

        /*wait to be notified, same as std::condition_variable*/
        void wait(std::unique_lock<Mutex<Tag>>& lock) {
          const size_t thread_index = Impl::ts_thread;

          /*we're on a valid thread running fibers*/
          assert(thread_index < Impl::thread_count);

          std::unique_lock<typename Impl::Inter_thread_mutex> my_lock{ thr_mutex_ };

          /*release the given lock*/
          lock.unlock();

          /*set this fiber to wait*/
          waiting_.emplace_back(Impl::threads[thread_index].current_fiber);

          /*and switch to a different fiber*/
          Impl::yield(Impl::threads[thread_index], &my_lock);

          /*we have to regain lock before we return */
          lock.lock();
        }

        /*wait to be notified and f is true, same as std::condition_variable*/
        template<typename F>
        void wait(std::unique_lock<Mutex<Tag>>& lock, F&& f) {
          while (!f()) {
            wait(lock);
          }
        }

        /*wake one waiting fiber*/
        void notify_one() {
          /*acquire our lock*/
          std::lock_guard<typename Impl::Inter_thread_mutex> guard{ thr_mutex_ };

          /*if we have something waiting*/
          if (!waiting_.empty()) {
            /*get it and set to runnable*/
            Impl::Fiber* to_run = waiting_.front();
            waiting_.pop_front();

            Impl::run_fiber(*to_run);
          }
        }
        /*wake all waiting fibers*/
        void notify_all() {
          impl::Fiber* to_run;
          /*acquire our lock*/
          std::lock_guard<typename Impl::Inter_thread_mutex> guard{ thr_mutex_ };

          /*while we have things waiting*/
          while (!waiting_.empty()) {
            /*get it and set to runnable*/
            to_run = waiting_.front();
            waiting_.pop_front();

            Impl::run_fiber(*to_run);
          }
        }
      private:
        /*to synchronize between threads running fibers*/
        typename Impl::Inter_thread_mutex thr_mutex_;
        /*queue of fibers waiting*/
        std::deque<impl::fibers::Fiber*> waiting_;
      };
    }
  }
}