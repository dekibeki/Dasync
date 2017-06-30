#include "fibers_condition_variable.h"

dasync::fibers::Condition_variable::Condition_variable() :
  thr_mutex_{},
  waiting_{} {

}

void dasync::fibers::Condition_variable::wait(std::unique_lock<Mutex>& lock) {
  const size_t thread_index = impl::ts_thread;

  /*we're on a valid thread running fibers*/
  assert(thread_index < impl::thread_count);

  std::unique_lock<impl::Inter_thread_mutex> my_lock{ thr_mutex_ };

  /*release the given lock*/
  lock.unlock();

  /*set this fiber to wait*/
  waiting_.emplace_back(impl::threads[thread_index].current_fiber);

  /*and switch to a different fiber*/
  impl::yield(impl::threads[thread_index], &my_lock);

  /*we have to regain lock before we return */
  lock.lock();
}

void dasync::fibers::Condition_variable::notify_one() {
  /*acquire our lock*/
  std::lock_guard<impl::Inter_thread_mutex> guard{ thr_mutex_ };

  /*if we have something waiting*/
  if (!waiting_.empty()) {
    /*get it and set to runnable*/
    impl::Fiber* to_run = waiting_.front();
    waiting_.pop_front();

    impl::run_fiber(*to_run);
  }
}

void dasync::fibers::Condition_variable::notify_all() {
  impl::Fiber* to_run;
  /*acquire our lock*/
  std::lock_guard<impl::Inter_thread_mutex> guard{ thr_mutex_ };

  /*while we have things waiting*/
  while (!waiting_.empty()) {
    /*get it and set to runnable*/
    to_run = waiting_.front();
    waiting_.pop_front();

    impl::run_fiber(*to_run);
  }
}