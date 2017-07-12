#include "fibers_impl.h"

#include "platform_specific.h"

namespace {
  /*so hashes can be used in seeding the rngs*/
  template<typename T>
  size_t ez_hash(T const& t) {
    /*make a hasher, hash it, return it*/
    std::hash<T> hasher;

    return hasher(t);
  }
}

dasync::impl::fibers::Job::Job() :
  counter{ nullptr },
  priority{ Priority::normal },
  context{},
  start_function{} {

}

/*thread constructor, just set everything to defaults*/
dasync::impl::fibers::Thread::Thread() :
  queue_mutex{},
  ready_queue{},
  sleep_condition{} {
}

/*initialize everything to defaults*/
dasync::impl::fibers::Counter::Counter() :
  count{ 0 },
  waiting{ nullptr },
  waiting_for{ 0 } {

}

dasync::impl::fibers::Globals::Globals() :
  sleep_mutex{},
  sleeping{},
  thread_count{ 0 },
  threads{},
  idle_mutex{},
  idling{},
  can_close{ false } {

}

dasync::impl::fibers::Ts_globals::Ts_globals() :
  queue_lock{ },
  thread{ nullptr },
  rng{ std::chrono::steady_clock::now().time_since_epoch().count() ^ ez_hash(std::this_thread::get_id()) },
  initial_context{},
  prev_context{ nullptr },
  cur_job{ nullptr },
  locks{} {

}

dasync::impl::fibers::Job* dasync::impl::fibers::try_steal(Globals& globals, Ts_globals& ts) {
  /*we can't steal with only 1 thread*/
  const size_t thread_count = globals.thread_count;
  assert(thread_count > 1);

  const size_t my_index = ts.thread - globals.threads.get();

  /*we can only steal while on a thread running fibers*/
  assert(my_index < thread_count);

  std::uniform_int_distribution<size_t> distribution{ 0,thread_count - 2 };

  /*generate someone to steal from*/
  size_t stealing_from = distribution(ts.rng);

  if (stealing_from >= my_index) {
    ++stealing_from;
  }

  Job* to{ nullptr };

  std::unique_lock<std::mutex> lock{ globals.threads[stealing_from].queue_mutex };

  /*check the fibers queues*/
  for (size_t i = 0; i < queue_count; ++i) {
    if (!globals.threads[stealing_from].ready_queue[i].empty()) {
      to = globals.threads[stealing_from].ready_queue[i].back();
      globals.threads[stealing_from].ready_queue[i].pop_back();

      /*we found something*/
      return to;
    }
  }

  /*we did not find anything*/
  return to;
}

dasync::impl::fibers::Job* dasync::impl::fibers::try_queue(Ts_globals& ts) {

  Job* returning{ nullptr };

  std::unique_lock<std::mutex> local_lock;
  /*if we aren't already locked, we need to lock*/
  if (!ts.queue_lock.owns_lock()) {
    local_lock = std::move(std::unique_lock<std::mutex>{ts.thread->queue_mutex});
  }
  /*if we have work in our queue, get it*/
  for (size_t i = 0; i < sizeof(ts.thread->ready_queue) / sizeof(*ts.thread->ready_queue);++i) {
    if (!ts.thread->ready_queue[i].empty()) {
      returning = ts.thread->ready_queue[i].front();
      ts.thread->ready_queue[i].pop_front();

      return returning;
    }
  }
  return returning;
}

dasync::impl::fibers::Job* dasync::impl::fibers::get_new_job(Globals& globals, Ts_globals& ts) {
  /*valid thread*/
  assert(ts.thread);
  /*first try the queue*/
  Job* to{ try_queue(ts) };

  if (to) {
    return to;
  }

  /*otherwise try and steal*/

  /*if we have threads to steal from*/
  if (globals.thread_count > 1) {
    /*try to steal max_steal_tries times*/
    for (size_t i = 0; i < max_steal_tries; ++i) {
      if ((to = try_steal(globals, ts)) != nullptr) {
        return to;
      } else {
        std::this_thread::yield();
      }
    }
  }

  /*to will be nullptr*/
  return to;
}

void dasync::impl::fibers::post_switch(Ts_globals& ts, Context&& ctx) {
  if (ts.prev_context) {
    *ts.prev_context = std::move(ctx);
  }

  /*if we have something to unlock*/
  while (!ts.locks.empty()) {
    /*unlock it*/
    ts.locks.back()->unlock();
    ts.locks.pop_back();
  }

  /*we the queue lock is locked, unlock it*/
  if (ts.queue_lock) {
    ts.queue_lock.unlock();
  }
}

void dasync::impl::fibers::job_runnable(Globals& globals, Ts_globals& ts, Job& f) {
  assert(globals.threads);

  Thread* target_thread{ nullptr };
  bool target_was_sleeping{ false };

  { /*are there any sleeping threads*/
    std::lock_guard<std::mutex> guard{ globals.sleep_mutex };

    if (!globals.sleeping.empty()) {
      /*if so get one*/
      target_thread = globals.sleeping.back();
      globals.sleeping.pop_back();
      target_was_sleeping = true;
    }
  }

  /*if there were no sleeping threads*/
  if (!target_thread) {
    /*can we use ourselves?*/
    if (ts.thread) {
      target_thread = ts.thread;
    } else {
      std::uniform_int_distribution<size_t> dist{ 0,globals.thread_count - 1 };
      target_thread = &globals.threads[dist(ts.rng)];
    }
  }

  /*the thread we're tryign to queue it on exists*/
  assert(target_thread);

  { /*put the task in its respective queue*/
    std::lock_guard<std::mutex> guard{ target_thread->queue_mutex };
    target_thread->ready_queue[static_cast<size_t>(f.priority)].push_back(&f);
  }

  /*if the thread was sleeping, wake it*/
  if (target_was_sleeping) {
    target_thread->sleep_condition.notify_one();
  }
}

dasync::impl::fibers::Job* dasync::impl::fibers::counter_decrement_check(Counter& me) {

  Job* returning{ nullptr };

  std::unique_lock<Inter_thread_mutex> lock{ me.mut };

  --me.count;

  if (me.waiting && me.waiting_for == me.count) {
    returning = me.waiting;
    me.waiting = nullptr;
  }
  return returning;
}

void dasync::impl::fibers::init_globals(Globals& globals, size_t n_threads) {
  /*make a new array, set the size*/
  globals.threads = std::make_unique<impl::fibers::Thread[]>(n_threads);
  globals.thread_count = n_threads;
}