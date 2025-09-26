// Copyright 2005 Google Inc.
//
// #status: RECOMMENDED
// #category: maps
// #summary: Utility functions for use with map-like containers.
//
// This file provides utility functions for use with STL map-like data
// structures, such as std::map and hash_map. Some functions will also work with
// sets, such as ContainsKey().
//
// The main functions in this file fall into the following categories:
//
// - Find*()
// - Contains*()
// - Insert*()
// - Lookup*()
//
// These functions often have "...OrDie" or "...OrDieNoPrint" variants. These
// variants will crash the process with a CHECK() failure on error, including
// the offending key/data in the log message. The NoPrint variants will not
// include the key/data in the log output under the assumption that it's not a
// printable type.
//
// Most functions are fairly self explanatory from their names, with the
// exception of Find*() vs Lookup*(). The Find functions typically use the map's
// .find() member function to locate and return the map's value type. The
// Lookup*() functions typically use the map's .insert() (yes, insert) member
// function to insert the given value if necessary and returns (usually a
// reference to) the map's value type for the found item.
//
// See the per-function comments for specifics.
//
// There are also a handful of functions for doing other miscellaneous things.
//
// A note on terminology:
//
// Map-like containers are collections of pairs. Like all STL containers they
// contain a few standard typedefs identifying the types of data they contain.
// Given the following map declaration:
//
//   map<string, int> my_map;
//
// the notable typedefs would be as follows:
//
//   - key_type    -- string
//   - value_type  -- pair<const string, int>
//   - mapped_type -- int
//
// Note that the map above contains two types of "values": the key-value pairs
// themselves (value_type) and the values within the key-value pairs
// (mapped_type). A value_type consists of a key_type and a mapped_type.
//
// The documentation below is written for programmers thinking in terms of keys
// and the (mapped_type) values associated with a given key.  For example, the
// statement
//
//   my_map["foo"] = 3;
//
// has a key of "foo" (type: string) with a value of 3 (type: int).
//

//
// The following only applies to changes made to this file as part of YugabyteDB development.
//
// Portions Copyright (c) YugabyteDB, Inc.
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

#include <set>
#include <string>
#include <vector>

#include "yb/util/logging.h"
#include "yb/util/result.h"
#include "yb/util/status_format.h"

//
// Find*()
//

// Returns a const reference to the value associated with the given key if it
// exists. Crashes otherwise.
//
// This is intended as a replacement for operator[] as an rvalue (for reading)
// when the key is guaranteed to exist.
//
// operator[] for lookup is discouraged for several reasons:
//  * It has a side-effect of inserting missing keys
//  * It is not thread-safe (even when it is not inserting, it can still
//      choose to resize the underlying storage)
//  * It invalidates iterators (when it chooses to resize)
//  * It default constructs a value object even if it doesn't need to
//
// This version assumes the key is printable, and includes it in the fatal log
// message.
template <class Collection>
const typename Collection::value_type::second_type&
FindOrDie(const Collection& collection,
          const typename Collection::value_type::first_type& key) {
  auto it = collection.find(key);
  CHECK(it != collection.end()) << "Map key not found: " << key;
  return it->second;
}

// Same as above, but returns a non-const reference.
template <class Collection>
typename Collection::value_type::second_type&
FindOrDie(Collection& collection,  // NOLINT
          const typename Collection::value_type::first_type& key) {
  auto it = collection.find(key);
  CHECK(it != collection.end()) << "Map key not found: " << key;
  return it->second;
}

// Same as FindOrDie above, but doesn't log the key on failure.
template <class Collection>
const typename Collection::value_type::second_type&
FindOrDieNoPrint(const Collection& collection,
                 const typename Collection::value_type::first_type& key) {
  typename Collection::const_iterator it = collection.find(key);
  CHECK(it != collection.end()) << "Map key not found";
  return it->second;
}

// Same as above, but returns a non-const reference.
template <class Collection>
typename Collection::value_type::second_type&
FindOrDieNoPrint(Collection& collection,  // NOLINT
                 const typename Collection::value_type::first_type& key) {
  typename Collection::iterator it = collection.find(key);
  CHECK(it != collection.end()) << "Map key not found";
  return it->second;
}

#define FIND_OR_STATUS(collection, key) ({ \
    Result<decltype(collection)::value_type::second_type> result = \
        STATUS_FORMAT(NotFound, "Map key not found: $0", key); \
    auto collection_it = collection.find(key); \
    if (collection_it != collection.end()) \
      result = collection_it->second; \
    result;})

