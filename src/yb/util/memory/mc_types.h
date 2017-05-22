//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// This module contains several datatypes that are to be used together with the MemoryContext.
// With current implementation, we focus on creating STL collection of pointers such as
//   MCVector<MCString *>
//   MCList<MCString *>
// Our SQL processes work on pointers exclusively. For example, the variable nodes that share the
// same name would all be point to the same MCString and also the same entry in our symbol table.
// Eventually, our symbol table should use unique pointer and treenode would contain the raw
// pointers of the unique pointers.
//
// Examples:
// - Memory context.
//     MemoryContext::UniPtr mem_ctx = unique_ptr<MemoryContext>(new MemoryContext());
//
// - String type.
//     MCString mc_string(memctx.get(), "abc");
//     mc_string += "xyz";
//
// - STL types.
//     MCVector<int> mc_vec(memctx.get());
//     vec.reserve(77);
//
// - SQL user-defined object.
//     class MyClass : public MCBase {
//     };
//     MyClass *mc_obj = new(memctx.get()) MyClass();
//
// - All of the above instances - mc_string, mc_vec, mc_obj - contain memory context, which
//   can be use to allocate new classes.
//     MCList<int> mc_list(mc_string.memory_context());
//--------------------------------------------------------------------------------------------------
#ifndef YB_UTIL_MEMORY_MC_TYPES_H
#define YB_UTIL_MEMORY_MC_TYPES_H

#include <list>
#include <set>
#include <map>
#include <string>
#include <type_traits>
#include <vector>

#include <boost/tti/has_type.hpp>

#include "yb/util/memory/memory_context.h"

namespace yb {

//--------------------------------------------------------------------------------------------------
// Buffer (char*) support.
char *MCStrdup(MemoryContext *memctx, const char *str);

// Class MCList.
template<class MCObject> using MCList = std::list<MCObject, MCAllocator<MCObject>>;

// Class MCVector.
template<class MCObject> using MCVector = std::vector<MCObject, MCAllocator<MCObject>>;

// Class MCSet.
template<class MCObject, class Compare = std::less<MCObject>>
using MCSet = std::set<MCObject, Compare, MCAllocator<MCObject>>;

// Class MCMap.
template<class MCKey, class MCObject, class Compare = std::less<MCKey>>
using MCMap = std::map<MCKey, MCObject, Compare, MCAllocator<MCObject>>;

//--------------------------------------------------------------------------------------------------
// String support.
// To use MCAllocator, strings should be declared as one of the following.
//   MCString s(memctx);
//   MCSharedPtr<MCString> s = MCMakeSharedString(memctx);

typedef std::basic_string<char, std::char_traits<char>, MCAllocator<char>> MCString;

//--------------------------------------------------------------------------------------------------
// User-defined object support.
// All objects that use MCAllocator should be derived from MCBase. For example:
// class MCMyObject : public MCBase {
// };

BOOST_TTI_HAS_TYPE(allocator_type);

template<class MCObject, typename... TypeArgs>
typename std::enable_if<!has_type_allocator_type<MCObject>::value, MCSharedPtr<MCObject>>::type
MCAllocateSharedHelper(MCObject*, MCAllocator<MCObject> allocator, TypeArgs&&... args) {
  return std::allocate_shared<MCObject>(allocator,
                                        allocator.memory_context(),
                                        std::forward<TypeArgs>(args)...);
}

template<class MCObject, typename... TypeArgs>
typename std::enable_if<has_type_allocator_type<MCObject>::value, MCSharedPtr<MCObject>>::type
MCAllocateSharedHelper(MCObject*, MCAllocator<MCObject> allocator, TypeArgs&&... args) {
  return std::allocate_shared<MCObject>(allocator, std::forward<TypeArgs>(args)..., allocator);
}

template<class MCObject, typename... TypeArgs>
MCSharedPtr<MCObject> MCAllocateShared(MCAllocator<MCObject> allocator, TypeArgs&&... args) {
  return MCAllocateSharedHelper(static_cast<MCObject*>(nullptr),
                                allocator,
                                std::forward<TypeArgs>(args)...);
}

// Construct a shared_ptr to any MC object.
template<class MCObject, typename... TypeArgs>
MCSharedPtr<MCObject> MCMakeShared(MemoryContext *memctx, TypeArgs&&... args) {
  MCAllocator<MCObject> allocator(memctx);
  return MCAllocateShared<MCObject>(allocator, std::forward<TypeArgs>(args)...);
}

template<class MCObject>
MCSharedPtr<MCObject> MCToShared(MemoryContext *memctx, MCObject *raw_ptr) {
  return memctx->ToShared<MCObject>(raw_ptr);
}

// MC base class.
class MCBase {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<MCBase> SharedPtr;
  typedef MCSharedPtr<const MCBase> SharedPtrConst;

  // Constructors.
  explicit MCBase(MemoryContext *memctx = nullptr);
  virtual ~MCBase();

  // Delete operator is a NO-OP. The custom allocator (e.g. Arena) will free it when the associated
  // memory context is deleted.
  void operator delete(void *ptr);

  // Operator new with placement allocate an object of any derived classes of MCBase.
  void *operator new(size_t bytes, MemoryContext *mem_ctx) throw(std::bad_alloc);

  // Allocate an array of objects of any derived classes of MCBase. Do not use this feature
  // as it is still experimental.
  void *operator new[](size_t bytes, MemoryContext *mem_ctx)
    throw(std::bad_alloc) __attribute__((deprecated));

  template<typename... TypeArgs>
  inline static MCBase::SharedPtr MakeShared(MemoryContext *memctx, TypeArgs&&... args) {
    return MCMakeShared<MCBase>(memctx, std::forward<TypeArgs>(args)...);
  }

 private:
  //------------------------------------------------------------------------------------------------
  // The following functions are deprecated and not supported for MC objects.

  // Delete[] operator is a NO-OP. The custom allocator (Arena) will free it when the associated
  // memory context is deleted.
  void operator delete[](void* ptr) __attribute__((deprecated));

  // Operator new without placement is disabled.
  void *operator new(size_t bytes) throw() __attribute__((deprecated));

  // Operator new[] without placement is disabled.
  void *operator new[](size_t bytes) throw() __attribute__((deprecated));
};

}  // namespace yb

#endif // YB_UTIL_MEMORY_MC_TYPES_H
