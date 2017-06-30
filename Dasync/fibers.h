#pragma once

#include <thread>
#include <Dasync/fibers_impl.h>
#include <Dasync/platform_specific.h>
#include <Dasync/fibers_mutex.h>
#include <Dasync/fibers_condition_variable.h>

namespace dasync {

  namespace impl {
    namespace fibers {
      /*function used by pin_and_start as the thread entry point
      
      waits until pin_and_start says it can start, and then checks whether it
      was a 'good_start'*/
      template<typename Tag>
      void batch_start_thread(std::mutex& start_mutex, bool& good_start, size_t i);
      /*join on all joinable threads in the vector of threads*/
      void join_all(std::vector<std::thread>& threads);
    }
  }

  template<typename Tag = impl::fibers::Default_tag>
  struct fibers {
    /*ease of use typedef*/
    using Impl = impl::fibers::Impl<Tag>;

    /*allow users to easily use Priority*/
    using Priority = impl::fibers::Priority;

    class Fiber;
    class Counter;

    /*wrapper around counter, stops users from playing with internals*/
    class Counter {
    public:
      friend class Fiber;

      void wait_for(size_t i) {
        Impl::counter_wait_for(counter_, i);
      }
    private:
      impl::fibers::Counter counter_;
    };

    /*wrapper around counter, stops users from playing with internals*/
    class Fiber {
    public:
      Fiber() {}

      /*shortcut for initializing*/
      template<typename F>
      Fiber(F&& f, Counter* counter = nullptr, Priority priority = Impl::ts_get_current_priority()) {
        initialize(std::forward<F>(f), counter, priority);
      }

      /*initializes and runs, therefore must be called AFTER init_fibers*/
      template<typename F>
      void initialize(F&& f, Counter* counter = nullptr,
        Priority priority = Impl::ts_get_current_priority()) {
        if (counter) {
          Impl::fiber_initialize(fiber_, std::forward<F>(f), &counter->counter_, priority);
        } else {
          Impl::fiber_initialize(fiber_, std::forward<F>(f), nullptr, priority);
        }
        Impl::run_fiber(fiber_);
      }
    private:
      impl::fibers::Fiber fiber_;
    };

    /*ease of use of mutex*/
    using Mutex = ::dasync::impl::fibers::Mutex<Tag>;

    /*ease of use of condition_variable*/
    using Condition_variable = ::dasync::impl::fibers::Condition_variable<Tag>;

    /*stop the threads for exiting if there's no work*/
    static void prevent_closure() {
      Impl::can_close = false;
    }

    /*allow the threads to exit if there's no work*/
    static void allow_closure() {
      /*put it in a mutex to stop threads from sleeping and not being notified*/
      std::lock_guard<std::mutex> guard{ Impl::sleep_mutex };

      Impl::can_close = true;

      /*wake all the sleeping threads so they can close*/
      while (!Impl::sleeping_threads.empty()) {
        Impl::sleeping_threads.back()->sleep_condition.notify_one();
        Impl::sleeping_threads.pop_back();
      }
    }

    /*initialize the global data for a certain number of threads*/
    static void init_fibers(size_t n_threads = std::thread::hardware_concurrency()) {
      /*make a new array, set the size*/
      Impl::threads = std::make_unique<impl::fibers::Thread[]>(n_threads);
      Impl::thread_count = n_threads;
    }

    /*sets the current thread to start processing fibers with index
    thread_index

    cannot be called until init_fibers has been called*/
    static void start_thread(size_t thread_index) {
      assert(Impl::threads);
      assert(thread_index < Impl::thread_count);

      Impl::ts_thread = thread_index;

      //yield to start the running
      Impl::yield(Impl::threads[thread_index]);

      Impl::ts_thread = impl::fibers::bad_thread_index;
    }

    /*start  threads pinned to the 0th,1st,2nd,... cores
    running fibers. Start a number of threads equal to a previous
    call to init_fibers

    return 0 on success, -1 on failure*/
    static int pin_and_run_threads() {
      std::vector<std::thread> threads;
      threads.reserve(Impl::thread_count);

      std::mutex start_mutex;
      std::unique_lock<std::mutex> start_lock{ start_mutex };
      bool good_start{ false };

      for (size_t i = 0; i < Impl::thread_count; ++i) {
        /*make the thread*/
        threads.emplace_back(std::bind(impl::fibers::batch_start_thread<Tag>,
          std::ref(start_mutex), std::ref(good_start), i));

        /*try and pin it*/
        if (platform_specific::pin_thread(i, threads.back()) != 0) {
          /*couldn't pin it, wait for them all to close then return*/
          start_lock.unlock();
          impl::fibers::join_all(threads);
          return -1;
        }
      }

      /*we have had a good start*/
      good_start = true;
      /*let them run*/
      start_lock.unlock();
      /*join on them all*/
      impl::fibers::join_all(threads);
      return 0; /*success*/
    }
  };

  namespace impl {
    namespace fibers {
      /*defintion*/
      template<typename Tag>
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
        ::dasync::fibers<Tag>::start_thread(i);
      }
    }
  }
}