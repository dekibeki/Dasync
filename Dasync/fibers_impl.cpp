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

dasync::fibers::impl::Priority dasync::fibers::impl::ts_get_current_priority() {
  size_t my_index = ts_thread;

  /*if the current thread is registered and has a fiber*/
  if (my_index < thread_count && threads[my_index].current_fiber) {
    /* use its priority*/
    return threads[my_index].current_fiber->priority;
  }
  /*otherwise return normal*/
  return Priority::normal;
}

void dasync::fibers::impl::counter_increment(Counter* c) {
  /*dumb proxy*/
  c->increment_();
}

dasync::fibers::impl::Fiber* dasync::fibers::impl::counter_decrement_check(Counter* c) {
  /*dumb proxy*/
  return c->decrement_check_();
}

dasync::fibers::impl::Fiber::Fiber() {

}

void dasync::fibers::impl::yield(Thread& me, std::unique_lock<Inter_thread_mutex>* lock) {
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

/*thread constructor, just set everything to defaults*/
dasync::fibers::impl::Thread::Thread() :
  initial_context{},
  unlocking{ nullptr },
  current_fiber{ nullptr },
  new_fiber{ nullptr }{
}

dasync::fibers::impl::Context dasync::fibers::impl::schedule(Thread& me) {
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

void dasync::fibers::impl::post_schedule(Thread& me, Context&& ctx) {
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

dasync::fibers::impl::Fiber* dasync::fibers::impl::try_steal() {
  
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

void dasync::fibers::impl::run_fiber(impl::Fiber& f) {
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
      target_thread = &threads[dist(impl::ts_rng)];
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

/*initialize everything to defaults*/
dasync::fibers::impl::Counter::Counter() :
  count_{ 0 },
  waiting_{ nullptr },
  waiting_for_{ 0 } {

}

void dasync::fibers::impl::Counter::wait_for(size_t i) {
  /*we've already passed it, no need to wait*/
  if (count_ <= i) {
    return;
  }

  size_t my_thread = ts_thread;

  //cannot be called from thread not running fibers
  assert(my_thread < thread_count);

  {
    std::unique_lock<Inter_thread_mutex> lock{ mut_ };
    //waiting_ is set after waiting_for_
    //when waiting_ is non-null, we know waiting_for_ is valid
    waiting_for_ = i;
    waiting_ = threads[my_thread].current_fiber;

    /*check again, maybe it was changed*/
    if (count_ <= i) { /*it was changed, just unset ourselves and return*/
      waiting_ = nullptr;
      return;
    }
    /*it wasn't changed, yield*/
    yield(threads[my_thread], &lock);
  }
}

void dasync::fibers::impl::Counter::increment_() {
  /*increment!*/
  assert(!waiting_);
  ++count_;
}

dasync::fibers::impl::Fiber* dasync::fibers::impl::Counter::decrement_check_() {
  /*decrement*/
  size_t cur_count = --count_;

  /*if there is something waiting*/
  if (waiting_) {
    /*if its waiting for what we just did*/
    if (waiting_for_ == cur_count) {
      /*get the lock*/
      std::unique_lock<Inter_thread_mutex> lock{ mut_ };
      /*check again*/
      if (waiting_) {
        Fiber* local_waiting = waiting_;
        waiting_ = nullptr;
        return local_waiting; /*local_waiting was waiting*/
      }
    }
  }
  return nullptr; /*nothing was waiting*/
}

std::mutex dasync::fibers::impl::sleep_mutex;
size_t dasync::fibers::impl::thread_count{ 0 };
std::unique_ptr<dasync::fibers::impl::Thread[]> dasync::fibers::impl::threads;
std::vector<dasync::fibers::impl::Thread*> dasync::fibers::impl::sleeping_threads;

std::atomic<bool> dasync::fibers::impl::can_close{ false };

thread_local size_t dasync::fibers::impl::ts_thread{ dasync::fibers::impl::bad_thread_index };
thread_local std::mt19937_64 dasync::fibers::impl::ts_rng{
  std::chrono::steady_clock::now().time_since_epoch().count() ^ ez_hash(std::this_thread::get_id()) };