// Returns a const reference to the value associated with the given key if it
// exists, otherwise a const reference to the provided default value is
// returned.
//
// WARNING: If a temporary object is passed as the default "value," this
// function will return a reference to that temporary object, which will be
// destroyed by the end of the statement. Specifically, if you have a map with
// string values, and you pass a char* as the default "value," either use the
// returned value immediately or store it in a string (not string&). Details:
template <class Collection>
const typename Collection::value_type::second_type&
FindWithDefault(const Collection& collection,
                const typename Collection::value_type::first_type& key,
                const typename Collection::value_type::second_type& value) {
  auto it = collection.find(key);
  if (it == collection.end()) {
    return value;
  }
  return it->second;
}

// Returns a pointer to the const value associated with the given key if it
// exists, or NULL otherwise.
template <class Collection>
const typename Collection::value_type::second_type*
FindOrNull(const Collection& collection,
           const typename Collection::value_type::first_type& key) {
  auto it = collection.find(key);
  if (it == collection.end()) {
    return 0;
  }
  return &it->second;
}

// Same as above but returns a pointer to the non-const value.
template <class Collection>
typename Collection::value_type::second_type*
FindOrNull(Collection& collection,  // NOLINT
           const typename Collection::value_type::first_type& key) {
  auto it = collection.find(key);
  if (it == collection.end()) {
    return 0;
  }
  return &it->second;
}

// Returns a pointer to the const value associated with the greatest key
// that's less than or equal to the given key, or NULL if no such key exists.
template <class Collection>
const typename Collection::value_type::second_type*
FindFloorOrNull(const Collection& collection,
                const typename Collection::value_type::first_type& key) {
  auto it = collection.upper_bound(key);
  if (it == collection.begin()) {
    return 0;
  }
  return &(--it)->second;
}

// Same as above but returns a pointer to the non-const value.
template <class Collection>
typename Collection::value_type::second_type*
FindFloorOrNull(Collection& collection,  // NOLINT
                const typename Collection::value_type::first_type& key) {
  auto it = collection.upper_bound(key);
  if (it == collection.begin()) {
    return 0;
  }
  return &(--it)->second;
}

// Returns the pointer value associated with the given key. If none is found,
// NULL is returned. The function is designed to be used with a map of keys to
// pointers.
//
// This function does not distinguish between a missing key and a key mapped
// to a NULL value.
template <class Collection>
typename Collection::value_type::second_type
FindPtrOrNull(const Collection& collection,
              const typename Collection::value_type::first_type& key) {
  auto it = collection.find(key);
  if (it == collection.end()) {
    return typename Collection::value_type::second_type(0);
  }
  return it->second;
}

// Same as above, except takes non-const reference to collection.
//
// This function is needed for containers that propagate constness to the
// pointee, such as boost::ptr_map.
template <class Collection>
typename Collection::value_type::second_type
FindPtrOrNull(Collection& collection,  // NOLINT
              const typename Collection::value_type::first_type& key) {
  auto it = collection.find(key);
  if (it == collection.end()) {
    return typename Collection::value_type::second_type(0);
  }
  return it->second;
}

// Finds the value associated with the given key and copies it to *value (if not
// NULL). Returns false if the key was not found, true otherwise.
template <class Collection, class Key, class Value>
bool FindCopy(const Collection& collection,
              const Key& key,
              Value* const value) {
  auto it = collection.find(key);
  if (it == collection.end()) {
    return false;
  }
  if (value) {
    *value = it->second;
  }
  return true;
}

//
// Contains*()
//

// Returns true iff the given collection contains the given key.
template <class Collection, class Key>
bool ContainsKey(const Collection& collection, const Key& key) {
  auto it = collection.find(key);
  return it != collection.end();
}

// Returns true iff the given collection contains the given key-value pair.
template <class Collection, class Key, class Value>
bool ContainsKeyValuePair(const Collection& collection,
                          const Key& key,
                          const Value& value) {
  typedef typename Collection::const_iterator const_iterator;
  std::pair<const_iterator, const_iterator> range = collection.equal_range(key);
  for (const_iterator it = range.first; it != range.second; ++it) {
    if (it->second == value) {
      return true;
    }
  }
  return false;
}

//
// Insert*()
//

