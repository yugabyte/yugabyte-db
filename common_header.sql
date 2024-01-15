-- File that defines macros for our SQL preprocessing
#define __CONCAT__(x, y) x##y
#define __EXPANDED_EXTENSION_FUNCTION__(PREFIX, NAME) __CONCAT__(PREFIX,NAME)

#define __EXTENSION_OBJECT__(NAME) __EXPANDED_EXTENSION_FUNCTION__(__EXTENSION_OBJECT_PREFIX__, NAME)
