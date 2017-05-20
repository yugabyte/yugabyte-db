//
// Copyright (c) YugaByte, Inc.
//

#ifndef YB_UTIL_ALLOCATION_TRACKER_H
#define YB_UTIL_ALLOCATION_TRACKER_H

#include <atomic>
#include <mutex>
#include <string>
#include <unordered_map>

namespace yb {

class AllocationTrackerBase {
 protected:
  explicit AllocationTrackerBase(std::string name) : name_(std::move(name)) {}
  ~AllocationTrackerBase();

  void DoCreated(void* object);
  void DoDestroyed(void* object);
 private:
  std::string name_;
#ifndef NDEBUG
  std::mutex mutex_;
  size_t id_ = 0;
  std::unordered_map<void*, std::pair<std::string, size_t>> objects_;
#else
  std::atomic<std::ptrdiff_t> count_ = {0};
#endif
};

// This class is created as light ASAN replacement.
// To debug memory leaks under MAC OS.
// When one know class of leaked object.
//
// Usage is following, to constructor of MyClass add:
//    AllocationTracker<MyClass>::Created(this);
// in destructor:
//    AllocationTracker<MyClass>::Destroyed(this);
template<class T>
class AllocationTracker : public AllocationTrackerBase {
 public:
  static void Created(T* object) { Instance().DoCreated(object); }
  static void Destroyed(T* object) { Instance().DoDestroyed(object); }

 private:
  AllocationTracker() : AllocationTrackerBase(typeid(T).name()) {}

  static AllocationTracker<T>& Instance() {
    static AllocationTracker<T> instance;
    return instance;
  }
};

} // namespace yb

#endif // YB_UTIL_ALLOCATION_TRACKER_H
