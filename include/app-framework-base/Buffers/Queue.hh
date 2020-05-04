#ifndef APP_FRAMEWORK_BASE_INCLUDE_APP_FRAMEWORK_BASE_BUFFERS_QUEUE_HH_
#define APP_FRAMEWORK_BASE_INCLUDE_APP_FRAMEWORK_BASE_BUFFERS_QUEUE_HH_

/**
 * @file Queue class interfaces which specify templated buffer with push and pop
 * functions
 *
 * This is part of the DUNE DAQ Application Framework, copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#include "BufferI.hh"

#include <chrono>
#include <cstddef>
#include <utility>
using std::size_t;

namespace appframework {

template <class ValueType, class DurationType = std::chrono::milliseconds>
class QueueSink : virtual public Buffer<ValueType> {

public:
  template <typename U> void push(U &&t) {
    push_wait_for(std::forward<U>(t), DurationType(0));
  }

  virtual void push_wait_for(ValueType &&, const DurationType &) = 0;

  // To use the non-virtual push_wait_for which leaves its value
  // argument unchanged, make sure to add 

  // using QueueSink<ValueType,DurationType>::push_wait_for 

  // in your derived class declaration (line above assumes you're
  // using the same template parameter labels)

  void push_wait_for(const ValueType &val, const DurationType &timeout) {
    push_wait_for(ValueType(val), timeout);
  }
};

template <class ValueType, class DurationType = std::chrono::milliseconds>
class QueueSource : virtual public Buffer<ValueType> {

public:
  ValueType pop() { return pop_wait_for(DurationType(0)); }

  // To use the non-virtual pop_wait_for which leaves its value
  // argument unchanged, make sure to add 

  // using QueueSource<ValueType,DurationType>::pop_wait_for;

  // in your derived class declaration (line above assumes you're
  // using the same template parameter labels)

  virtual ValueType pop_wait_for(const DurationType &) = 0;
};

} // namespace appframework

#endif // APP_FRAMEWORK_BASE_INCLUDE_APP_FRAMEWORK_BASE_BUFFERS_QUEUE_HH_
