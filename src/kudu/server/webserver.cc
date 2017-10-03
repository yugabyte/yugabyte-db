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

#include "kudu/server/webserver.h"

#include <algorithm>
#include <stdio.h>
#include <signal.h>
#include <string>
#include <map>
#include <vector>
#include <boost/lexical_cast.hpp>
#include <boost/bind.hpp>
#include <boost/mem_fn.hpp>
#include <boost/algorithm/string.hpp>
#include <gflags/gflags.h>
#include <glog/logging.h>
#include <squeasel.h>

#include "kudu/gutil/map-util.h"
#include "kudu/gutil/stl_util.h"
#include "kudu/gutil/stringprintf.h"
#include "kudu/gutil/strings/join.h"
#include "kudu/gutil/strings/numbers.h"
#include "kudu/gutil/strings/split.h"
#include "kudu/gutil/strings/stringpiece.h"
#include "kudu/util/env.h"
#include "kudu/util/flag_tags.h"
#include "kudu/util/net/net_util.h"
#include "kudu/util/url-coding.h"
#include "kudu/util/version_info.h"

#if defined(__APPLE__)
typedef sig_t sighandler_t;
#endif

using std::string;
using std::stringstream;
using std::vector;
using std::make_pair;

DEFINE_int32(webserver_max_post_length_bytes, 1024 * 1024,
             "The maximum length of a POST request that will be accepted by "
             "the embedded web server.");
TAG_FLAG(webserver_max_post_length_bytes, advanced);
TAG_FLAG(webserver_max_post_length_bytes, runtime);