// Inserts the given key-value pair into the collection. Returns true if the
// given key didn't previously exist. If the given key already existed in the
// map, its value is changed to the given "value" and false is returned.
template <class Collection>
bool InsertOrUpdate(Collection* const collection,
                    const typename Collection::value_type& vt) {
  std::pair<typename Collection::iterator, bool> ret = collection->insert(vt);
  if (!ret.second) {
    // update
    ret.first->second = vt.second;
    return false;
  }
  return true;
}

// Same as above, except that the key and value are passed separately.
template <class Collection>
bool InsertOrUpdate(Collection* const collection,
                    const typename Collection::value_type::first_type& key,
                    const typename Collection::value_type::second_type& value) {
  return InsertOrUpdate(
      collection, typename Collection::value_type(key, value));
}

// Inserts/updates all the key-value pairs from the range defined by the
// iterators "first" and "last" into the given collection.
template <class Collection, class InputIterator>
void InsertOrUpdateMany(Collection* const collection,
                        InputIterator first, InputIterator last) {
  for (; first != last; ++first) {
    InsertOrUpdate(collection, *first);
  }
}

// Change the value associated with a particular key in a map or hash_map
// of the form map<Key, Value*> which owns the objects pointed to by the
// value pointers.  If there was an existing value for the key, it is deleted.
// True indicates an insert took place, false indicates an update + delete.
template <class Collection>
bool InsertAndDeleteExisting(
    Collection* const collection,
    const typename Collection::value_type::first_type& key,
    const typename Collection::value_type::second_type& value) {
  std::pair<typename Collection::iterator, bool> ret =
      collection->insert(typename Collection::value_type(key, value));
  if (!ret.second) {
    delete ret.first->second;
    ret.first->second = value;
    return false;
  }
  return true;
}

// Inserts the given key and value into the given collection iff the given key
// did NOT already exist in the collection. If the key previously existed in the
// collection, the value is not changed. Returns true if the key-value pair was
// inserted; returns false if the key was already present.
template <class Collection>
bool InsertIfNotPresent(Collection* const collection,
                        const typename Collection::value_type& vt) {
  return collection->insert(vt).second;
}

// Same as above except the key and value are passed separately.
template <class Collection>
bool InsertIfNotPresent(
    Collection* const collection,
    const typename Collection::value_type::first_type& key,
    const typename Collection::value_type::second_type& value) {
  return InsertIfNotPresent(
      collection, typename Collection::value_type(key, value));
}

// Same as above except dies if the key already exists in the collection.
template <class Collection>
void InsertOrDie(Collection* const collection,
                 const typename Collection::value_type& value) {
  CHECK(InsertIfNotPresent(collection, value)) << "duplicate value: " << value;
}

// Same as above except doesn't log the value on error.
template <class Collection>
void InsertOrDieNoPrint(Collection* const collection,
                        const typename Collection::value_type& value) {
  CHECK(InsertIfNotPresent(collection, value)) << "duplicate value.";
}

// Inserts the key-value pair into the collection. Dies if key was already
// present.
template <class Collection>
void InsertOrDie(Collection* const collection,
                 const typename Collection::value_type::first_type& key,
                 const typename Collection::value_type::second_type& data) {
  CHECK(InsertIfNotPresent(collection, key, data))
      << "duplicate key: " << key;
}

// Same as above except deson't log the key on error.
template <class Collection>
void InsertOrDieNoPrint(
    Collection* const collection,
    const typename Collection::value_type::first_type& key,
    const typename Collection::value_type::second_type& data) {
  CHECK(InsertIfNotPresent(collection, key, data)) << "duplicate key.";
}

// Inserts a new key and default-initialized value. Dies if the key was already
// present. Returns a reference to the value. Example usage:
//
// map<int, SomeProto> m;
// SomeProto& proto = InsertKeyOrDie(&m, 3);
// proto.set_field("foo");
template <class Collection>
typename Collection::value_type::second_type& InsertKeyOrDie(
    Collection* const collection,
    const typename Collection::value_type::first_type& key) {
  typedef typename Collection::value_type value_type;
  std::pair<typename Collection::iterator, bool> res =
      collection->insert(value_type(key, typename value_type::second_type()));
  CHECK(res.second) << "duplicate key: " << key;
  return res.first->second;
}

//
// Emplace*()
//
template <class Collection, class... Args>
bool EmplaceIfNotPresent(Collection* const collection,
                         Args&&... args) {
  return collection->emplace(std::forward<Args>(args)...).second;
}

template <class Collection, class... Args>
void EmplaceOrDie(Collection* const collection,
                  Args&&... args) {
  CHECK(EmplaceIfNotPresent(collection, std::forward<Args>(args)...))
      << "duplicate value";
}

