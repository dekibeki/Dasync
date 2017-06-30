#include "fibers.h"

#include "platform_specific.h"

namespace {
  /*function used by the nicer plural register/start threads*/
  void batch_start_thread(std::mutex& start_mutex, bool& good_start, size_t i) {
    {
      /*wait until the parent thread says we can start*/
      std::lock_guard<std::mutex> reg_guard{ start_mutex };
    }
    /*if something went wrong, exit instead of continuing*/
    if (!good_start) {
      return;
    }
    /*start the thread running fibers*/
    dasync::fibers::start_thread(i);
  }

  /*join on all joinable threads in the vector of threads*/
  void join_all(std::vector<std::thread>& threads) {
    auto end_iter = threads.end();

    for (auto iter = threads.begin(); iter != end_iter; ++iter) {
      if (iter->joinable()) {
        iter->join();
      }
    }
  }
}

void dasync::fibers::prevent_closure() {
  impl::can_close = false;
}

void dasync::fibers::allow_closure() {
  /*put it in a mutex to stop threads from sleeping and not being notified*/
  std::lock_guard<std::mutex> guard{ impl::sleep_mutex };

  impl::can_close = true;

  /*wake all the sleeping threads so they can close*/
  while (!impl::sleeping_threads.empty()) {
    impl::sleeping_threads.back()->sleep_condition.notify_one();
    impl::sleeping_threads.pop_back();
  }
}

void dasync::fibers::init_fibers(size_t n_threads) {
  /*make a new array, set the size*/
  impl::threads = std::make_unique<impl::Thread[]>(n_threads);
  impl::thread_count = n_threads;
}

void dasync::fibers::run_fibers(Fiber* f, size_t n) {
  /*just run fiber on each fiber*/

  for (size_t i = 0; i < n; ++i) {
    impl::run_fiber(f[i]);
  }
}

void dasync::fibers::start_thread(size_t i) {
  assert(impl::threads);
  assert(i < impl::thread_count);

  impl::ts_thread = i;

  //yield to start the running
  impl::yield(impl::threads[i]);

  impl::ts_thread = impl::bad_thread_index;
}

int dasync::fibers::pin_and_run_threads() {

  std::vector<std::thread> threads;
  threads.reserve(impl::thread_count);

  std::mutex start_mutex;
  std::unique_lock<std::mutex> start_lock{ start_mutex };
  bool good_start{ false };

  for (size_t i = 0; i < impl::thread_count; ++i) {
    /*make the thread*/
    threads.emplace_back(std::bind(batch_start_thread,
      std::ref(start_mutex), std::ref(good_start), i));

    /*try and pin it*/
    if (platform_specific::pin_thread(i, threads.back()) != 0) {
      /*couldn't pin it, wait for them all to close then return*/
      start_lock.unlock();
      join_all(threads);
      return -1;
    }
  }

  /*we have had a good start*/
  good_start = true;
  /*let them run*/
  start_lock.unlock();
  /*join on them all*/
  join_all(threads);
  return 0; /*success*/
}