namespace kudu {

Webserver::Webserver(const WebserverOptions& opts)
  : opts_(opts),
    context_(nullptr) {
  string host = opts.bind_interface.empty() ? "0.0.0.0" : opts.bind_interface;
  http_address_ = host + ":" + boost::lexical_cast<string>(opts.port);
}

Webserver::~Webserver() {
  Stop();
  STLDeleteValues(&path_handlers_);
}

void Webserver::RootHandler(const Webserver::WebRequest& args, stringstream* output) {
  (*output) << "<h2>Status Pages</h2>";
  for (const PathHandlerMap::value_type& handler : path_handlers_) {
    if (handler.second->is_on_nav_bar()) {
      (*output) << "<a href=\"" << handler.first << "\">" << handler.second->alias() << "</a><br/>";
    }
  }
  (*output) << "<hr/>\n";
  (*output) << "<h2>Version Info</h2>\n";
  (*output) << "<pre>" << EscapeForHtmlToString(VersionInfo::GetAllVersionInfo()) << "</pre>";
}

void Webserver::BuildArgumentMap(const string& args, ArgumentMap* output) {
  vector<StringPiece> arg_pairs = strings::Split(args, "&");

  for (const StringPiece& arg_pair : arg_pairs) {
    vector<StringPiece> key_value = strings::Split(arg_pair, "=");
    if (key_value.empty()) continue;

    string key;
    if (!UrlDecode(key_value[0].ToString(), &key)) continue;
    string value;
    if (!UrlDecode((key_value.size() >= 2 ? key_value[1].ToString() : ""), &value)) continue;
    boost::to_lower(key);
    (*output)[key] = value;
  }
}

bool Webserver::IsSecure() const {
  return !opts_.certificate_file.empty();
}

Status Webserver::BuildListenSpec(string* spec) const {
  vector<Sockaddr> addrs;
  RETURN_NOT_OK(ParseAddressList(http_address_, 80, &addrs));

  vector<string> parts;
  for (const Sockaddr& addr : addrs) {
    // Mongoose makes sockets with 's' suffixes accept SSL traffic only
    parts.push_back(addr.ToString() + (IsSecure() ? "s" : ""));
  }

  JoinStrings(parts, ",", spec);
  return Status::OK();
}

Status Webserver::Start() {
  LOG(INFO) << "Starting webserver on " << http_address_;

  vector<const char*> options;

  if (static_pages_available()) {
    LOG(INFO) << "Document root: " << opts_.doc_root;
    options.push_back("document_root");
    options.push_back(opts_.doc_root.c_str());
    options.push_back("enable_directory_listing");
    options.push_back("no");
  } else {
    LOG(INFO)<< "Document root disabled";
  }

  if (IsSecure()) {
    LOG(INFO) << "Webserver: Enabling HTTPS support";
    options.push_back("ssl_certificate");
    options.push_back(opts_.certificate_file.c_str());
  }

  if (!opts_.authentication_domain.empty()) {
    options.push_back("authentication_domain");
    options.push_back(opts_.authentication_domain.c_str());
  }

  if (!opts_.password_file.empty()) {
    // Mongoose doesn't log anything if it can't stat the password file (but will if it
    // can't open it, which it tries to do during a request)
    if (!Env::Default()->FileExists(opts_.password_file)) {
      stringstream ss;
      ss << "Webserver: Password file does not exist: " << opts_.password_file;
      return Status::InvalidArgument(ss.str());
    }
    LOG(INFO) << "Webserver: Password file is " << opts_.password_file;
    options.push_back("global_passwords_file");
    options.push_back(opts_.password_file.c_str());
  }

  options.push_back("listening_ports");
  string listening_str;
  RETURN_NOT_OK(BuildListenSpec(&listening_str));
  options.push_back(listening_str.c_str());

  // Num threads
  options.push_back("num_threads");
  string num_threads_str = SimpleItoa(opts_.num_worker_threads);
  options.push_back(num_threads_str.c_str());

  // Options must be a NULL-terminated list
  options.push_back(nullptr);

  // mongoose ignores SIGCHLD and we need it to run kinit. This means that since
  // mongoose does not reap its own children CGI programs must be avoided.
  // Save the signal handler so we can restore it after mongoose sets it to be ignored.
  sighandler_t sig_chld = signal(SIGCHLD, SIG_DFL);

  sq_callbacks callbacks;
  memset(&callbacks, 0, sizeof(callbacks));
  callbacks.begin_request = &Webserver::BeginRequestCallbackStatic;
  callbacks.log_message = &Webserver::LogMessageCallbackStatic;

  // To work around not being able to pass member functions as C callbacks, we store a
  // pointer to this server in the per-server state, and register a static method as the
  // default callback. That method unpacks the pointer to this and calls the real
  // callback.
  context_ = sq_start(&callbacks, reinterpret_cast<void*>(this), &options[0]);

  // Restore the child signal handler so wait() works properly.
  signal(SIGCHLD, sig_chld);

  if (context_ == nullptr) {
    stringstream error_msg;
    error_msg << "Webserver: Could not start on address " << http_address_;
    Sockaddr addr;
    addr.set_port(opts_.port);
    TryRunLsof(addr);
    return Status::NetworkError(error_msg.str());
  }

  PathHandlerCallback default_callback =
    boost::bind<void>(boost::mem_fn(&Webserver::RootHandler), this, _1, _2);

  RegisterPathHandler("/", "Home", default_callback);

  vector<Sockaddr> addrs;
  RETURN_NOT_OK(GetBoundAddresses(&addrs));
  string bound_addresses_str;
  for (const Sockaddr& addr : addrs) {
    if (!bound_addresses_str.empty()) {
      bound_addresses_str += ", ";
    }
    bound_addresses_str += "http://" + addr.ToString() + "/";
  }

  LOG(INFO) << "Webserver started. Bound to: " << bound_addresses_str;
  return Status::OK();
}

void Webserver::Stop() {
  if (context_ != nullptr) {
    sq_stop(context_);
    context_ = nullptr;
  }
}

Status Webserver::GetBoundAddresses(std::vector<Sockaddr>* addrs) const {
  if (!context_) {
    return Status::IllegalState("Not started");
  }

  struct sockaddr_in** sockaddrs;
  int num_addrs;

  if (sq_get_bound_addresses(context_, &sockaddrs, &num_addrs)) {
    return Status::NetworkError("Unable to get bound addresses from Mongoose");
  }

  addrs->reserve(num_addrs);

  for (int i = 0; i < num_addrs; i++) {
    addrs->push_back(Sockaddr(*sockaddrs[i]));
    free(sockaddrs[i]);
  }
  free(sockaddrs);

  return Status::OK();
}

int Webserver::LogMessageCallbackStatic(const struct sq_connection* connection,
                                        const char* message) {
  if (message != nullptr) {
    LOG(INFO) << "Webserver: " << message;
    return 1;
  }
  return 0;
}

int Webserver::BeginRequestCallbackStatic(struct sq_connection* connection) {
  struct sq_request_info* request_info = sq_get_request_info(connection);
  Webserver* instance = reinterpret_cast<Webserver*>(request_info->user_data);
  return instance->BeginRequestCallback(connection, request_info);
}

int Webserver::BeginRequestCallback(struct sq_connection* connection,
                                    struct sq_request_info* request_info) {
  PathHandler* handler;
  {
    boost::shared_lock<boost::shared_mutex> lock(lock_);
    PathHandlerMap::const_iterator it = path_handlers_.find(request_info->uri);
    if (it == path_handlers_.end()) {
      // Let Mongoose deal with this request; returning NULL will fall through
      // to the default handler which will serve files.
      if (!opts_.doc_root.empty() && opts_.enable_doc_root) {
        VLOG(2) << "HTTP File access: " << request_info->uri;
        return 0;
      } else {
        sq_printf(connection, "HTTP/1.1 404 Not Found\r\n"
                  "Content-Type: text/plain\r\n\r\n");
        sq_printf(connection, "No handler for URI %s\r\n\r\n", request_info->uri);
        return 1;
      }
    }
    handler = it->second;
  }

  return RunPathHandler(*handler, connection, request_info);
}


int Webserver::RunPathHandler(const PathHandler& handler,
                              struct sq_connection* connection,
                              struct sq_request_info* request_info) {
  // Should we render with css styles?
  bool use_style = true;