//
// Lookup*()
//

// Looks up a given key and value pair in a collection and inserts the key-value
// pair if it's not already present. Returns a reference to the value associated
// with the key.
template <class Collection>
typename Collection::value_type::second_type&
LookupOrInsert(Collection* const collection,
               const typename Collection::value_type& vt) {
  return collection->insert(vt).first->second;
}

// Same as above except the key-value are passed separately.
template <class Collection>
typename Collection::value_type::second_type&
LookupOrInsert(Collection* const collection,
               const typename Collection::value_type::first_type& key,
               const typename Collection::value_type::second_type& value) {
  return LookupOrInsert(
      collection, typename Collection::value_type(key, value));
}

// Counts the number of equivalent elements in the given "sequence", and stores
// the results in "count_map" with element as the key and count as the value.
//
// Example:
//   vector<string> v = {"a", "b", "c", "a", "b"};
//   map<string, int> m;
//   AddTokenCounts(v, 1, &m);
//   assert(m["a"] == 2);
//   assert(m["b"] == 2);
//   assert(m["c"] == 1);
template <typename Sequence, typename Collection>
void AddTokenCounts(
    const Sequence& sequence,
    const typename Collection::value_type::second_type& increment,
    Collection* const count_map) {
  for (typename Sequence::const_iterator it = sequence.begin();
       it != sequence.end(); ++it) {
    typename Collection::value_type::second_type& value =
        LookupOrInsert(count_map, *it,
                       typename Collection::value_type::second_type());
    value += increment;
  }
}

// Helpers for LookupOrInsertNew(), needed to create a new value type when the
// type itself is a pointer, i.e., these extract the actual type from a pointer.
template <class T>
void MapUtilAssignNewDefaultInstance(T** location) {
  *location = new T();
}

template <class T, class Arg>
void MapUtilAssignNewInstance(T** location, const Arg &arg) {
  *location = new T(arg);
}

// Returns a reference to the value associated with key. If not found, a value
// is default constructed on the heap and added to the map.
//
// This function is useful for containers of the form map<Key, Value*>, where
// inserting a new key, value pair involves constructing a new heap-allocated
// Value, and storing a pointer to that in the collection.
template <class Collection>
typename Collection::value_type::second_type&
LookupOrInsertNew(Collection* const collection,
                  const typename Collection::value_type::first_type& key) {
  std::pair<typename Collection::iterator, bool> ret =
      collection->insert(
          typename Collection::value_type(key,
              static_cast<typename Collection::value_type::second_type>(NULL)));
  if (ret.second) {
    // This helper is needed to 'extract' the Value type from the type of the
    // container value, which is (Value*).
    MapUtilAssignNewDefaultInstance(&(ret.first->second));
  }
  return ret.first->second;
}

// Same as above but constructs the value using the single-argument constructor
// and the given "arg".
template <class Collection, class Arg>
typename Collection::value_type::second_type&
LookupOrInsertNew(Collection* const collection,
                  const typename Collection::value_type::first_type& key,
                  const Arg& arg) {
  std::pair<typename Collection::iterator, bool> ret =
      collection->insert(
          typename Collection::value_type(
              key,
              static_cast<typename Collection::value_type::second_type>(NULL)));
  if (ret.second) {
    // This helper is needed to 'extract' the Value type from the type of the
    // container value, which is (Value*).
    MapUtilAssignNewInstance(&(ret.first->second), arg);
  }
  return ret.first->second;
}

// Lookup of linked/shared pointers is used in two scenarios:
//
// Use LookupOrInsertSharedPtr if the container does not own the elements
// for their whole lifetime. This is typically the case when a reader allows
// parallel updates to the container. In this case a Mutex only needs to lock
// container operations, but all element operations must be performed on the
// shared pointer. Finding an element must be performed using FindPtr*() and
// cannot be done with FindLinkedPtr*() even though it compiles.

// Lookup a key in a map or hash_map whose values are shared_ptrs.  If it is
// missing, set collection[key].reset(new Value::element_type). Unlike
// LookupOrInsertNewLinkedPtr, this function returns the shared_ptr instead of
// the raw pointer. Value::element_type must be default constructable.
template <class Collection>
typename Collection::value_type::second_type&
LookupOrInsertNewSharedPtr(
    Collection* const collection,
    const typename Collection::value_type::first_type& key) {
  typedef typename Collection::value_type::second_type SharedPtr;
  typedef typename Collection::value_type::second_type::element_type Element;
  std::pair<typename Collection::iterator, bool> ret =
      collection->insert(typename Collection::value_type(key, SharedPtr()));
  if (ret.second) {
    ret.first->second.reset(new Element());
  }
  return ret.first->second;
}

