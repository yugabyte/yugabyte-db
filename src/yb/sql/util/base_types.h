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
#ifndef YB_SQL_UTIL_BASE_TYPES_H_
#define YB_SQL_UTIL_BASE_TYPES_H_

#include <list>
#include <string>

#include "yb/sql/util/memory_context.h"

namespace yb {
namespace sql {

//--------------------------------------------------------------------------------------------------
// Buffer (char*) support.
char *MCStrdup(MemoryContext *memctx, const char *str);

//--------------------------------------------------------------------------------------------------
// STL<mc_type> support.
// - MCStl Template is a mold for all of our STL classes.
// - All STD-compatible container should use this template. For example, MCList should be
//   template<class MCObject> using MCList = MCStl<std::list, MCObject>;
template<template<class, class> class StlType, class MCObject>
using MCStlBase = StlType<MCObject, MCAllocator<MCObject>>;

template<template<class, class> class StlType, class MCObject>
class MCStl : public MCStlBase<StlType, MCObject> {
 public:
  // Constructor for STL types.
  explicit MCStl(MemoryContext *mem_ctx)
      : MCStlBase<StlType, MCObject>(mem_ctx->GetAllocator<MCObject>()) {
    CHECK(mem_ctx) << "Memory context must be provided";
  }
};

// Class MCList
template<class MCObject> using MCList = MCStl<std::list, MCObject>;

// Class MCVector.
template<class MCObject> using MCVector = MCStl<std::vector, MCObject>;

//--------------------------------------------------------------------------------------------------
// String support.
// To use MCAllocator, strings should be declared as one of the following.
//   MCString s(memctx);
//   MCString::SharedPtr s = MCString::MakeShared(memctx);

using MCStringBase = std::basic_string<char, std::char_traits<char>, MCAllocator<char>>;

class MCString : public MCStringBase {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<MCString> SharedPtr;
  typedef MCSharedPtr<const MCString> SharedPtrConst;

  // Constructors.
  explicit MCString(MemoryContext *mem_ctx);
  MCString(MemoryContext *mem_ctx, const char *str);
  MCString(MemoryContext *mem_ctx, const char *str, size_t len);
  MCString(MemoryContext *mem_ctx, size_t len, char c);

  // Destructor.
  virtual ~MCString();

  // Construct a shared_ptr to MCString.
  template<typename... TypeArgs>
  static MCString::SharedPtr MakeShared(MemoryContext *mem_ctx, TypeArgs&&... args) {
    return mem_ctx->AllocateShared<MCString>(mem_ctx, std::forward<TypeArgs>(args)...);
  }
};

//--------------------------------------------------------------------------------------------------
// User-defined object support.
// All objects that use MCAllocator should be derived from MCBase. For example:
// class MCMyObject : public MCBase {
// };

// Construct a shared_ptr to any MC object.
template<class MCObject, typename... TypeArgs>
MCSharedPtr<MCObject> MCMakeShared(MemoryContext *memctx, TypeArgs&&... args) {
  return memctx->AllocateShared<MCObject>(memctx, std::forward<TypeArgs>(args)...);
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

}  // namespace sql
}  // namespace yb

#endif  // YB_SQL_UTIL_BASE_TYPES_H_
