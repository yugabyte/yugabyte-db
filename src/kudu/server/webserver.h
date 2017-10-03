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
#ifndef KUDU_UTIL_WEBSERVER_H
#define KUDU_UTIL_WEBSERVER_H

#include <map>
#include <string>
#include <vector>
#include <boost/function.hpp>
#include <boost/thread/shared_mutex.hpp>

#include "kudu/server/webserver_options.h"
#include "kudu/util/net/sockaddr.h"
#include "kudu/util/status.h"
#include "kudu/util/web_callback_registry.h"

struct sq_connection;
struct sq_request_info;
struct sq_context;

namespace kudu {

// Wrapper class for the Mongoose web server library. Clients may register callback
// methods which produce output for a given URL path
class Webserver : public WebCallbackRegistry {
 public:
  // Using this constructor, the webserver will bind to all available
  // interfaces.
  explicit Webserver(const WebserverOptions& opts);

  ~Webserver();

  // Starts a webserver on the port passed to the constructor. The webserver runs in a
  // separate thread, so this call is non-blocking.
  Status Start();

  // Stops the webserver synchronously.
  void Stop();

  // Return the addresses that this server has successfully
  // bound to. Requires that the server has been Start()ed.
  Status GetBoundAddresses(std::vector<Sockaddr>* addrs) const;

  virtual void RegisterPathHandler(const std::string& path, const std::string& alias,
                                   const PathHandlerCallback& callback,
                                   bool is_styled = true, bool is_on_nav_bar = true) OVERRIDE;

  // Change the footer HTML to be displayed at the bottom of all styled web pages.
  void set_footer_html(const std::string& html);

  // True if serving all traffic over SSL, false otherwise
  bool IsSecure() const;
 private:
  // Container class for a list of path handler callbacks for a single URL.
  class PathHandler {
   public:
    PathHandler(bool is_styled, bool is_on_nav_bar, std::string alias)
        : is_styled_(is_styled),
          is_on_nav_bar_(is_on_nav_bar),
          alias_(std::move(alias)) {}

    void AddCallback(const PathHandlerCallback& callback) {
      callbacks_.push_back(callback);
    }

    bool is_styled() const { return is_styled_; }
    bool is_on_nav_bar() const { return is_on_nav_bar_; }
    const std::string& alias() const { return alias_; }
    const std::vector<PathHandlerCallback>& callbacks() const { return callbacks_; }

   private:
    // If true, the page appears is rendered styled.
    bool is_styled_;

    // If true, the page appears in the navigation bar.
    bool is_on_nav_bar_;

    // Alias used when displaying this link on the nav bar.
    std::string alias_;

    // List of callbacks to render output for this page, called in order.
    std::vector<PathHandlerCallback> callbacks_;
  };

  bool static_pages_available() const;

  // Build the string to pass to mongoose specifying where to bind.
  Status BuildListenSpec(std::string* spec) const;

  // Renders a common Bootstrap-styled header
  void BootstrapPageHeader(std::stringstream* output);

  // Renders a common Bootstrap-styled footer. Must be used in conjunction with
  // BootstrapPageHeader.
  void BootstrapPageFooter(std::stringstream* output);

  // Dispatch point for all incoming requests.
  // Static so that it can act as a function pointer, and then call the next method
  static int BeginRequestCallbackStatic(struct sq_connection* connection);
  int BeginRequestCallback(struct sq_connection* connection,
                           struct sq_request_info* request_info);

  int RunPathHandler(const PathHandler& handler,
                     struct sq_connection* connection,
                     struct sq_request_info* request_info);

  // Callback to funnel mongoose logs through glog.
  static int LogMessageCallbackStatic(const struct sq_connection* connection,
                                      const char* message);

  // Registered to handle "/", and prints a list of available URIs
  void RootHandler(const WebRequest& args, std::stringstream* output);

  // Builds a map of argument name to argument value from a typical URL argument
  // string (that is, "key1=value1&key2=value2.."). If no value is given for a
  // key, it is entered into the map as (key, "").
  void BuildArgumentMap(const std::string& args, ArgumentMap* output);

  const WebserverOptions opts_;

  // Lock guarding the path_handlers_ map and footer_html.
  boost::shared_mutex lock_;

  // Map of path to a PathHandler containing a list of handlers for that
  // path. More than one handler may register itself with a path so that many
  // components may contribute to a single page.
  typedef std::map<std::string, PathHandler*> PathHandlerMap;
  PathHandlerMap path_handlers_;

  // Snippet of HTML which will be displayed in the footer of all pages
  // rendered by this server. Protected by 'lock_'.
  std::string footer_html_;

  // The address of the interface on which to run this webserver.
  std::string http_address_;

  // Handle to Mongoose context; owned and freed by Mongoose internally
  struct sq_context* context_;
};

} // namespace kudu

#endif // KUDU_UTIL_WEBSERVER_H
