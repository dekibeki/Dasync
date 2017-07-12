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
      struct Job;
      struct Globals;
      struct Ts_globals;
      template<typename Tag>
      struct Global_holder;

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

      /*typedef for use of use of the context*/
      using Context = boost::context::execution_context<void>;

      /*typedef for use in synchronization that doesn't use the adding runnable
      fiber*/
      using Inter_thread_mutex = std::mutex;

      /*typedef of a queue of fibers*/
      using Job_queue = std::deque<Job*>;
      /*typedef of a stack of threads*/
      using Thread_stack = std::vector<Thread*>;
      /*typedef a stack of spare contexts*/
      using Context_stack = std::vector<Context>;
      /*typedef a stack of unique_locks*/
      using Lock_stack = std::vector<std::unique_lock<std::mutex>*>;

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
        size_t count;
        /*which fiber is waiting*/
        Job* waiting;
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

        /*mutex for the job queue*/
        std::mutex queue_mutex;

        /*the queues*/
        Job_queue ready_queue[static_cast<size_t>(Priority::count_)];

        /*condition variable used to sleep on when no fibers in queue and
        trying to steal work failed too many times*/
        std::condition_variable sleep_condition;
      };

      /*A job/unit of work, etc...*/
      struct Job {

        /*default constructor, doesn't initialize context to a function,
        so this fiber can't actually be run until a good call to
        initialize*/
        Job();

        /*don't copy,
          we could move, but the imagined use case doesn't use it, so we remove
          the possibility for simplicity*/
        Job(Job const&) = delete;
        Job(Job&&) = delete;

        Job& operator=(Job const&) = delete;
        Job& operator=(Job&&) = delete;

        /*counter that should be decremented when this fiber finishes*/
        Counter* counter;
        /*the fibers priority*/
        Priority priority;
        /*the context of this fiber (stack, etc)*/
        Context context;
        /*start function*/
        std::function<void()> start_function;
      };

      /*structure to hold all the global state*/
      struct Globals {
        /*constructor to initialize global state*/
        Globals();
        /*the mutex for the sleep stack. Has to be an actual OS mutex. Consider some
        thread that isn't running fibers preempts a fiber holding the sleep_mutex in
        a call to run_fiber, and calls run_fiber itself. If this mutex is a spinlock
        then the preempting thread will spin for its entire time slice as the preempted
        thread may be pinned to that core*/
        std::mutex sleep_mutex;
        /*stack of sleeping threads, prefer recently slept threads when waking*/
        Thread_stack sleeping;

        /*the number of threads being used*/
        size_t thread_count;
        /*an array with pointers to every thread, used in things
        such as work stealing*/
        std::unique_ptr<Thread[]> threads;

        /*has to be OS mutex for same reasons as sleep*/
        std::mutex idle_mutex;
        /*The stack of spare contexts*/
        Context_stack idling;

        /*whether the threads should close if there is no work left*/
        std::atomic<bool> can_close;
      };

      /*A structure to hold all the thread_local global state*/
      struct Ts_globals {
        /*constructor to initialize thread specific state*/
        Ts_globals();
        /*lock for the job queue mutex*/
        std::unique_lock<std::mutex> queue_lock;
        /*which thread are we*/
        Thread* thread;
        /*thread local random number generator*/
        std::mt19937_64 rng;
        /*the context of the original caller to start running this scheduler,
        also acts as the idle/sleep task*/
        Context initial_context;
        /*Where to place the previous context post switch*/
        Context* prev_context;

        /*current job being run*/
        Job* cur_job;

        /*locks to unlock after scheduling, used for synchronization*/
        Lock_stack locks;
      };

      /*put all globals in a tagged struct as statics to allow for multiple
        independant copies of the globals (if you want 2 schedulers for example*/
      template<typename Tag>
      struct Global_holder {
        static Globals globals;
        static thread_local Ts_globals ts_globals;
      };

      /*defns for static members*/
      template<typename Tag>
      Globals Global_holder<Tag>::globals;

      template<typename Tag>
      thread_local Ts_globals Global_holder<Tag>::ts_globals;

      /*try and steal a job from another random thread*/
      Job* try_steal(Globals& globals, Ts_globals& ts);

      /*check our queue to see if there's anything in there*/
      Job* try_queue(Ts_globals& ts);

      /*get the next job to run*/
      Job* get_new_job(Globals& globals, Ts_globals& ts);

      /*called after every context switch, bookkeeping things*/
      void post_switch(Ts_globals& ts, Context&& ctx);

      /*sets a fiber to be runnable
      cannot be called until threads and thread_count have been set
      (probably through init_fibers)*/
      void job_runnable(Globals& globals, Ts_globals& ts, Job& f);

      /*decrement the counter and check if any fiber was waiting on what we
      just decremented it to

      returns nullptr if no fiber was waiting, returns the fiber if one was*/
      Job* counter_decrement_check(Counter& me);

      /*set up globals*/
      void init_globals(Globals& globals, size_t n_threads);

      /*templated functions, these involve switching*/

      /*switch to a particular context:
      returns the thread_locals for the thread fiber is executing on post switch-back*/
      template<typename T>
      Ts_globals& switch_to(Globals& globals, Ts_globals& ts, Context&& ctx_to, Context* prev);

      /*wait for a counter to hit i, if it is already below, return immediately*/
      template<typename T>
      void counter_wait_for(Globals& globals, Ts_globals& ts,Counter& me, size_t i);

      /*get a context that is either idle if there some in the stack, or make one*/
      template<typename T>
      Context get_idle_context(Globals& globals);

      /*return the context for the fiber to return when we are trying to close*/
      template<typename T>
      Context&& get_close_context(Globals& globals, Ts_globals& thread);

      /*yield the current thread (given by Ts_globals&)*/
      template<typename T>
      Ts_globals& yield(Globals& globals, Ts_globals& ts);

      /*where a worker fiber starts*/
      template<typename T>
      Context fiber_entry(Context&& ctx);

      /*the entry point for a thread running fibers*/
      template<typename T>
      void start_thread(size_t i);

      /*starts running the job, and performs proper bookkeeping when it finishes*/
      template<typename T>
      Ts_globals& start_job(Globals& globals, Ts_globals& ts);

      /*other templated declerations*/

      /*initialize a job*/
      template<typename F>
      void job_initialize(Job& me, F&& f, Counter* counter = nullptr, Priority priority = Priority::normal);

      /*defns*/

      template<typename T>
      Ts_globals& switch_to(Globals& globals, Ts_globals& ts, Context&& ctx_to, Context* prev) {

        assert(ts.thread);

        ts.prev_context = prev;

        /*SWITCH HAPPENS HERE*/
        Context old_ctx = std::move(ctx_to());
        /*SWITCH HAPPENS HERE*/

        /*need to reget ts_globals as we may have been moved to a different thread*/

        Ts_globals& new_ts = T::ts_globals;

        post_switch(new_ts, std::move(old_ctx));

        return new_ts;
      }

      /*wait for a counter to hit i, if it is already below, return immediately*/
      template<typename T>
      void counter_wait_for(Globals& globals, Ts_globals& ts, Counter& me, size_t i) {
        /*we've already passed it, no need to wait*/
        std::unique_lock<Inter_thread_mutex> lock{ me.mut };

        if (me.count <= i) {
          return;
        }

        /*we're on a proper thread*/
        assert(ts.thread);

        me.waiting_for = i;
        me.waiting = ts.cur_job;

        ts.locks.emplace_back(&lock);

        schedule<T>(globals, ts);
      }

      template<typename T>
      Ts_globals& schedule(Globals& globals, Ts_globals& ts_) {
        /*try and get a job*/

        Ts_globals* ts{ &ts_ };

        Job* old_job = ts->cur_job;
        ts->cur_job = get_new_job(globals, *ts);

        /*what follows is a giant truth table for the following three conditions

          if(ts->cur_job) - we have a new job to run

          if(ts->cur_job->context) - if we have a new job to run, it already has its own
            context

          if(old_job) - the current context belongs to a job*/

        for (;;) {
          if (!old_job) {
            /*as we weren't running a job, we should have no locks*/
            assert(ts->locks.empty());
            assert(!ts->queue_lock.owns_lock());
          }

          if (ts->cur_job && !ts->cur_job->context && !old_job) {
            /*become the job's context*/
            return start_job<T>(globals, *ts);
          } else if (ts->cur_job && !ts->cur_job->context && old_job) {
            /*get an idle context, switch to it, it will start running the new job*/
            return switch_to<T>(globals, *ts, std::move(get_idle_context<T>(globals)), &old_job->context);
          } else if (ts->cur_job && ts->cur_job->context && !old_job) {
            /*this context then becomes idle when we jump to the job's context*/

            std::unique_lock<std::mutex> idle_lock{ globals.idle_mutex };
            globals.idling.emplace_back();
            ts->locks.emplace_back(&idle_lock);
            ts = &switch_to<T>(globals, *ts, std::move(ts->cur_job->context), &globals.idling.back());
            /*when we come back from being idle, if we have a cur_job, start it, otherwise return*/
            if (ts->cur_job) {
              continue;
            } else {
              return *ts;
            }
          } else if (ts->cur_job && ts->cur_job->context && old_job) {
            /*do not idle the current context, switch to the new job */
            return switch_to<T>(globals, *ts, std::move(ts->cur_job->context), &old_job->context);

          } else if (!ts->cur_job && old_job) {
            /*we didn't get a job, but this context still belongs to a job

              get an idle context, and jump into that to allow this job to sleep*/

            return switch_to<T>(globals, *ts, std::move(get_idle_context<T>(globals)), &old_job->context);

          } else if (!ts->cur_job && !old_job) {
            /*sleep this thread*/
            std::unique_lock<std::mutex> sleep_lock{ globals.sleep_mutex };

            /*try the queue again to see if someone put something on between us trying to sleep and
              checking the first time*/
            ts->cur_job = try_queue(*ts);

            if (ts->cur_job) {
              /*we found something try and run it*/
              continue;
            }

            globals.sleeping.emplace_back(ts->thread);
            ts->thread->sleep_condition.wait(sleep_lock);

            return *ts;
          } else {
            /*should never get here*/
            assert(false);
            return *ts;
          }
        }
      }

      template<typename T>
      Context get_idle_context(Globals& globals) {
        Context returning;
        std::unique_lock<std::mutex> lock{ globals.idle_mutex };
        if (globals.idling.empty()) {
          returning = std::move(Context{ fiber_entry<T> });
          return returning;
        } else {
          returning = std::move(globals.idling.back());
          globals.idling.pop_back();
          return returning;
        }
      }

      template<typename T>
      Context&& get_close_context(Globals& globals, Ts_globals& ts_) {
        Ts_globals* ts{ &ts_ };

        /*we must have switched from the initial OS given context to be needing a close context*/
        assert(ts->initial_context);
        /*we aren't running a job currently*/
        assert(!ts->cur_job);

        /*get a job to switch to*/
        while (1) {
          ts->cur_job = get_new_job(globals, *ts);

          if (ts->cur_job) {
            if (ts->cur_job->context) {
              return std::move(ts->cur_job->context);
            } else {
              ts = &start_job<T>(globals, *ts);
            }
          } else {
            std::unique_lock<std::mutex> idle_lock{ globals.idle_mutex };

            if (globals.idling.empty()) {
              return std::move(ts->initial_context);
            } else {
              Context returning = std::move(globals.idling.back());
              globals.idling.pop_back();
              return std::move(returning);
            }
          }
        }
      }

      template<typename T>
      Ts_globals& yield(Globals& globals, Ts_globals& ts) {
        /*get what we should switch to*/

        assert(ts.cur_job);

        ts.queue_lock.lock();

        ts.thread->ready_queue[static_cast<size_t>(ts.cur_job->priority)].push_back(
          ts.cur_job);

        return schedule<T>(globals, ts);
      }

      template<typename T>
      Context fiber_entry(Context&& ctx) {
        /*ease of use*/

        Globals& globals{ T::globals };
        Ts_globals* ts{ &T::ts_globals };

        post_switch(*ts, std::move(ctx));

        /*as we've just entered, if we have a cur_job, that's for us to run*/
        if (ts->cur_job) {
          ts = &start_job<T>(globals, *ts);
          assert(!ts->cur_job);
        }

        while (!globals.can_close) {
          ts = &schedule<T>(globals, *ts);
        }

        ts->prev_context = nullptr;

        return get_close_context<T>(globals, *ts);
      }

      /*the entry point for a thread running fibers*/
      template<typename T>
      void start_thread(size_t i) {
        Globals& globals{ T::globals };
        Ts_globals& ts{ T::ts_globals };

        std::thread::id initial_id{ std::this_thread::get_id() };

        ts.thread = &globals.threads[i];

        switch_to<T>(globals, ts, get_idle_context<T>(globals), &ts.initial_context);

        /*we must end up on the same thread post switch*/
        assert(initial_id == std::this_thread::get_id());

        /*this thread no longe ris running fibers*/
        ts.thread = nullptr;
      }

      template<typename T>
      Ts_globals& start_job(Globals& globals, Ts_globals& ts_) {
        
        Ts_globals* ts{ &ts_ };
        
        assert(ts->cur_job);
        
        assert(ts->locks.empty());
        assert(!ts->queue_lock.owns_lock());
        ts->cur_job->start_function();

        ts = &T::ts_globals;

        assert(ts->locks.empty());
        assert(!ts->queue_lock.owns_lock());

        if (ts->cur_job->counter) {
          Job* waiting_job = counter_decrement_check(*ts->cur_job->counter);
          if (waiting_job) {
            job_runnable(globals, *ts, *waiting_job);
          }
        }

        ts->cur_job = nullptr;

        return *ts;
      }

      template<typename F>
      void job_initialize(Job& me, F&&f, Counter* counter, Priority priority) {
        /*we haven't been initialized yet*/
        assert(!me.context);
        assert(!me.counter);
        assert(!me.start_function);

        me.counter = counter;
        if (counter) {
          ++counter->count;
        }
        me.priority = priority;
        me.start_function = std::forward<F>(f);
      }
    }
  }
}