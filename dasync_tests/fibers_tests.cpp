#include "stdafx.h"
#include "CppUnitTest.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

#include <string>
#include <Dasync\fibers.h>

using namespace dasync::fibers;

namespace dasync_tests
{
  TEST_CLASS(Fibers) {
public:

  /*basic just try and start_thread with no fibers*/
  TEST_METHOD(No_tasks) {
    init_fibers(1);
    allow_closure();

    start_thread(0);
  }

  /*start_thread with single fiber*/
  TEST_METHOD(Single_task) {
    std::string v;

    Fiber f{ [&v]() {
      v.push_back('1');} };

    init_fibers(1);
    allow_closure();

    run_fiber(f);

    start_thread(0);

    Assert::AreEqual(v.c_str(), "1");
  }

  /*start_thread with 2 fibers*/
  TEST_METHOD(Two_tasks) {
    std::string v;

    Fiber f[]{
      [&v]() {
        v.push_back('1');} ,
      [&v]() {
        v.push_back('2');}
    };

    init_fibers(1);
    allow_closure();

    run_fibers(f);

    start_thread(0);

    Assert::AreEqual(v.c_str(), "12");
  }

  /*first fiber creates second fiber*/
  TEST_METHOD(Recursive_tasks) {
    std::string v;

    v.reserve(3);

    Fiber child{ [&v]() {
      v.push_back('3');} };

    Fiber parent{ [&v, &child]() {
      v.push_back('1');
      run_fiber(child);
      v.push_back('2');} };

    init_fibers(1);
    allow_closure();

    run_fiber(parent);

    start_thread(0);

    Assert::AreEqual(v.c_str(), "123");
  }

  /*wait on a counter that's ready*/
  TEST_METHOD(Counter_no_wait) {
    Fiber f{ []() {
      Counter counter;
      counter.wait_for(0);} };

    init_fibers(1);
    allow_closure();

    run_fiber(f);

    start_thread(0);
  }

  /*fiber creates a child fiber then waits on it using a counter*/
  TEST_METHOD(Recursive_tasks_counter) {
    std::string v;

    v.reserve(3);

    Fiber parent{ [&v]() {
      v.push_back('1');
      Counter counter;
      Fiber child{ [&v]() {
        v.push_back('2');},&counter };
      run_fiber(child);
      counter.wait_for(0);
      v.push_back('3');} };

    init_fibers(1);
    allow_closure();

    run_fiber(parent);

    start_thread(0);

    Assert::AreEqual(v.c_str(), "123");
  }

  /*create 1 fiber through pin_and_run_threads, no fibers*/
  TEST_METHOD(one_thread_no_fibers) {
    allow_closure();

    init_fibers(1);

    Assert::AreEqual(pin_and_run_threads(), 0);
  }

  /*create 2 fibers through pin_and_run_threads, no fibers*/
  TEST_METHOD(two_threads_no_fibers) {
    allow_closure();

    init_fibers(2);

    Assert::AreEqual(pin_and_run_threads(), 0);
  }

  /*create 1 thread through pin_and_run_threads, 1 fiber*/
  TEST_METHOD(one_thread_one_fiber) {
    std::string v;
    v.reserve(1);

    allow_closure();

    init_fibers(1);

    Fiber f1{ [&v]() {
      v.push_back('1');} };

    run_fiber(f1);

    Assert::AreEqual(pin_and_run_threads(), 0);

    Assert::AreEqual(v.c_str(), "1");
  }

  /*create 2 threads through pin_and_run_threads, 1 fiber*/
  TEST_METHOD(two_threads_one_fiber) {
    std::string v;
    v.reserve(1);

    allow_closure();

    init_fibers(2);

    Fiber f1{ [&v]() {
      v.push_back('1');} };

    run_fiber(f1);

    Assert::AreEqual(pin_and_run_threads(), 0);

    Assert::AreEqual(v.c_str(), "1");
  }

  /*one pin_and_run thread with lots of fibers*/
  TEST_METHOD(one_threads_1024_fibers) {
    std::atomic<size_t> counter{ 0 };

    Fiber fibers[1024];

    for (size_t i = 0; i < sizeof(fibers) / sizeof(*fibers);++i) {
      fibers[i].initialize([&counter]() {++counter;});
    }

    allow_closure();
    init_fibers(1);
    run_fibers(fibers);
    Assert::AreEqual(pin_and_run_threads(), 0);
    Assert::AreEqual(counter.load(), sizeof(fibers) / sizeof(*fibers));
  }

  /*two pin_and_run thread with lots of fibers*/
  TEST_METHOD(two_threads_1024_fibers) {
    std::atomic<size_t> counter{ 0 };

    Fiber fibers[1024];

    for (size_t i = 0; i < sizeof(fibers) / sizeof(*fibers);++i) {
      fibers[i].initialize([&counter]() {++counter;});
    }

    allow_closure();
    init_fibers(2);
    run_fibers(fibers);
    Assert::AreEqual(pin_and_run_threads(), 0);
    Assert::AreEqual(counter.load(), sizeof(fibers) / sizeof(*fibers));
  }

  /*default pin_and_run thread with lots of fibers*/
  TEST_METHOD(default_threads_1024_fibers) {
    std::atomic<size_t> counter{ 0 };

    Fiber fibers[1024];

    for (size_t i = 0; i < sizeof(fibers) / sizeof(*fibers);++i) {
      fibers[i].initialize([&counter]() {++counter;});
    }

    allow_closure();
    init_fibers();
    run_fibers(fibers);
    Assert::AreEqual(pin_and_run_threads(), 0);
    Assert::AreEqual(counter.load(), sizeof(fibers) / sizeof(*fibers));
  }

  /*create 65 threads through pin_and_run_threads, this should fail as
    not enough cores to pin to*/
  TEST_METHOD(sixtyfive_threads_no_fibers) {
    allow_closure();

    init_fibers(65);

    Assert::AreEqual(pin_and_run_threads(), -1);
  }
  };
}