// Copyright (c) YugabyteDB, Inc.
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

#include <string>

#include "yb/tserver/tserver_util_fwd.h"
#include "yb/util/status_fwd.h"

namespace yb::pgwrapper {

class PgWrapperContext {
 public:
  virtual ~PgWrapperContext() = default;
  virtual void RegisterCertificateReloader(tserver::CertificateReloader reloader) = 0;
  virtual Status StartSharedMemoryNegotiation() = 0;
  virtual Status StopSharedMemoryNegotiation() = 0;
  virtual int SharedMemoryNegotiationFd() = 0;
  virtual const std::string& permanent_uuid() const = 0;
};

} // namespace yb::pgwrapper
