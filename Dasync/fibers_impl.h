#pragma once

#include <atomic>
#include <mutex>
#include <deque>
#include <random>
#include <memory>
#include <chrono>
#include <condition_variable>
#include <boost/context/execution_context_v2.hpp>

namespace dasync {
  namespace impl {
    namespace fibers {
      struct Default_tag;

      /*declarations*/
      struct Counter;
      struct Thread;
      struct Fiber;

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

      /*typedef of a queue of fibers*/
      using Fiber_queue = std::deque<Fiber*>;
      /*typedef of a stack of threads*/
      using Thread_stack = std::vector<Thread*>;

      /*synchronization primitive

      Set it to some value (+1 for every fiber using it), and wait until the
      counter reaches some value (0 for all fibers finished, fiber_count - 1
      for any fiber finished, etc)*/
      struct Counter {
      public:
        /*default init*/
        Counter();

        /*mutex for this counter*/
        Inter_thread_mutex mut;
        /*the counter*/
        std::atomic<size_t> count;
        /*which fiber is waiting*/
        std::atomic<Fiber*> waiting;
        /*what value the fiber is waiting for*/
        size_t waiting_for;
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

      /*A fiber/job/unit of work, etc...*/
      struct Fiber {

        /*default constructor, doesn't initialize context to a function,
        so this fiber can't actually be run until a good call to
        initialize*/
        Fiber();

        /*don't copy/move*/
        Fiber(Fiber const&) = delete;
        Fiber(Fiber&&) = delete;

        Fiber& operator=(Fiber const&) = delete;

        /*counter that should be decremented when this fiber finishes*/
        Counter* counter;
        /*the fibers priority*/
        Priority priority;
        /*the context of this fiber (stack, etc)*/
        Context context;
      };

      /*so hashes can be used in seeding the rngs*/
      template<typename T>
      size_t ez_hash(T const& t) {
        /*make a hasher, hash it, return it*/
        std::hash<T> hasher;

        return hasher(t);
      }

      template<typename Tag = Default_tag>
      struct Impl {

        /*get the priority of the current running fiber, returns Priority::normal if
          no fiber is currently running */
        static Priority ts_get_current_priority() {
          size_t my_index = ts_thread;

          /*if the current thread is registered and has a fiber*/
          if (my_index < thread_count && threads[my_index].current_fiber) {
            /* use its priority*/
            return threads[my_index].current_fiber->priority;
          }
          /*otherwise return normal*/
          return Priority::normal;
        }

        /*yield the current thread (given by Thread&) and optionaly give a lock to unlock
          after yielding*/
        static void yield(Thread& me, std::unique_lock<Inter_thread_mutex>* lock = nullptr) {
          /*get what we should switch to*/
          Context switch_to = std::move(schedule(me));

          /*if we shouldn't switch, return without switching*/
          if (!switch_to) {
            return;
          }

          /*set unlocking*/
          me.unlocking = lock;

          /*do the switch*/
          Context switch_from = std::move(switch_to());

          const size_t my_index_after = ts_thread;

          assert(my_index_after < thread_count);

          /*we've now switched, call post-switch bookkeeping*/
          post_schedule(threads[my_index_after], std::move(switch_from));
        }

        /*get the next context to run*/
        static Context schedule(Thread& me) {
          Fiber* to;

          for (;;) {
            {
              /*if we have work in our queue, get it*/
              std::unique_lock<std::mutex> lock{ me.queue_mutex };
              for (size_t i = 0; i < sizeof(me.ready_queue) / sizeof(decltype(*me.ready_queue));++i) {
                if (!me.ready_queue[i].empty()) {
                  to = me.ready_queue[i].front();
                  me.ready_queue[i].pop_front();

                  lock.unlock();

                  me.new_fiber = to;
                  return std::move(to->context);
                }
              }
            }
            /*if we have threads to steal from*/
            if (thread_count > 1) {
              /*try to steal max_steal_tries times*/
              for (size_t i = 0; i < max_steal_tries; ++i) {
                if ((to = try_steal()) != nullptr) {
                  me.new_fiber = to;
                  return std::move(to->context);
                } else {
                  std::this_thread::yield();
                }
              }
            }

            {
              std::unique_lock<std::mutex> lock{ sleep_mutex };
              /*if there was no work and we can't close, sleep*/
              if (!can_close) {
                sleeping_threads.emplace_back(&me);

                me.sleep_condition.wait(lock);
              } else if (me.initial_context) { /*we can close, we have a valid initial context*/
                me.new_fiber = nullptr;
                return std::move(me.initial_context); /*return it*/
              } else {
                return Context{}; /*no initial context, so we haven't yet switched, just return*/
              }
            }
          }
        }

