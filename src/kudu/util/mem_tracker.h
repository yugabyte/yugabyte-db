// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
#ifndef KUDU_UTIL_MEM_TRACKER_H
#define KUDU_UTIL_MEM_TRACKER_H

#include <boost/function.hpp>
#include <list>
#include <memory>
#include <stdint.h>
#include <string>
#include <vector>

#include "kudu/gutil/ref_counted.h"
#include "kudu/util/high_water_mark.h"
#include "kudu/util/locks.h"
#include "kudu/util/mutex.h"
#include "kudu/util/random.h"

namespace kudu {

class Status;
class MemTracker;

// A MemTracker tracks memory consumption; it contains an optional limit and is
// arranged into a tree structure such that the consumption tracked by a
// MemTracker is also tracked by its ancestors.
//
// The MemTracker hierarchy is rooted in a single static MemTracker whose limit
// is set via gflag. The root MemTracker always exists, and it is the common
// ancestor to all MemTrackers. All operations that discover MemTrackers begin
// at the root and work their way down the tree, while operations that deal
// with adjusting memory consumption begin at a particular MemTracker and work
// their way up the tree to the root. The tree structure is strictly enforced:
// all MemTrackers (except the root) must have a parent, and all children
// belonging to a parent must have unique ids.
//
// When a MemTracker begins its life, it has a strong reference to its parent
// and the parent has a weak reference to it. The strong reference remains for
// the lifetime of the MemTracker, but the weak reference can be dropped via
// UnregisterFromParent(). A MemTracker in this state may continue servicing
// memory consumption operations while allowing a new MemTracker with the same
// id to be created on the old parent.
//
// By default, memory consumption is tracked via calls to Consume()/Release(), either to
// the tracker itself or to one of its descendents. Alternatively, a consumption function
// can specified, and then the function's value is used as the consumption rather than the
// tally maintained by Consume() and Release(). A tcmalloc function is used to track process
// memory consumption, since the process memory usage may be higher than the computed
// total memory (tcmalloc does not release deallocated memory immediately).
//
// GcFunctions can be attached to a MemTracker in order to free up memory if the limit is
// reached. If LimitExceeded() is called and the limit is exceeded, it will first call the
// GcFunctions to try to free memory and recheck the limit. For example, the process
// tracker has a GcFunction that releases any unused memory still held by tcmalloc, so
// this will be called before the process limit is reported as exceeded. GcFunctions are
// called in the order they are added, so expensive functions should be added last.
//
// This class is thread-safe.
//
// NOTE: this class has been partially ported over from Impala with
// several changes, and as a result the style differs somewhat from
// the Kudu style.
//
// Changes from Impala:
// 1) Id a string vs. a TUniqueId
// 2) There is no concept of query trackers vs. pool trackers -- trackers are instead
//    associated with objects. Parent hierarchy is preserved, with the assumption that,
//    e.g., a tablet server's memtracker will have as its children the tablets' memtrackers,
//    which in turn will have memtrackers for their caches, logs, and so forth.
//
// TODO: this classes uses a lot of statics fields and methods, which
// isn't common in Kudu. It is probably wise to later move the
// 'registry' of trackers to a separate class, but it's better to
// start using the 'class' *first* and then change this functionality,
// depending on how MemTracker ends up being used in Kudu.
class MemTracker : public std::enable_shared_from_this<MemTracker> {
 public:

  // Signature for function that can be called to free some memory after limit is reached.
  typedef boost::function<void ()> GcFunction;

  ~MemTracker();

  // Removes this tracker from its parent's children. This tracker retains its
  // link to its parent. Must be called on a tracker with a parent.
  //
  // Automatically called in the MemTracker destructor, but should be called
  // explicitly when an object is destroyed if that object is also the "primary
  // owner" of a tracker (i.e. the object that originally created the tracker).
  // This orphans the tracker so that if the object is recreated, its new
  // tracker won't collide with the now orphaned tracker.
  //
  // Is thread-safe on the parent but not the child. Meaning, multiple trackers
  // that share the same parent can all UnregisterFromParent() at the same
  // time, but all UnregisterFromParent() calls on a given tracker must be
  // externally synchronized.
  void UnregisterFromParent();

