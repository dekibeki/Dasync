#pragma once

#include <atomic>
#include <mutex>
#include <deque>
#include <random>
#include <memory>
#include <condition_variable>
#include <boost/context/execution_context_v2.hpp>

namespace dasync {
  namespace fibers {
    namespace impl {

      /*priorities for fibers, higher priorities must have lower numerical value*/
      enum class Priority : size_t {
        high = 0,
        normal,
        low,
        count_, //special non-priority for number of priorities
      };

      /*another way of getting the number of priorities/queues*/
      constexpr size_t queue_count = static_cast<size_t>(Priority::count_);

      /*maximum number of times to try and steal from another thread*/
      constexpr size_t max_steal_tries = 10;

      /*sentinal value for a bad thread index*/
      constexpr size_t bad_thread_index = std::numeric_limits<size_t>::max();

      /*typedef for use of use of the context*/
      using Context = boost::context::execution_context<void>;

      /*typedef for use in synchronization that doesn't use the adding runnable
        fiber*/
      using Inter_thread_mutex = std::mutex;

      /*declerations*/

      /*a fiber, a job, a unit of work, etc*/
      struct Fiber;
      /*a structure specific to each thread, contains scheduling information for that thread*/
      struct Thread;
      /*synchronization primitive*/
      class Counter;

      /*typedef of a queue of fibers*/
      using Fiber_queue = std::deque<Fiber*>;
      /*typedef of a stack of threads*/
      using Thread_stack = std::vector<Thread*>;

      /*declaration of the fiber entry point*/
      template<typename F>
      static Context fiber_entry(Context&& ctx, Fiber* fiber, F&& f);

      /*get the priority of the current running fiber, returns Priority::normal if 
        no fiber is currently running */
      Priority ts_get_current_priority();

      /*yield the current thread (given by Thread&) and optionaly give a lock to unlock
        after yielding*/
      void yield(Thread&, std::unique_lock<Inter_thread_mutex>* = nullptr);

      /*get the next context to run*/
      Context schedule(Thread&);

      /*called after every context switch, bookkeeping things*/
      void post_schedule(Thread&, Context&& ctx);

      /*try and steal a fiber from a random other thread*/
      Fiber* try_steal();

      /*sets a fiber to be runnable

      cannot be called until threads and thread_count have been set
      (probably through init_fibers)*/
      void run_fiber(Fiber&);

      /*proxy to allow for accessing private member function of Counter.
        Increments the counter, shouldn't be called after someone is waiting
        on the counter*/
      void counter_increment(Counter*);
      /*proxy to allow for accessing private member function of Counter.
        Decrements the counter and checks if a fiber was waiting on it. If returns
        nullptr, no fiber was waiting, otherwise the returned fiber was waiting*/
      Fiber* counter_decrement_check(Counter*);

      /*A fiber/job/unit of work, etc...*/
      struct Fiber {

        /*default constructor, doesn't initialize context to a function,
          so this fiber can't actually be run until a good call to 
          initialize*/
        Fiber();

        /*templated constructor allows for giving it any kind of callable to run
        defaults to no counter and the same priority as the current/parent fiber.
        If not current/parent fiber, ts_get_current_priority() gives Priority::normal*/
        template<typename F>
        Fiber(F&& f, Counter* counter_ = nullptr, Priority priority_ = ts_get_current_priority()) :
          counter{ counter_ },
          priority{ priority_ },
          context{ [this,f](Context&& ctx) {
            return std::move(fiber_entry(std::move(ctx),this, f));} } {
          if (counter_) {
            counter_increment(counter_);
          }
        }

        /*don't copy/move*/
        Fiber(Fiber const&) = delete;
        Fiber(Fiber&&) = delete;

        Fiber& operator=(Fiber const&) = delete;

        /*same as constructor but allows for initializing later*/
        template<typename F>
        void initialize(F&&f, Counter* counter_ = nullptr, Priority priority_ = ts_get_current_priority()) {
          counter = counter_;
          priority = priority_;
          assert(!context);
          context = std::move(Context{ [this,f](Context&& ctx) {
            return std::move(fiber_entry(std::move(ctx),this,f));} });
        }