        /*called after every context switch, bookkeeping things*/
        static void post_schedule(Thread& me, Context&& ctx) {
          if (me.current_fiber) {
            if (ctx) {
              /*we have an old fiber, it didn't close
              set its context*/
              me.current_fiber->context = std::move(ctx);
            } else {
              //previous fiber just ended, don't do anything
            }
          } else { /*no old fiber, we must just be started, save the initial context*/
            me.initial_context = std::move(ctx);
          }

          /*if we have something to unlock*/
          if (me.unlocking) {
            /*unlock it*/
            me.unlocking->unlock();
            me.unlocking = nullptr;
          }
          /*set current fiber to our fiber*/
          me.current_fiber = me.new_fiber;
        }

        /*try and steal a fiber from a random other thread*/
        static Fiber* try_steal() {
          /*we can't steal with only 1 thread*/
          assert(thread_count > 1);

          size_t my_index = ts_thread;

          /*we can only steal while on a thread running fibers*/
          assert(my_index < thread_count);

          std::uniform_int_distribution<size_t> distribution{ 0,thread_count - 2 };

          std::mt19937_64& rng = ts_rng;

          /*generate someone to steal from*/
          size_t stealing_from = distribution(rng);

          if (stealing_from >= my_index) {
            ++stealing_from;
          }

          Fiber* to;

          std::unique_lock<std::mutex> lock{ threads[stealing_from].queue_mutex };

          /*check the fibers queues*/
          for (size_t i = 0; i < queue_count; ++i) {
            if (!threads[stealing_from].ready_queue[i].empty()) {
              to = threads[stealing_from].ready_queue[i].back();
              threads[stealing_from].ready_queue[i].pop_back();

              /*we found something*/
              return to;
            }
          }

          /*we did not find anything*/
          return nullptr;
        }

        /*sets a fiber to be runnable

        cannot be called until threads and thread_count have been set
        (probably through init_fibers)*/
        static void run_fiber(Fiber& f) {
          assert(threads);

          Thread* target_thread{ nullptr };
          bool target_was_sleeping{ false };

          { /*are there any sleeping threads*/
            std::lock_guard<std::mutex> guard{ sleep_mutex };

            if (!sleeping_threads.empty()) {
              /*if so get one*/
              target_thread = sleeping_threads.back();
              sleeping_threads.pop_back();
              target_was_sleeping = true;
            }
          }

          /*if there were no sleeping threads*/
          if (!target_thread) {
            /*can we use ourselves?*/
            size_t my_index = ts_thread;
            if (my_index < thread_count) {
              target_thread = &threads[my_index];
            } else {
              std::uniform_int_distribution<size_t> dist{ 0,thread_count - 1 };
              target_thread = &threads[dist(Impl::ts_rng)];
            }
          }

          { /*put the task in its respective queue*/
            std::lock_guard<std::mutex> guard{ target_thread->queue_mutex };
            target_thread->ready_queue[static_cast<size_t>(f.priority)].push_back(&f);
          }

          /*if the thread was sleeping, wake it*/
          if (target_was_sleeping) {
            target_thread->sleep_condition.notify_one();
          }
        }

