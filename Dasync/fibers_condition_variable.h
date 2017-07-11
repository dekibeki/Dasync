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
          Globals& globals{ Global_holder<Tag>::globals };
          Ts_globals& ts{ Global_holder<Tag>::ts_globals };

          /*we're on a valid thread running fibers*/
          assert(ts.thread);

          std::unique_lock<Inter_thread_mutex> my_lock{ thr_mutex_ };

          /*release the given lock*/
          lock.unlock();

          /*set this fiber to wait*/
          waiting_.emplace_back(ts.cur_job);

          ts.locks.emplace_back(&my_lock);

          /*and switch to a different fiber*/
          yield<Globla_holder<Tag>>(globals, ts);

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
          Globals& globals{ Global_holder<Tag>::globals };
          Ts_globals& ts{ Global_holder<Tag>::ts_globals };
          /*acquire our lock*/
          std::lock_guard<Inter_thread_mutex> guard{ thr_mutex_ };

          /*if we have something waiting*/
          if (!waiting_.empty()) {
            /*get it and set to runnable*/
            Job* to_run = waiting_.front();
            waiting_.pop_front();

            job_runnable(globals, ts, *to_run);
          }
        }
        /*wake all waiting fibers*/
        void notify_all() {
          Job* to_run;
          Globals& globals{ Global_holder<Tag>::globals };
          Ts_globals& ts{ Global_holder<Tag>::ts_globals };
          /*acquire our lock*/
          std::lock_guard<Inter_thread_mutex> guard{ thr_mutex_ };

          /*while we have things waiting*/
          while (!waiting_.empty()) {
            /*get it and set to runnable*/
            to_run = waiting_.front();
            waiting_.pop_front();

            job_runnable(globals, ts, *to_run);
          }
        }
      private:
        /*to synchronize between threads running fibers*/
        typename Inter_thread_mutex thr_mutex_;
        /*queue of fibers waiting*/
        std::deque<Job*> waiting_;
      };
    }
  }
}