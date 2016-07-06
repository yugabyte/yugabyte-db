// Copyright (c) YugaByte, Inc.

#ifndef YB_UTIL_STRING_PACKER_H
#define YB_UTIL_STRING_PACKER_H

#include<string>
#include<vector>

#include "yb/util/string_packer.h"

namespace yb {
namespace util {

// TODO: Make these work with slices so there is less copying involved.
// Not optimizing for that now, since this string packing process may be deprecated in future.

std::string PackZeroEncoded(const std::vector<std::string>& pieces);

std::vector<std::string> UnpackZeroEncoded(std::string packed);

}
}

#endif