  // Creates and adds the tracker to the tree so that it can be retrieved with
  // FindTracker/FindOrCreateTracker.
  //
  // byte_limit < 0 means no limit; 'id' is a used as a label for LogUsage()
  // and web UI and must be unique for the given parent. Use the two-argument
  // form if there is no parent.
  static std::shared_ptr<MemTracker> CreateTracker(
      int64_t byte_limit,
      const std::string& id,
      const std::shared_ptr<MemTracker>& parent = std::shared_ptr<MemTracker>());

  // If a tracker with the specified 'id' and 'parent' exists in the tree, sets
  // 'tracker' to reference that instance. Use the two-argument form if there
  // is no parent. Returns false if no such tracker exists.
  static bool FindTracker(
      const std::string& id,
      std::shared_ptr<MemTracker>* tracker,
      const std::shared_ptr<MemTracker>& parent = std::shared_ptr<MemTracker>());

  // If a tracker with the specified 'id' and 'parent' exists in the tree,
  // returns a shared_ptr to that instance. Otherwise, creates a new
  // MemTracker with the specified byte_limit, id, and parent. Use the two
  // argument form if there is no parent.
  static std::shared_ptr<MemTracker> FindOrCreateTracker(
      int64_t byte_limit,
      const std::string& id,
      const std::shared_ptr<MemTracker>& parent = std::shared_ptr<MemTracker>());

  // Returns a list of all the valid trackers.
  static void ListTrackers(std::vector<std::shared_ptr<MemTracker> >* trackers);

  // Gets a shared_ptr to the "root" tracker, creating it if necessary.
  static std::shared_ptr<MemTracker> GetRootTracker();

  // Updates consumption from the consumption function specified in the constructor.
  // NOTE: this method will crash if 'consumption_func_' is not set.
  void UpdateConsumption();

  // Increases consumption of this tracker and its ancestors by 'bytes'.
  void Consume(int64_t bytes);

  // Try to expand the limit (by asking the resource broker for more memory) by at least
  // 'bytes'. Returns false if not possible, true if the request succeeded. May allocate
  // more memory than was requested.
  // TODO: always returns false for now, not yet implemented.
  bool ExpandLimit(int64_t /* unused: bytes */) { return false; }

  // Increases consumption of this tracker and its ancestors by 'bytes' only if
  // they can all consume 'bytes'. If this brings any of them over, none of them
  // are updated.
  // Returns true if the try succeeded.
  bool TryConsume(int64_t bytes);

  // Decreases consumption of this tracker and its ancestors by 'bytes'.
  void Release(int64_t bytes);

  // Returns true if a valid limit of this tracker or one of its ancestors is
  // exceeded.
  bool AnyLimitExceeded();

  // If this tracker has a limit, checks the limit and attempts to free up some memory if
  // the limit is exceeded by calling any added GC functions. Returns true if the limit is
  // exceeded after calling the GC functions. Returns false if there is no limit.
  bool LimitExceeded();

  // Like LimitExceeded() but may also return true if the soft memory limit is exceeded.
  // The greater the excess, the higher the chance that it returns true.
  //
  // If the soft limit is exceeded and 'current_capacity_pct' is not NULL, the percentage
  // of the hard limit consumed is written to it.
  bool SoftLimitExceeded(double* current_capacity_pct);

  // Combines the semantics of AnyLimitExceeded() and SoftLimitExceeded().
  //
  // Note: if there's more than one soft limit defined, the probability of it being
  // exceeded in at least one tracker is much higher (as each soft limit check is an
  // independent event).
  bool AnySoftLimitExceeded(double* current_capacity_pct);

  // Returns the maximum consumption that can be made without exceeding the limit on
  // this tracker or any of its parents. Returns int64_t::max() if there are no
  // limits and a negative value if any limit is already exceeded.
  int64_t SpareCapacity() const;


