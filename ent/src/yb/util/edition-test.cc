// Copyright (c) YugaByte, Inc.

#include "yb/util/test_macros.h"
#include "yb/util/test_util.h"

namespace yb {
namespace util {

TEST(EditionTest, BasicTest) {
  LOG(INFO) << "This test is part of YugaByte Enterprise Edition but not Community Edition";
}

}
}
