#include "fibers.h"

  
void dasync::impl::fibers::join_all(std::vector<std::thread>& threads) {
  auto end_iter = threads.end();

  for (auto iter = threads.begin(); iter != end_iter; ++iter) {
    if (iter->joinable()) {
      iter->join();
    }
  }
}