// A variant of LookupOrInsertNewSharedPtr where the value is constructed using
// a single-parameter constructor.  Note: the constructor argument is computed
// even if it will not be used, so only values cheap to compute should be passed
// here.  On the other hand it does not matter how expensive the construction of
// the actual stored value is, as that only occurs if necessary.
template <class Collection, class Arg>
typename Collection::value_type::second_type&
LookupOrInsertNewSharedPtr(
    Collection* const collection,
    const typename Collection::value_type::first_type& key,
    const Arg& arg) {
  typedef typename Collection::value_type::second_type SharedPtr;
  typedef typename Collection::value_type::second_type::element_type Element;
  std::pair<typename Collection::iterator, bool> ret =
      collection->insert(typename Collection::value_type(key, SharedPtr()));
  if (ret.second) {
    ret.first->second.reset(new Element(arg));
  }
  return ret.first->second;
}

//
// Misc Utility Functions
//

// Updates the value associated with the given key. If the key was not already
// present, then the key-value pair are inserted and "previous" is unchanged. If
// the key was already present, the value is updated and "*previous" will
// contain a copy of the old value.
//
// InsertOrReturnExisting has complementary behavior that returns the
// address of an already existing value, rather than updating it.
template <class Collection>
bool UpdateReturnCopy(Collection* const collection,
                      const typename Collection::value_type::first_type& key,
                      const typename Collection::value_type::second_type& value,
                      typename Collection::value_type::second_type* previous) {
  std::pair<typename Collection::iterator, bool> ret =
      collection->insert(typename Collection::value_type(key, value));
  if (!ret.second) {
    // update
    if (previous) {
      *previous = ret.first->second;
    }
    ret.first->second = value;
    return true;
  }
  return false;
}

// Same as above except that the key and value are passed as a pair.
template <class Collection>
bool UpdateReturnCopy(Collection* const collection,
                      const typename Collection::value_type& vt,
                      typename Collection::value_type::second_type* previous) {
  std::pair<typename Collection::iterator, bool> ret =
    collection->insert(vt);
  if (!ret.second) {
    // update
    if (previous) {
      *previous = ret.first->second;
    }
    ret.first->second = vt.second;
    return true;
  }
  return false;
}

// Tries to insert the given key-value pair into the collection. Returns NULL if
// the insert succeeds. Otherwise, returns a pointer to the existing value.
//
// This complements UpdateReturnCopy in that it allows to update only after
// verifying the old value and still insert quickly without having to look up
// twice. Unlike UpdateReturnCopy this also does not come with the issue of an
// undefined previous* in case new data was inserted.
template <class Collection>
typename Collection::value_type::second_type*
InsertOrReturnExisting(Collection* const collection,
                       const typename Collection::value_type& vt) {
  std::pair<typename Collection::iterator, bool> ret = collection->insert(vt);
  if (ret.second) {
    return NULL;  // Inserted, no existing previous value.
  } else {
    return &ret.first->second;  // Return address of already existing value.
  }
}

// Same as above, except for explicit key and data.
template <class Collection>
typename Collection::value_type::second_type*
InsertOrReturnExisting(
    Collection* const collection,
    const typename Collection::value_type::first_type& key,
    const typename Collection::value_type::second_type& data) {
  return InsertOrReturnExisting(collection,
                                typename Collection::value_type(key, data));
}

// Saves the reverse mapping into reverse. Key/value pairs are inserted in the
// order the iterator returns them.
template <class Collection, class ReverseCollection>
void ReverseMap(const Collection& collection,
                ReverseCollection* const reverse) {
  CHECK(reverse != NULL);
  for (typename Collection::const_iterator it = collection.begin();
       it != collection.end();
       ++it) {
    InsertOrUpdate(reverse, it->second, it->first);
  }
}

// Erases the collection item identified by the given key, and returns the value
// associated with that key. It is assumed that the value (i.e., the
// mapped_type) is a pointer. Returns NULL if the key was not found in the
// collection.
//
// Examples:
//   map<string, MyType*> my_map;
//
// One line cleanup:
//     delete EraseKeyReturnValuePtr(&my_map, "abc");
//
// Use returned value:
//     std::unique_ptr<MyType> value_ptr(EraseKeyReturnValuePtr(&my_map, "abc"));
//     if (value_ptr.get())
//       value_ptr->DoSomething();
//
template <class Collection>
typename Collection::value_type::second_type EraseKeyReturnValuePtr(
    Collection* const collection,
    const typename Collection::value_type::first_type& key) {
  auto it = collection->find(key);
  if (it == collection->end()) {
    return NULL;
  }
  typename Collection::value_type::second_type v = it->second;
  collection->erase(it);
  return v;
}