  WebRequest req;
  if (request_info->query_string != nullptr) {
    req.query_string = request_info->query_string;
    BuildArgumentMap(request_info->query_string, &req.parsed_args);
  }
  req.request_method = request_info->request_method;
  if (req.request_method == "POST") {
    const char* content_len_str = sq_get_header(connection, "Content-Length");
    int32_t content_len = 0;
    if (content_len_str == nullptr ||
        !safe_strto32(content_len_str, &content_len)) {
      sq_printf(connection, "HTTP/1.1 411 Length Required\r\n");
      return 1;
    }
    if (content_len > FLAGS_webserver_max_post_length_bytes) {
      // TODO: for this and other HTTP requests, we should log the
      // remote IP, etc.
      LOG(WARNING) << "Rejected POST with content length " << content_len;
      sq_printf(connection, "HTTP/1.1 413 Request Entity Too Large\r\n");
      return 1;
    }

    char buf[8192];
    int rem = content_len;
    while (rem > 0) {
      int n = sq_read(connection, buf, std::min<int>(sizeof(buf), rem));
      if (n <= 0) {
        LOG(WARNING) << "error reading POST data: expected "
                     << content_len << " bytes but only read "
                     << req.post_data.size();
        sq_printf(connection, "HTTP/1.1 500 Internal Server Error\r\n");
        return 1;
      }

      req.post_data.append(buf, n);
      rem -= n;
    }
  }

  if (!handler.is_styled() || ContainsKey(req.parsed_args, "raw")) {
    use_style = false;
  }

  stringstream output;
  if (use_style) BootstrapPageHeader(&output);
  for (const PathHandlerCallback& callback_ : handler.callbacks()) {
    callback_(req, &output);
  }
  if (use_style) BootstrapPageFooter(&output);

  string str = output.str();
  // Without styling, render the page as plain text
  if (!use_style) {
    sq_printf(connection, "HTTP/1.1 200 OK\r\n"
              "Content-Type: text/plain\r\n"
              "Content-Length: %zd\r\n"
              "\r\n", str.length());
  } else {
    sq_printf(connection, "HTTP/1.1 200 OK\r\n"
              "Content-Type: text/html\r\n"
              "Content-Length: %zd\r\n"
              "\r\n", str.length());
  }

  // Make sure to use sq_write for printing the body; sq_printf truncates at 8kb
  sq_write(connection, str.c_str(), str.length());
  return 1;
}

void Webserver::RegisterPathHandler(const string& path, const string& alias,
    const PathHandlerCallback& callback, bool is_styled, bool is_on_nav_bar) {
  boost::lock_guard<boost::shared_mutex> lock(lock_);
  auto it = path_handlers_.find(path);
  if (it == path_handlers_.end()) {
    it = path_handlers_.insert(
        make_pair(path, new PathHandler(is_styled, is_on_nav_bar, alias))).first;
  }
  it->second->AddCallback(callback);
}

const char* const PAGE_HEADER = "<!DOCTYPE html>"
" <html>"
"   <head><title>Kudu</title>"
" <link href='/bootstrap/css/bootstrap.min.css' rel='stylesheet' media='screen' />"
" <link href='/kudu.css' rel='stylesheet' />"
" </head>"
" <body>";

static const char* const NAVIGATION_BAR_PREFIX =
"<div class='navbar navbar-inverse navbar-fixed-top'>"
"      <div class='navbar-inner'>"
"        <div class='container-fluid'>"
"          <a href='/'>"
"            <img src=\"/logo.png\" width='61' height='45' alt=\"Kudu\" style=\"float:left\"/>"
"          </a>"
"          <div class='nav-collapse collapse'>"
"            <ul class='nav'>";

static const char* const NAVIGATION_BAR_SUFFIX =
"            </ul>"
"          </div>"
"        </div>"
"      </div>"
"    </div>"
"    <div class='container-fluid'>";

void Webserver::BootstrapPageHeader(stringstream* output) {
  (*output) << PAGE_HEADER;
  (*output) << NAVIGATION_BAR_PREFIX;
  for (const PathHandlerMap::value_type& handler : path_handlers_) {
    if (handler.second->is_on_nav_bar()) {
      (*output) << "<li><a href=\"" << handler.first << "\">" << handler.second->alias()
                << "</a></li>";
    }
  }
  (*output) << NAVIGATION_BAR_SUFFIX;

  if (!static_pages_available()) {
    (*output) << "<div style=\"color: red\"><strong>"
              << "Static pages not available. Configure KUDU_HOME or use the --webserver_doc_root "
              << "flag to fix page styling.</strong></div>\n";
  }
}

bool Webserver::static_pages_available() const {
  return !opts_.doc_root.empty() && opts_.enable_doc_root;
}

void Webserver::set_footer_html(const std::string& html) {
  boost::lock_guard<boost::shared_mutex> l(lock_);
  footer_html_ = html;
}

void Webserver::BootstrapPageFooter(stringstream* output) {
  boost::shared_lock<boost::shared_mutex> l(lock_);
  *output << "</div>\n"; // end bootstrap 'container' div
  if (!footer_html_.empty()) {
    *output << "<footer class=\"footer\"><div class=\"container text-muted\">";
    *output << footer_html_;
    *output << "</div></footer>";
  }
  *output << "</body></html>";
}

} // namespace kudu
