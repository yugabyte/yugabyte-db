// Copyright (c) YugaByte, Inc.

#ifndef YB_DOCDB_DOCDB_INTERNAL_H_
#define YB_DOCDB_DOCDB_INTERNAL_H_

#include "yb/gutil/strings/substitute.h"

// This file should only be included in .cc files of the docdb subsystem. Defines some macros for
// debugging DocDB functionality.

// Enable this during debugging only. This enables very verbose logging. Should always be undefined
// when code is checked in.
#undef DOCDB_DEBUG

#ifdef DOCDB_DEBUG
#define DOCDB_DEBUG_LOG(...) \
  do { \
    LOG(INFO) << "DocDB DEBUG [" << __func__  << "]: " \
              << strings::Substitute(__VA_ARGS__); \
  } while (false)

#else
#define DOCDB_DEBUG_LOG(...)
#endif

#endif