  int64_t limit() const { return limit_; }
  bool has_limit() const { return limit_ >= 0; }
  const std::string& id() const { return id_; }

  // Returns the memory consumed in bytes.
  int64_t consumption() const {
    return consumption_.current_value();
  }

  // Note that if consumption_ is based on consumption_func_, this
  // will be the max value we've recorded in consumption(), not
  // necessarily the highest value consumption_func_ has ever
  // reached.
  int64_t peak_consumption() const { return consumption_.max_value(); }

  // Retrieve the parent tracker, or NULL If one is not set.
  std::shared_ptr<MemTracker> parent() const { return parent_; }

  // Add a function 'f' to be called if the limit is reached.
  // 'f' does not need to be thread-safe as long as it is added to only one MemTracker.
  // Note that 'f' must be valid for the lifetime of this MemTracker.
  void AddGcFunction(GcFunction f) {
    gc_functions_.push_back(f);
  }

  // Logs the usage of this tracker and all of its children (recursively).
  std::string LogUsage(const std::string& prefix = "") const;

  void EnableLogging(bool enable, bool log_stack) {
    enable_logging_ = enable;
    log_stack_ = log_stack;
  }

  // Returns a textual representation of the tracker that is guaranteed to be
  // globally unique.
  std::string ToString() const;

 private:
  // Function signatures for gauge-style memory trackers (where consumption is
  // periodically observed rather than explicitly tracked).
  //
  // Currently only used by the root tracker.
  typedef boost::function<uint64_t ()> ConsumptionFunction;

  // If consumption_func is not empty, uses it as the consumption value.
  // Consume()/Release() can still be called.
  // byte_limit < 0 means no limit
  // 'id' is the label for LogUsage() and web UI.
  MemTracker(ConsumptionFunction consumption_func, int64_t byte_limit,
             const std::string& id, std::shared_ptr<MemTracker> parent);

  bool CheckLimitExceeded() const {
    return limit_ >= 0 && limit_ < consumption();
  }

  // If consumption is higher than max_consumption, attempts to free memory by calling any
  // added GC functions.  Returns true if max_consumption is still exceeded. Takes
  // gc_lock. Updates metrics if initialized.
  bool GcMemory(int64_t max_consumption);

  // Called when the total release memory is larger than GC_RELEASE_SIZE.
  // TcMalloc holds onto released memory and very slowly (if ever) releases it back to
  // the OS. This is problematic since it is memory we are not constantly tracking which
  // can cause us to go way over mem limits.
  void GcTcmalloc();

  // Further initializes the tracker.
  void Init();

  // Adds tracker to child_trackers_.
  //
  // child_trackers_lock_ must be held.
  void AddChildTrackerUnlocked(MemTracker* tracker);

  // Logs the stack of the current consume/release. Used for debugging only.
  void LogUpdate(bool is_consume, int64_t bytes) const;

  static std::string LogUsage(const std::string& prefix,
      const std::list<MemTracker*>& trackers);

  // Variant of CreateTracker() that:
  // 1. Must be called with a non-NULL parent, and
  // 2. Must be called with parent->child_trackers_lock_ held.
  static std::shared_ptr<MemTracker> CreateTrackerUnlocked(
      int64_t byte_limit,
      const std::string& id,
      const std::shared_ptr<MemTracker>& parent);

  // Variant of FindTracker() that:
  // 1. Must be called with a non-NULL parent, and
  // 2. Must be called with parent->child_trackers_lock_ held.
  static bool FindTrackerUnlocked(
      const std::string& id,
      std::shared_ptr<MemTracker>* tracker,
      const std::shared_ptr<MemTracker>& parent);

  // Creates the root tracker.
  static void CreateRootTracker();