        /*wait for a counter to hit i, if it is already below, return immediately*/
        static void counter_wait_for(Counter& me, size_t i) {
          /*we've already passed it, no need to wait*/
          if (me.count <= i) {
            return;
          }

          size_t my_thread = ts_thread;

          //cannot be called from thread not running fibers
          assert(my_thread < thread_count);

          {
            std::unique_lock<Inter_thread_mutex> lock{ me.mut };
            //waiting_ is set after waiting_for_
            //when waiting_ is non-null, we know waiting_for_ is valid
            me.waiting_for = i;
            me.waiting = threads[my_thread].current_fiber;

            /*check again, maybe it was changed*/
            if (me.count <= i) { /*it was changed, just unset ourselves and return*/
              me.waiting = nullptr;
              return;
            }
            /*it wasn't changed, yield*/
            yield(threads[my_thread], &lock);
          }
        }

        /*decrement the counter and check if any fiber was waiting on what we
          just decremented it to

          returns nullptr if no fiber was waiting, returns the fiber if one was*/
        static Fiber* counter_decrement_check(Counter& me) {
          /*decrement*/
          size_t cur_count = --me.count;

          /*if there is something waiting*/
          if (me.waiting) {
            /*if its waiting for what we just did*/
            if (me.waiting_for == cur_count) {
              /*get the lock*/
              std::unique_lock<Inter_thread_mutex> lock{ me.mut };
              /*check again*/
              Fiber* local_waiting;
              if ((local_waiting = me.waiting) != nullptr) {
                me.waiting = nullptr;
                return local_waiting; /*local_waiting was waiting*/
              }
            }
          }
          return nullptr; /*nothing was waiting*/
        }

        /*initialize a thread to use this implementation*/
        template<typename F>
        static void fiber_initialize(Fiber& me, F&&f,
          Counter* counter_ = nullptr, Priority priority_ = ts_get_current_priority()) {
          /*we haven't been initialized yet*/
          assert(!me.context);

          me.counter = counter_;
          if (counter_) {
            ++counter_->count;
          }
          me.priority = priority_;
          me.context = std::move(Context{ [&me,f](Context&& ctx) {
            return std::move(fiber_entry(std::move(ctx),std::ref(me),f));} });
        }

        /*the mutex for the sleep stack

          has to be an actual OS mutex. Consider some thread that isn't running fibers
          preempts a fiber holding the sleep_mutex in a call to run_fiber, and calls
          run_fiber itself. If this mutex is a spinlock then the preempting thread will
          spin for its entire time slice as the preempted thread may be pinned to that
          core*/
        static  std::mutex sleep_mutex;
        /*the number of threads being used*/
        static size_t thread_count;
        /*an array with pointers to every thread, used in things
          such as work stealing*/
        static std::unique_ptr<Thread[]> threads;
        /*stack of sleeping threads, prefer recently slept threads when waking*/
        static Thread_stack sleeping_threads;

        /*whether the threads should close if there is no work left*/
        static std::atomic<bool> can_close;

        /*thread local storage of the index of the thread structure*/
        static thread_local size_t ts_thread;
        /*thread local random number generator*/
        static thread_local std::mt19937_64 ts_rng;

        /*definition of the fiber entry function*/
        template<typename F>
        static Context fiber_entry(Context&& ctx, Fiber& fiber, F&& f) {
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
          if (fiber.counter) {
            /*decrement and check if anyone was waiting*/
            waiting_on_us = counter_decrement_check(*fiber.counter);
          }

          /*if someone was waiting, set them to runnable*/
          if (waiting_on_us) {
            run_fiber(*waiting_on_us);
          }

          /*return where to switch to through schedule*/
          return std::move(schedule(threads[finish_thread_index]));
        }
      };

      /*definitions*/
      template<typename Tag>
      std::mutex Impl<Tag>::sleep_mutex{};
      template<typename Tag>
      size_t Impl<Tag>::thread_count{ 0 };
      template<typename Tag>
      std::unique_ptr<Thread[]> Impl<Tag>::threads{};
      template<typename Tag>
      std::vector<Thread*> Impl<Tag>::sleeping_threads{};
      template<typename Tag>
      std::atomic<bool> Impl<Tag>::can_close{ false };

      template<typename Tag>
      thread_local size_t Impl<Tag>::ts_thread{ bad_thread_index };
      template<typename Tag>
      thread_local std::mt19937_64 Impl<Tag>::ts_rng{
        std::chrono::steady_clock::now().time_since_epoch().count() ^ ez_hash(std::this_thread::get_id()) };
    }
  }
}