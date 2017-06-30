#include <cassert>

#include "pipeline.h"

dasync::Pipeline_base::Pipeline_base() :
  state_{ Pipeline_state::initialized } {
  /*start as initialized*/
}

int dasync::Pipeline_base::start_close() {
  Pipeline_state expected_state = Pipeline_state::running;

  /*were we running, so we can close?*/
  if (!std::atomic_compare_exchange_strong(&state_, &expected_state, Pipeline_state::closing)) {
    /*we we weren't running, we must be either closing or closed (someone got to it before us)*/
    assert(expected_state == Pipeline_state::closing || expected_state == Pipeline_state::closed);

    return -1;
  } else {
    /*we were running, now we're closing, say we can close*/
    return 0;
  }
}

void dasync::Pipeline_base::finish_close() {
  /*we must be closing to finish closing*/
  assert(state_ == Pipeline_state::closing);
  /*we are closed*/
  state_ = Pipeline_state::closed;
}

int dasync::Pipeline_base::start_start() {
  Pipeline_state expected_state = Pipeline_state::initialized;

  /*we were initialized, so we can start?*/
  if (!std::atomic_compare_exchange_strong(&state_, &expected_state, Pipeline_state::starting)) {
    /*someone has already started it*/
    return -1;
  } else {
    /*we were initialized, now we're starting, say we can start*/
    return 0;
  }
}

void dasync::Pipeline_base::finish_start() {
  /*we must be starting to finish starting*/
  assert(state_ == Pipeline_state::starting);
  /*we are running*/
  state_ = Pipeline_state::running;
}

dasync::Pipeline_state dasync::Pipeline_base::state() const {
  /*get the state*/
  return state_;
}

dasync::Pipeline_base::~Pipeline_base() {
  /*do nothing, we just need this so we can delete the pipeline_base safely*/
}

/*definition*/
const dasync::Empty_pipeline_arg dasync::empty_pipeline_arg;