        /*counter that should be decremented when this fiber finishes*/
        Counter* counter;
        /*the fibers priority*/
        Priority priority;
        /*the context of this fiber (stack, etc)*/
        Context context;
      };

      /*thread specific information for the schedulers*/
      struct Thread {
        /*default constructor only, can't move/copy*/
        Thread();
        Thread(Thread const&) = delete;
        Thread(Thread&&) = delete;

        Thread& operator=(Thread const&) = delete;

        /*mutex for the fiber queue*/
        std::mutex queue_mutex;
        /*condition variable used to sleep on when no fibers in queue and 
          trying to steal work failed too many times*/
        std::condition_variable sleep_condition;

        /*the queues*/
        Fiber_queue ready_queue[static_cast<size_t>(Priority::count_)];

        /*the context of the original caller to start running this scheduler*/
        Context initial_context;

        /*lock to unlock after scheduling, used for synchronization*/
        std::unique_lock<Inter_thread_mutex>* unlocking;
        /*our current fiber*/
        Fiber* current_fiber;
        /*our next fiber, used for bookkeeping during a context switch*/
        Fiber* new_fiber;
      };

      /*synchronization primitive
      
        Set it to some value (+1 for every fiber using it), and wait until the
        counter reaches some value (0 for all fibers finished, fiber_count - 1 
        for any fiber finished, etc)*/
      class Counter {
      public:
        /*friends, allow others to use private member functions through proxy*/
        friend void ::dasync::fibers::impl::counter_increment(Counter*);
        friend Fiber* ::dasync::fibers::impl::counter_decrement_check(Counter*);

        /*default init*/
        Counter();
        /*wait until this counter hits or goes below i*/
        void wait_for(size_t i);
      private:
        /*add one more fiber using this counter*/
        void increment_();
        /*decrement the fiber and check if someone was waiting*/
        Fiber* decrement_check_();

        /*mutex for this counter*/
        Inter_thread_mutex mut_;
        /*the counter*/
        std::atomic<size_t> count_;
        /*which fiber is waiting*/
        std::atomic<Fiber*> waiting_;
        /*what value the fiber is waiting for*/
        size_t waiting_for_;
      };

      /*the mutex for the sleep stack
      
        has to be an actual OS mutex. Consider some thread that isn't running fibers
        preempts a fiber holding the sleep_mutex in a call to run_fiber, and calls
        run_fiber itself. If this mutex is a spinlock then the preempting thread will
        spin for its entire time slice as the preempted thread may be pinned to that
        core*/
      extern std::mutex sleep_mutex;
      /*the number of threads being used*/
      extern size_t thread_count;
      /*an array with pointers to every thread, used in things
        such as work stealing*/
      extern std::unique_ptr<Thread[]> threads;
      /*stack of sleeping threads, prefer recently slept threads when waking*/
      extern Thread_stack sleeping_threads;

      /*whether the threads should close if there is no work left*/
      extern std::atomic<bool> can_close;

      /*thread local storage of the index of the thread structure*/
      extern thread_local size_t ts_thread;
      /*thread local random number generator*/
      extern thread_local std::mt19937_64 ts_rng;

      /*definition of the fiber entry function*/
      template<typename F>
      static Context fiber_entry(Context&& ctx, Fiber* fiber, F&& f) {
        const size_t start_thread_index = ts_thread;

        /*assert we're running on a thread that is set up to run fibers*/
        assert(start_thread_index < thread_count);

        /*we've just context switched to here, do post switch bookkeeping*/
        post_schedule(threads[start_thread_index], std::move(ctx));

        /*call the function*/
        f();

        const size_t finish_thread_index = ts_thread;

        /*assert we've ended up on a thread that is set up to run fibers*/
        assert(finish_thread_index < thread_count);

        Fiber* waiting_on_us = nullptr;

        /*if we have a counter*/
        if (fiber->counter) {
          /*decrement and check if anyone was waiting*/
          waiting_on_us  = counter_decrement_check(fiber->counter);
        }

        /*if someone was waiting, set them to runnable*/
        if (waiting_on_us) {
          run_fiber(*waiting_on_us);
        }

        /*return where to switch to through schedule*/
        return std::move(schedule(threads[finish_thread_index]));
      }
    }
  }
}