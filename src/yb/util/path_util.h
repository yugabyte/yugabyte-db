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
// Utility methods for dealing with file paths.
#pragma once

#include <string>

#include "yb/util/status_fwd.h"

namespace yb {

class Env;

// Appends path segments with the appropriate path separator, if necessary.
void AppendPathSegments(std::string* out, const std::string &b);

template <class... Args>
void AppendPathSegments(std::string* out, const std::string& a, Args&&... args) {
  AppendPathSegments(out, a);
  AppendPathSegments(out, std::forward<Args>(args)...);
}

template <class... Args>
std::string JoinPathSegments(const std::string& a, Args&&... args) {
  std::string result = a;
  AppendPathSegments(&result, std::forward<Args>(args)...);
  return result;
}

// Return the enclosing directory of path.
// This is like dirname(3) but for C++ strings.
std::string DirName(const std::string& path);

// Return the terminal component of a path.
// This is like basename(3) but for C++ strings.
std::string BaseName(const std::string& path);

// We ask the user to specify just a set of top level dirs where we can place data. However,
// under those, we create our own structure, which includes one level worth of generic suffix
// and another level under that splitting by server type (master vs tserver).
std::string GetYbDataPath(const std::string& root);
std::string GetServerTypeDataPath(const std::string& root, const std::string& server_type);

// For the user specified root dirs, setup the two level hierarchy below it and report the final
// path as well as whether we created the final path or not.
Status SetupRootDir(
    Env* env, const std::string& root, const std::string& server_type, std::string* out_dir,
    bool* created);

// Tests whether a given path allows creation of temporary files with O_DIRECT given a
// environment variable.
Status CheckODirectTempFileCreationInDir(Env* env, const std::string& dir_path);

} // namespace yb
