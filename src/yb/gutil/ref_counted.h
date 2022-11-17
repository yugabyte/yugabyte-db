// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//

#pragma once

#include <atomic>
#include <ostream>
#include <utility>

#ifndef NDEBUG
#include <string>
#endif

#include <typeinfo>

#include "yb/gutil/atomicops.h"
#include "yb/gutil/port.h"
#include "yb/gutil/threading/thread_collision_warner.h"

namespace yb {
namespace subtle {

// TODO: switch to std::atomic<int32_t>
typedef std::atomic<intptr_t> AtomicRefCount;

class RefCountedBase {
 public:
  bool HasOneRef() const { return ref_count_ == 1; }

 protected:
  RefCountedBase();
  ~RefCountedBase();

  void AddRef() const;

  // Returns true if the object should self-delete.
  bool Release() const;

#ifndef NDEBUG
  intptr_t GetRefCountForDebugging() const { return ref_count_; }
#endif

 private:
  mutable intptr_t ref_count_;
#ifndef NDEBUG
  mutable bool in_dtor_;
#endif

  DFAKE_MUTEX(add_release_);

  DISALLOW_COPY_AND_ASSIGN(RefCountedBase);
};

class RefCountedThreadSafeBase {
 public:
  bool HasOneRef() const;

 protected:
  RefCountedThreadSafeBase() = default;
  ~RefCountedThreadSafeBase();

  void AddRef() const;

  // Returns true if the object should self-delete.
  bool Release() const;

#ifndef NDEBUG
  intptr_t GetRefCountForDebugging() const {
    return ref_count_.load(std::memory_order_relaxed);
  }
#endif

 private:
  mutable AtomicRefCount ref_count_{0};
#ifndef NDEBUG
  mutable bool in_dtor_ = false;
#endif

  DISALLOW_COPY_AND_ASSIGN(RefCountedThreadSafeBase);
};

// ------------------------------------------------------------------------------------------------
// A facility for debugging where exactly reference counts are incremented and decremented.

#ifdef NDEBUG

// No-op in release mode.
#define INVOKE_REF_COUNTED_DEBUG_HOOK(event_type)

#else

extern bool g_ref_counted_debug_enabled;

// This callback is called for type names matching the regex to do the actual reporting of refcount
// increase/decrease.
// Parameters: type name, instance pointer, current refcount, refcount delta (+1 or -1).
typedef void RefCountedDebugFn(const char*, const void*, int64_t, int64_t);

// Configure logging on reference count increments/decrements.
// type_name_regex - regular expression for type names that we'll be logging for.
// debug_fn - a function to log a refcount increment/decrement event.
void InitRefCountedDebugging(const std::string& type_name_regex, RefCountedDebugFn* debug_fn);

void RefCountedDebugHook(const char* type_name,
                         const void* this_ptr,
                         int64_t current_refcount,
                         int64_t ref_delta);

#define INVOKE_REF_COUNTED_DEBUG_HOOK(ref_delta) \
    do { \
      if (subtle::g_ref_counted_debug_enabled) { \
        subtle::RefCountedDebugHook(typeid(*this).name(), this, GetRefCountForDebugging(), \
                                    ref_delta); \
      } \
    } while (0)

#endif

}  // namespace subtle

//
// A base class for reference counted classes.  Otherwise, known as a cheap
// knock-off of WebKit's RefCounted<T> class.  To use this guy just extend your
// class from it like so:
//
//   class MyFoo : public RefCounted<MyFoo> {
//    ...
//    private:
//     friend class RefCounted<MyFoo>;
//     ~MyFoo();
//   };
//
// You should always make your destructor private, to avoid any code deleting
// the object accidentally while there are references to it.ging();
template <class T>
class RefCounted : public subtle::RefCountedBase {
 public:
  RefCounted() {}

  void AddRef() const {
    INVOKE_REF_COUNTED_DEBUG_HOOK(1);
    subtle::RefCountedBase::AddRef();
  }

  void Release() const {
    INVOKE_REF_COUNTED_DEBUG_HOOK(-1);
    if (subtle::RefCountedBase::Release()) {
      delete static_cast<const T*>(this);
    }
  }

 protected:
  ~RefCounted() {}

