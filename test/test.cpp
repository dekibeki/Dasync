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

struct Assert {
  template<typename T>
  static void AreEqual(T&& t1, T&& t2) {
    assert(t1 == t2);
  }
};

int main()
{
  dasync::Pipeline<Test_pipe> pipeline;

  pipeline.start();

  pipeline.get_front().print("print front\n");

  pipeline.get_back().print("print back\n");

  pipeline.close();
}