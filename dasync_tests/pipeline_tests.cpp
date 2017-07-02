#include "stdafx.h"
#include "CppUnitTest.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

#include <string>
#include <Dasync\pipeline.h>

template<typename Base>
struct Increment_pipe :
  protected Base {
  int increment(int i) {
    return ++i;
  }
};

template<typename Base>
struct Decrement_pipe :
  protected Base {
  int decrement(int i) {
    return --i;
  }
};

template<typename Base>
struct Square_pipe :
  protected Base {
  int square(int i) {
    return i*i;
  }
};

constexpr int constructor_pipe_default = 0;

template<typename Base>
struct Constructor_pipe :
  protected Base {

  Constructor_pipe() :
    i{ constructor_pipe_default } {

  }

  Constructor_pipe(int i_) :
    i{ i_ } {

  }

  int get() {
    return i;
  }

  int i;
};

template<typename Base>
struct Double_pipe :
  protected Base {
  void recv(int& i) {
    i *= 2;
    Base::get_next().recv(i);
  }
};

template<typename Base>
struct Cap_pipe :
  protected Base {
  template<typename T>
  void recv(T&& t) {
  }
};

namespace dasync_tests
{
  TEST_CLASS(Pipeline) {
public:
  /*A pipeline with one pipe, check if get_front and get_back get the same pipe*/
  TEST_METHOD(one_pipe) {
    using Test_pipeline = dasync::Pipeline<Increment_pipe>;
    std::shared_ptr<Test_pipeline> pipeline{std::make_shared<Test_pipeline>() };

    pipeline->start();

    Assert::AreEqual(pipeline->get_front().increment(0), 1);
    Assert::AreEqual(pipeline->get_back().increment(1), 2);

  }
  /*A pipeline with two pipes, check if get_front and get_back get the correct pipes*/
  TEST_METHOD(two_pipes) {
    using Test_pipeline = dasync::Pipeline<Increment_pipe, Decrement_pipe>;
    std::shared_ptr<Test_pipeline> pipeline{ std::make_shared<Test_pipeline>() };

    pipeline->start();

    Assert::AreEqual(pipeline->get_front().increment(1), 2);
    Assert::AreEqual(pipeline->get_back().decrement(2), 1);
  }
  /*A pipeline with three pipes, check if get_front and get_back get the correct pipes*/
  TEST_METHOD(three_pipes) {
    using Test_pipeline = dasync::Pipeline<Increment_pipe, Decrement_pipe, Square_pipe>;
    std::shared_ptr<Test_pipeline> pipeline{ std::make_shared<Test_pipeline>() };

    pipeline->start();

    Assert::AreEqual(pipeline->get_front().increment(2),3);
    Assert::AreEqual(pipeline->get_back().square(3), 9);
  }
  /*check whether the constructor nicely passes the args along*/
  TEST_METHOD(constructor_both) {
    using Test_pipeline = dasync::Pipeline<Constructor_pipe, Constructor_pipe>;
    std::shared_ptr<Test_pipeline> pipeline{ std::make_shared<Test_pipeline>(1,2) };

    pipeline->start();

    Assert::AreEqual(pipeline->get_front().get(), 1);
    Assert::AreEqual(pipeline->get_back().get(), 2);
  }
  /*check whether the constructor nicely calls the second with no args*/
  TEST_METHOD(constructor_first) {
    using Test_pipeline = dasync::Pipeline<Constructor_pipe, Constructor_pipe>;
    std::shared_ptr<Test_pipeline> pipeline{ std::make_shared<Test_pipeline>(1) };

    pipeline->start();

    Assert::AreEqual(pipeline->get_front().get(), 1);
    Assert::AreEqual(pipeline->get_back().get(), constructor_pipe_default);
  }
  /*check whether the cosntrcutor nicely calls the first with no args*/
  TEST_METHOD(constructor_second) {
    using Test_pipeline = dasync::Pipeline<Constructor_pipe, Constructor_pipe>;
    std::shared_ptr<Test_pipeline> pipeline{ std::make_shared<Test_pipeline>(
      dasync::empty_pipeline_arg,2) };

    pipeline->start();

    Assert::AreEqual(pipeline->get_front().get(), constructor_pipe_default);
    Assert::AreEqual(pipeline->get_back().get(), 2);
  }
  /*pipes that call functions on the next pipe, one step*/
  TEST_METHOD(one_stage_double) {
    using Test_pipeline = dasync::Pipeline<Double_pipe, Cap_pipe>;
    std::shared_ptr<Test_pipeline> pipeline{ std::make_shared<Test_pipeline>() };

    pipeline->start();

    int i = 1;

    pipeline->get_front().recv(i);

    Assert::AreEqual(i, 2);
  }
  /*pipes that call functions on the next pipe, two steps*/
  TEST_METHOD(two_stage_double) {
    using Test_pipeline = dasync::Pipeline<Double_pipe, Double_pipe, Cap_pipe>;
    std::shared_ptr<Test_pipeline> pipeline{ std::make_shared<Test_pipeline>() };

    pipeline->start();

    int i = 1;

    pipeline->get_front().recv(i);

    Assert::AreEqual(i, 4);
  }
  };
}