  // Size, in bytes, that is considered a large value for Release() (or Consume() with
  // a negative value). If tcmalloc is used, this can trigger it to GC.
  // A higher value will make us call into tcmalloc less often (and therefore more
  // efficient). A lower value will mean our memory overhead is lower.
  // TODO: this is a stopgap.
  static const int64_t GC_RELEASE_SIZE = 128 * 1024L * 1024L;

  simple_spinlock gc_lock_;

  int64_t limit_;
  int64_t soft_limit_;
  const std::string id_;
  const std::string descr_;
  std::shared_ptr<MemTracker> parent_;

  HighWaterMark consumption_;

  ConsumptionFunction consumption_func_;

  // this tracker plus all of its ancestors
  std::vector<MemTracker*> all_trackers_;
  // all_trackers_ with valid limits
  std::vector<MemTracker*> limit_trackers_;

  // All the child trackers of this tracker. Used for error reporting and
  // listing only (i.e. updating the consumption of a parent tracker does not
  // update that of its children).
  mutable Mutex child_trackers_lock_;
  std::list<MemTracker*> child_trackers_;

  // Iterator into parent_->child_trackers_ for this object. Stored to have O(1)
  // remove.
  std::list<MemTracker*>::iterator child_tracker_it_;

  // Functions to call after the limit is reached to free memory.
  std::vector<GcFunction> gc_functions_;

  ThreadSafeRandom rand_;

  // If true, logs to INFO every consume/release called. Used for debugging.
  bool enable_logging_;

  // If true, log the stack as well.
  bool log_stack_;
};

// An std::allocator that manipulates a MemTracker during allocation
// and deallocation.
template<typename T, typename Alloc = std::allocator<T> >
class MemTrackerAllocator : public Alloc {
 public:
  typedef typename Alloc::pointer pointer;
  typedef typename Alloc::const_pointer const_pointer;
  typedef typename Alloc::size_type size_type;

  explicit MemTrackerAllocator(std::shared_ptr<MemTracker> mem_tracker)
      : mem_tracker_(std::move(mem_tracker)) {}

  // This constructor is used for rebinding.
  template <typename U>
  MemTrackerAllocator(const MemTrackerAllocator<U>& allocator)
      : Alloc(allocator),
        mem_tracker_(allocator.mem_tracker()) {
  }

  ~MemTrackerAllocator() {
  }

  pointer allocate(size_type n, const_pointer hint = 0) {
    // Ideally we'd use TryConsume() here to enforce the tracker's limit.
    // However, that means throwing bad_alloc if the limit is exceeded, and
    // it's not clear that the rest of Kudu can handle that.
    mem_tracker_->Consume(n * sizeof(T));
    return Alloc::allocate(n, hint);
  }

  void deallocate(pointer p, size_type n) {
    Alloc::deallocate(p, n);
    mem_tracker_->Release(n * sizeof(T));
  }

  // This allows an allocator<T> to be used for a different type.
  template <class U>
  struct rebind {
    typedef MemTrackerAllocator<U, typename Alloc::template rebind<U>::other> other;
  };

  const std::shared_ptr<MemTracker>& mem_tracker() const { return mem_tracker_; }

 private:
  std::shared_ptr<MemTracker> mem_tracker_;
};

// Convenience class that adds memory consumption to a tracker when declared,
// releasing it when the end of scope is reached.
class ScopedTrackedConsumption {
 public:
  ScopedTrackedConsumption(std::shared_ptr<MemTracker> tracker,
                           int64_t to_consume)
      : tracker_(std::move(tracker)), consumption_(to_consume) {
    DCHECK(tracker_);
    tracker_->Consume(consumption_);
  }

  void Reset(int64_t new_consumption) {
    // Consume(-x) is the same as Release(x).
    tracker_->Consume(new_consumption - consumption_);
    consumption_ = new_consumption;
  }

  ~ScopedTrackedConsumption() {
    tracker_->Release(consumption_);
  }

  int64_t consumption() const { return consumption_; }

 private:
  std::shared_ptr<MemTracker> tracker_;
  int64_t consumption_;
};

} // namespace kudu

#endif // KUDU_UTIL_MEM_TRACKER_H
