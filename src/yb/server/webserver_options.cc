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

#include "yb/server/webserver_options.h"

#include <limits.h>
#include <string.h>
#include <stdlib.h>
#include <string>

#include <gflags/gflags.h>
#include "yb/gutil/strings/substitute.h"
#include "yb/util/env.h"
#include "yb/util/flag_tags.h"
#include "yb/util/path_util.h"

using std::string;

namespace yb {

static std::string GetDefaultDocumentRoot();

} // namespace yb

// Flags defining web server behavior. The class implementation should
// not use these directly, but rather access them via WebserverOptions.
// This makes it easier to instantiate web servers with different options
// within a single unit test.
DEFINE_string(webserver_interface, "",
    "Interface to start debug webserver on. If blank, webserver binds to 0.0.0.0");
TAG_FLAG(webserver_interface, advanced);

DEFINE_string(webserver_doc_root, yb::GetDefaultDocumentRoot(),
    "Files under <webserver_doc_root> are accessible via the debug webserver. "
    "Defaults to $YB_HOME/www, or if $YB_HOME is not set, disables the document "
    "root");
TAG_FLAG(webserver_doc_root, advanced);

DEFINE_bool(webserver_enable_doc_root, true,
    "If true, webserver may serve static files from the webserver_doc_root");
TAG_FLAG(webserver_enable_doc_root, advanced);

DEFINE_string(webserver_certificate_file, "",
    "The location of the debug webserver's SSL certificate file, in .pem format. If "
    "empty, webserver SSL support is not enabled");
DEFINE_string(webserver_authentication_domain, "",
    "Domain used for debug webserver authentication");
DEFINE_string(webserver_password_file, "",
    "(Optional) Location of .htpasswd file containing user names and hashed passwords for"
    " debug webserver authentication");

DEFINE_int32(webserver_num_worker_threads, 50,
             "Maximum number of threads to start for handling web server requests");
TAG_FLAG(webserver_num_worker_threads, advanced);

DEFINE_int32(webserver_port, 0,
             "Port to bind to for the web server");
TAG_FLAG(webserver_port, stable);

namespace yb {

// Returns YB_HOME if set, otherwise we won't serve any static files.
static string GetDefaultDocumentRoot() {
  char* yb_home = getenv("YB_HOME");
  if (yb_home) {
    return strings::Substitute("$0/www", yb_home);
  }

  // If YB_HOME is not set, we use the path where the binary is located
  // (e.g., /opt/yugabyte/tserver/bin/yb-tserver) to determine the doc root.
  // To find "www"'s location, we search whether "www" exists at each directory, starting with
  // the directory where the current binary (yb-tserver, or yb-master) is located.
  // During each iteration, we keep going up one directory and do the search again.
  // If we can't find a directory that contains "www", we return a default value for now.
  string executable_path;
  auto status = Env::Default()->GetExecutablePath(&executable_path);
  if (!status.ok()) {
    LOG(WARNING) << "Ignoring status error: " << status.ToString();
    return "";
  }

  auto path = executable_path;
  while (path != "/") {
    path = DirName(path);
    auto www_dir = JoinPathSegments(path, "www");
    bool is_dir = false;
    auto status = Env::Default()->IsDirectory(www_dir, &is_dir);
    if (!status.ok()) {
      continue;
    }
    if (is_dir) {
      return www_dir;
    }
  }

  LOG(ERROR) << "Unable to find www directory by starting the search at path "
             << DirName(executable_path) << " and walking up the directory structure";

  // Return a path.
  return JoinPathSegments(DirName(DirName(executable_path)), "www");
}

WebserverOptions::WebserverOptions()
  : bind_interface(FLAGS_webserver_interface),
    port(FLAGS_webserver_port),
    doc_root(FLAGS_webserver_doc_root),
    enable_doc_root(FLAGS_webserver_enable_doc_root),
    certificate_file(FLAGS_webserver_certificate_file),
    authentication_domain(FLAGS_webserver_authentication_domain),
    password_file(FLAGS_webserver_password_file),
    num_worker_threads(FLAGS_webserver_num_worker_threads) {
}

} // namespace yb