 private:
  RefCounted(const RefCounted<T>&) = delete;
  void operator=(const RefCounted&) = delete;
};

// Forward declaration.
template <class T, typename Traits> class RefCountedThreadSafe;

// Default traits for RefCountedThreadSafe<T>.  Deletes the object when its ref
// count reaches 0.  Overload to delete it on a different thread etc.
template<typename T>
struct DefaultRefCountedThreadSafeTraits {
  static void Destruct(const T* x) {
    // Delete through RefCountedThreadSafe to make child classes only need to be
    // friend with RefCountedThreadSafe instead of this struct, which is an
    // implementation detail.
    RefCountedThreadSafe<T, DefaultRefCountedThreadSafeTraits>::DeleteInternal(x);
  }
};

//
// A thread-safe variant of RefCounted<T>
//
//   class MyFoo : public RefCountedThreadSafe<MyFoo> {
//    ...
//   };
//
// If you're using the default trait, then you should add compile time
// asserts that no one else is deleting your object.  i.e.
//    private:
//     friend class RefCountedThreadSafe<MyFoo>;
//     ~MyFoo();
template <class T, typename Traits = DefaultRefCountedThreadSafeTraits<T> >
class RefCountedThreadSafe : public subtle::RefCountedThreadSafeBase {
 public:
  RefCountedThreadSafe() {}

  void AddRef() const {
    INVOKE_REF_COUNTED_DEBUG_HOOK(1);
    subtle::RefCountedThreadSafeBase::AddRef();
  }

  void Release() const {
    INVOKE_REF_COUNTED_DEBUG_HOOK(-1);
    if (subtle::RefCountedThreadSafeBase::Release()) {
      Traits::Destruct(static_cast<const T*>(this));
    }
  }

 protected:
  ~RefCountedThreadSafe() {}

 private:
  friend struct DefaultRefCountedThreadSafeTraits<T>;
  static void DeleteInternal(const T* x) { delete x; }

  DISALLOW_COPY_AND_ASSIGN(RefCountedThreadSafe);
};

template <class T, typename Traits>
void intrusive_ptr_add_ref(RefCountedThreadSafe<T, Traits>* px) {
  px->AddRef();
}

template <class T, typename Traits>
void intrusive_ptr_release(RefCountedThreadSafe<T, Traits>* px) {
  px->Release();
}

//
// A thread-safe wrapper for some piece of data so we can place other
// things in scoped_refptrs<>.
//
template<typename T>
class RefCountedData
    : public yb::RefCountedThreadSafe< yb::RefCountedData<T> > {
 public:
  RefCountedData() : data() {}
  explicit RefCountedData(const T& in_value) : data(in_value) {}

  T data;

 private:
  friend class yb::RefCountedThreadSafe<yb::RefCountedData<T> >;
  ~RefCountedData() {}
};

}  // namespace yb

//
// A smart pointer class for reference counted objects.  Use this class instead
// of calling AddRef and Release manually on a reference counted object to
// avoid common memory leaks caused by forgetting to Release an object
// reference.  Sample usage:
//
//   class MyFoo : public RefCounted<MyFoo> {
//    ...
//   };
//
//   void some_function() {
//     scoped_refptr<MyFoo> foo = new MyFoo();
//     foo->Method(param);
//     // |foo| is released when this function returns
//   }
//
//   void some_other_function() {
//     scoped_refptr<MyFoo> foo = new MyFoo();
//     ...
//     foo = NULL;  // explicitly releases |foo|
//     ...
//     if (foo)
//       foo->Method(param);
//   }
//
// The above examples show how scoped_refptr<T> acts like a pointer to T.
// Given two scoped_refptr<T> classes, it is also possible to exchange
// references between the two objects, like so:
//
//   {
//     scoped_refptr<MyFoo> a = new MyFoo();
//     scoped_refptr<MyFoo> b;
//
//     b.swap(a);
//     // now, |b| references the MyFoo object, and |a| references NULL.
//   }
//
// To make both |a| and |b| in the above example reference the same MyFoo
// object, simply use the assignment operator:
//
//   {
//     scoped_refptr<MyFoo> a = new MyFoo();
//     scoped_refptr<MyFoo> b;
//
//     b = a;
//     // now, |a| and |b| each own a reference to the same MyFoo object.
//   }
//

#ifndef NDEBUG
void ScopedRefPtrCheck(bool);
#else
inline void ScopedRefPtrCheck(bool) {}
#endif


template <class T>
class scoped_refptr {
 public:
  typedef T element_type;

  scoped_refptr() : ptr_(NULL) {
  }

  scoped_refptr(T* p) : ptr_(p) { // NOLINT
    if (ptr_)
      ptr_->AddRef();
  }

