#define BOOST_EXECUTION_CONTEXT 2

#include <stdio.h>

#include <Dasync\pipeline.h>

template<typename Base>
struct Test_pipe :
  protected Base{

  void print(const char* s) {
    printf("%s", s);
  }
};

template<typename Base>
struct Test_pipe2 :
  protected Base {

  void pipe2_stuff() {

  }
};

struct Assert {
  template<typename T>
  static void AreEqual(T&& t1, T&& t2) {
    assert(t1 == t2);
  }
};

int main()
{
  using Test_pipeline = dasync::Pipeline<Test_pipe,Test_pipe2>;

  constexpr size_t pipeline_size = sizeof(Test_pipeline);

  auto pipeline = std::make_shared<Test_pipeline>();

  pipeline->start();

  pipeline->get_front().print("print front\n");

  pipeline->get_back().pipe2_stuff();

  pipeline->close();
}