// Inserts all the keys from map_container into key_container, which must
// support insert(MapContainer::key_type).
//
// Note: any initial contents of the key_container are not cleared.
template <class MapContainer, class KeyContainer>
void InsertKeysFromMap(const MapContainer& map_container,
                       KeyContainer* key_container) {
  CHECK(key_container != NULL);
  for (typename MapContainer::const_iterator it = map_container.begin();
       it != map_container.end(); ++it) {
    key_container->insert(it->first);
  }
}

// Appends all the keys from map_container into key_container, which must
// support push_back(MapContainer::key_type).
//
// Note: any initial contents of the key_container are not cleared.
template <class MapContainer, class KeyContainer>
void AppendKeysFromMap(const MapContainer& map_container,
                       KeyContainer* key_container) {
  CHECK(key_container != NULL);
  for (typename MapContainer::const_iterator it = map_container.begin();
       it != map_container.end(); ++it) {
    key_container->push_back(it->first);
  }
}

// A more specialized overload of AppendKeysFromMap to optimize reallocations
// for the common case in which we're appending keys to a vector and hence can
// (and sometimes should) call reserve() first.
//
// (It would be possible to play SFINAE games to call reserve() for any
// container that supports it, but this seems to get us 99% of what we need
// without the complexity of a SFINAE-based solution.)
template <class MapContainer, class KeyType>
void AppendKeysFromMap(const MapContainer& map_container,
                       std::vector<KeyType>* key_container) {
  CHECK(key_container != NULL);
  // We now have the opportunity to call reserve(). Calling reserve() every
  // time is a bad idea for some use cases: libstdc++'s implementation of
  // vector<>::reserve() resizes the vector's backing store to exactly the
  // given size (unless it's already at least that big). Because of this,
  // the use case that involves appending a lot of small maps (total size
  // N) one by one to a vector would be O(N^2). But never calling reserve()
  // loses the opportunity to improve the use case of adding from a large
  // map to an empty vector (this improves performance by up to 33%). A
  // number of heuristics are possible; see the discussion in
  // cl/34081696. Here we use the simplest one.
  if (key_container->empty()) {
    key_container->reserve(map_container.size());
  }
  for (typename MapContainer::const_iterator it = map_container.begin();
       it != map_container.end(); ++it) {
    key_container->push_back(it->first);
  }
}

// Inserts all the values from map_container into value_container, which must
// support push_back(MapContainer::mapped_type).
//
// Note: any initial contents of the value_container are not cleared.
template <class MapContainer, class ValueContainer>
void AppendValuesFromMap(const MapContainer& map_container,
                         ValueContainer* value_container) {
  CHECK(value_container != NULL);
  for (typename MapContainer::const_iterator it = map_container.begin();
       it != map_container.end(); ++it) {
    value_container->push_back(it->second);
  }
}

// A more specialized overload of AppendValuesFromMap to optimize reallocations
// for the common case in which we're appending values to a vector and hence
// can (and sometimes should) call reserve() first.
//
// (It would be possible to play SFINAE games to call reserve() for any
// container that supports it, but this seems to get us 99% of what we need
// without the complexity of a SFINAE-based solution.)
template <class MapContainer, class ValueType>
void AppendValuesFromMap(const MapContainer& map_container,
                         std::vector<ValueType>* value_container) {
  CHECK(value_container != NULL);
  // See AppendKeysFromMap for why this is done.
  if (value_container->empty()) {
    value_container->reserve(map_container.size());
  }
  for (const auto& entry : map_container) {
    value_container->push_back(entry.second);
  }
}

// Appends elements to the end of the container.
template <class FromCollection, class ToType>
void AppendValues(const FromCollection& from, std::vector<ToType>* const to) {
  CHECK(to != NULL);
  to->insert(to->end(), from.begin(), from.end());
}

// Inserts elements into the container, if the container doesn't already contain
// an element with an equivalent key.
template <class FromCollection, class ToType>
void AppendValues(const FromCollection& from, std::set<ToType>* const to) {
  CHECK(to != NULL);
  to->insert(from.begin(), from.end());
}
