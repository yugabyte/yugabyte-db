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

#ifndef KUDU_CODEGEN_PRECOMPILED_LL_H
#define KUDU_CODEGEN_PRECOMPILED_LL_H

namespace kudu {
namespace codegen {

// Declare the precompiled LLVM bitcode data. The actual data is provided by a
// cc file generated at build time using xxd. See codegen/CMakeLists.txt.
extern const char precompiled_ll_data[];
extern const unsigned int precompiled_ll_len;

} // namespace codegen
} // namespace kudu

#endif // KUDU_CODEGEN_PRECOMPILED_LL_H