  scoped_refptr(const scoped_refptr<T>& r) : ptr_(r.ptr_) {
    if (ptr_)
      ptr_->AddRef();
  }

  template <typename U>
  scoped_refptr(const scoped_refptr<U>& r) : ptr_(r.get()) {
    if (ptr_)
      ptr_->AddRef();
  }

  template <typename U>
  scoped_refptr(scoped_refptr<U>&& r) : ptr_(r.get()) {
    r.ptr_ = nullptr;
  }

  ~scoped_refptr() {
    if (ptr_)
      ptr_->Release();
  }

  T* get() const { return ptr_; }

  T* detach() {
    T *temp = ptr_;
    ptr_ = nullptr;
    return temp;
  }

  explicit operator bool() const { return ptr_ != nullptr; }

  bool operator!() const { return ptr_ == nullptr; }

  T* operator->() const {
    ScopedRefPtrCheck(ptr_ != nullptr);
    return ptr_;
  }

  T& operator*() const {
    ScopedRefPtrCheck(ptr_ != nullptr);
    return *ptr_;
  }

  scoped_refptr<T>& operator=(T* p) {
    // AddRef first so that self assignment should work
    if (p)
      p->AddRef();
    T* old_ptr = ptr_;
    ptr_ = p;
    if (old_ptr)
      old_ptr->Release();
    return *this;
  }

  scoped_refptr<T>& operator=(const scoped_refptr<T>& r) {
    return *this = r.ptr_;
  }

  template <typename U>
  scoped_refptr<T>& operator=(const scoped_refptr<U>& r) {
    return *this = r.get();
  }

  scoped_refptr<T>& operator=(scoped_refptr<T>&& r) {
    scoped_refptr<T>(r).swap(*this);
    return *this;
  }

  template <typename U>
  scoped_refptr<T>& operator=(scoped_refptr<U>&& r) {
    scoped_refptr<T>(r).swap(*this);
    return *this;
  }

  void swap(T** pp) {
    T* p = ptr_;
    ptr_ = *pp;
    *pp = p;
  }

  void swap(scoped_refptr<T>& r) {
    swap(&r.ptr_);
  }

  // Like std::unique_ptr::reset(), drops a reference on the currently held object
  // (if any), and adds a reference to the passed-in object (if not NULL).
  void reset(T* p = NULL) {
    *this = p;
  }

 protected:
  T* ptr_;

 private:
  template <typename U> friend class scoped_refptr;
};

// Handy utility for creating a scoped_refptr<T> out of a T* explicitly without
// having to retype all the template arguments
template <typename T>
scoped_refptr<T> make_scoped_refptr(T* t) {
  return scoped_refptr<T>(t);
}

template<class T, class... Args>
scoped_refptr<T> make_scoped_refptr(Args&&... args) {
  return scoped_refptr<T>(new T(std::forward<Args>(args)...));
}

// equal_to and hash implementations for templated scoped_refptrs suitable for
// use with STL unordered_* containers.
struct ScopedRefPtrEqualsFunctor {
  template <class T>
  bool operator()(const scoped_refptr<T>& x, const scoped_refptr<T>& y) const {
    return x.get() == y.get();
  }
};

struct ScopedRefPtrHashFunctor {
  template <class T>
  size_t operator()(const scoped_refptr<T>& p) const {
    return reinterpret_cast<size_t>(p.get());
  }
};

template<class T>
bool operator==(const scoped_refptr<T>& lhs, std::nullptr_t) {
  return !lhs;
}

template<class T>
bool operator!=(const scoped_refptr<T>& lhs, std::nullptr_t) {
  return static_cast<bool>(lhs);
}

template<class T>
bool operator==(std::nullptr_t, const scoped_refptr<T>& rhs) {
  return !rhs;
}

template<class T>
bool operator!=(std::nullptr_t, const scoped_refptr<T>& rhs) {
  return static_cast<bool>(rhs);
}

template<class T>
std::ostream& operator<<(std::ostream& out, const scoped_refptr<T>& ptr) {
  return out << ptr.get();
}

template <class T, class U>
bool operator==(const scoped_refptr<T>& lhs, const scoped_refptr<U>& rhs) {
  return lhs.get() == rhs.get();
}

template <class T, class U>
bool operator!=(const scoped_refptr<T>& lhs, const scoped_refptr<U>& rhs) {
  return lhs.get() != rhs.get();
}

#undef INVOKE_REF_COUNTED_DEBUG_HOOK
