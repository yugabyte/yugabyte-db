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
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#pragma once

#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#include "yb/server/webserver_options.h"

#include "yb/util/status_fwd.h"
#include "yb/util/net/net_fwd.h"
#include "yb/util/web_callback_registry.h"

namespace yb {

// Wrapper class for the Squeasel web server library. Clients may register callback
// methods which produce output for a given URL path
class Webserver : public WebCallbackRegistry {
 public:
  // Using this constructor, the webserver will bind to all available
  // interfaces. The server_name is used for display purposes.
  Webserver(const WebserverOptions& opts, const std::string& server_name);

  ~Webserver();

  // Starts a webserver on the port passed to the constructor. The webserver runs in a
  // separate thread, so this call is non-blocking.
  Status Start();

  // Stops the webserver synchronously.
  void Stop();

  // Return the addresses that this server has successfully
  // bound to. Requires that the server has been Start()ed.
  Status GetBoundAddresses(std::vector<Endpoint>* addrs) const;

  // Return the single HostPort that this server was asked to bind on
  Status GetInputHostPort(HostPort* hp) const;

  void SetLogging(bool enable_access_logging, bool enable_tcmalloc_logging);

  void RegisterPathHandler(const std::string& path,
                           const std::string& alias,
                           const PathHandlerCallback& callback,
                           bool is_styled = true,
                           bool is_on_nav_bar = true,
                           const std::string icon = "") override;

  // Change the footer HTML to be displayed at the bottom of all styled web pages.
  void set_footer_html(const std::string& html);

  // True if serving all traffic over SSL, false otherwise
  bool IsSecure() const;

  void SetAutoFlags(std::unordered_set<std::string>&& flags);

  bool ContainsAutoFlag(const std::string& flag) const;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace yb
