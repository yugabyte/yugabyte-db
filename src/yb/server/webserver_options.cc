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

#include <string.h>

#include <string>

#include "yb/util/env.h"
#include "yb/util/env_util.h"
#include "yb/util/flags.h"
#include "yb/util/path_util.h"

using std::string;

namespace yb {

static std::string GetDefaultDocumentRoot();

} // namespace yb

// Flags defining web server behavior. The class implementation should
// not use these directly, but rather access them via WebserverOptions.
// This makes it easier to instantiate web servers with different options
// within a single unit test.
DEFINE_UNKNOWN_string(
    webserver_interface, "",
    "Interface to start debug webserver on. If blank, webserver binds to first host IP"
    "present in the list of comma separated rpc_bind_addresses");
TAG_FLAG(webserver_interface, advanced);

// We use an empty default value here because we can't call GetDefaultDocumentRoot from flag
// initilization. Instead, we call GetDefaultDocumentRoot if we find that the flag is empty.
DEFINE_UNKNOWN_string(webserver_doc_root, "",
    "Files under <webserver_doc_root> are accessible via the debug webserver. "
    "Defaults to $YB_HOME/www, or if $YB_HOME is not set, disables the document "
    "root");
TAG_FLAG(webserver_doc_root, advanced);

DEFINE_UNKNOWN_bool(webserver_enable_doc_root, true,
    "If true, webserver may serve static files from the webserver_doc_root");
TAG_FLAG(webserver_enable_doc_root, advanced);

DEFINE_UNKNOWN_string(webserver_ca_certificate_file, "",
    "The location of the certificate of the certificate authority of the debug webserver's SSL "
    "certificate file, in .pem format. If empty, system-wide CA certificates are used.");
DEFINE_UNKNOWN_string(webserver_certificate_file, "",
    "The location of the debug webserver's SSL certificate file, in .pem format. If "
    "empty, webserver SSL support is not enabled");
DEFINE_UNKNOWN_string(webserver_private_key_file, "",
    "The location of the debug webserver's SSL private key file, in .pem format. If "
    "empty, the private key is assumed to be located in the same file as the certificate.");
DEFINE_UNKNOWN_string(webserver_private_key_password, "",
    "The password for the debug webserver's SSL private key. If empty, no password is used.");
DEFINE_UNKNOWN_string(webserver_authentication_domain, "",
    "Domain used for debug webserver authentication");
DEFINE_UNKNOWN_string(webserver_password_file, "",
    "(Optional) Location of .htpasswd file containing user names and hashed passwords for"
    " debug webserver authentication");

DEFINE_UNKNOWN_int32(webserver_num_worker_threads, 50,
             "Maximum number of threads to start for handling web server requests");
TAG_FLAG(webserver_num_worker_threads, advanced);

DEFINE_UNKNOWN_int32(webserver_port, 0,
             "Port to bind to for the web server");
TAG_FLAG(webserver_port, stable);

namespace yb {

// Returns $YB_HOME/www if set, else ROOT_DIR/www, where ROOT_DIR is computed based on executable
// path.
static string GetDefaultDocumentRoot() {
  return JoinPathSegments(yb::env_util::GetRootDir("www"), "www");
}

WebserverOptions::WebserverOptions()
  : bind_interface(FLAGS_webserver_interface),
    port(FLAGS_webserver_port),
    enable_doc_root(FLAGS_webserver_enable_doc_root),
    certificate_file(FLAGS_webserver_certificate_file),
    private_key_file(FLAGS_webserver_private_key_file),
    private_key_password(FLAGS_webserver_private_key_password),
    authentication_domain(FLAGS_webserver_authentication_domain),
    password_file(FLAGS_webserver_password_file),
    num_worker_threads(FLAGS_webserver_num_worker_threads) {
  doc_root = FLAGS_webserver_doc_root.empty() ?
      GetDefaultDocumentRoot() : FLAGS_webserver_doc_root;
}

} // namespace yb
