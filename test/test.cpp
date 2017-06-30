#define BOOST_EXECUTION_CONTEXT 2

#include <stdio.h>

#include <Dasync\fibers.h>

using fibers = dasync::fibers<>;

struct Assert {
  template<typename T>
  static void AreEqual(T&& t1, T&& t2) {
    assert(t1 == t2);
  }
};

int main()
{
  fibers::allow_closure();

  std::atomic<size_t> counter{ 0 };

  fibers::Fiber f[4096];

  fibers::init_fibers();

  for (size_t i = 0; i < 4096; ++i) {
    f[i].initialize([&counter]() {for (size_t i = 0; i < 1000;++i)++counter;});
  }

  Assert::AreEqual(fibers::pin_and_run_threads(), 0);

  printf("%zd", counter.load());
}