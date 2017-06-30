#pragma once

#include <thread>
#include <Dasync/fibers_impl.h>

namespace dasync {
  namespace fibers {

    /*allow users to easily use Priority*/
    using Priority = impl::Priority;

    /*allow users to easily use Counter*/
    using Counter = impl::Counter;

    /*allow users to easily use Fiber*/
    using Fiber = impl::Fiber;

    /*stop the threads for exiting if there's no work*/
    void prevent_closure();

    /*allow the threads to exit if there's no work*/
    void allow_closure();

    /*initialize the global data for a certain number of threads*/
    void init_fibers(size_t n_threads = std::thread::hardware_concurrency());

    /*allow users to easily use run_fiber for single fiber*/
    using impl::run_fiber;

    /*sets a fiber(s) to be runnable

    cannot be called until init_fibers has been called*/
    void run_fibers(Fiber*, size_t n);

    /*sets an array of fibers to be runnable

    cannot be called until init_fibers has been called*/
    template<size_t n>
    void run_fibers(Fiber(&fibers)[n]) {
      run_fibers(fibers, n);
    }

    /*sets the current thread to start processing fibers with index
    thread_index

    cannot be called until init_fibers has been called*/
    void start_thread(size_t thread_index);

    /*start  threads pinned to the 0th,1st,2nd,... cores
    running fibers. Start a number of threads equal to a previous
    call to init_fibers

    return 0 on success, -1 on failure*/
    int pin_and_run_threads();
  }
}