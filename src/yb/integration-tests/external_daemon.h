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

#include <string.h>
#include <sys/types.h>

#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest_prod.h>
#include <rapidjson/document.h>

#include "yb/gutil/macros.h"
#include "yb/gutil/ref_counted.h"
#include "yb/gutil/strings/substitute.h"

#include "yb/util/curl_util.h"
#include "yb/util/env.h"
#include "yb/util/jsonreader.h"
#include "yb/util/metrics.h"
#include "yb/util/net/net_util.h"
#include "yb/util/status_fwd.h"
#include "yb/util/status.h"

namespace yb {

namespace rpc {
class Messenger;
class ProxyCache;
}  // namespace rpc

namespace server {
class ServerStatusPB;
}  // namespace server

class HybridTime;
class OpIdPB;
class NodeInstancePB;
class Subprocess;
struct GlobalLogTailerState;

YB_STRONGLY_TYPED_BOOL(RequireExitCode0);
YB_STRONGLY_TYPED_BOOL(SafeShutdown);

class ExternalDaemon : public RefCountedThreadSafe<ExternalDaemon> {
 public:
  class StringListener {
   public:
    virtual void Handle(const GStringPiece& s) = 0;
    virtual ~StringListener() {}
  };

  class LogTailerThread {
   public:
    LogTailerThread(const std::string& line_prefix, const int child_fd, std::ostream* const out);

    void SetListener(StringListener* listener);

    void RemoveListener(StringListener* listener);

    StringListener* listener() { return listener_.load(std::memory_order_acquire); }

    ~LogTailerThread();

   private:
    static GlobalLogTailerState* global_state();

    static std::atomic<bool>* CreateStoppedFlagForId(int id);

    const int id_;

    // This lock protects the stopped_ pointer in case of a race between tailer thread's
    // initialization (i.e. before it gets into its loop) and the destructor.
    std::mutex state_lock_;

    std::atomic<bool>* const stopped_;
    const std::string thread_desc_;  // A human-readable description of this thread.
    std::thread thread_;
    std::atomic<StringListener*> listener_{nullptr};
  };

  ExternalDaemon(
      std::string daemon_id, rpc::Messenger* messenger, rpc::ProxyCache* proxy_cache,
      const std::string& exe, const std::string& root_dir,
      const std::vector<std::string>& data_dirs, const std::vector<std::string>& extra_flags);

  HostPort bound_rpc_hostport() const;
  HostPort bound_rpc_addr() const;
  HostPort bound_http_hostport() const;
  const NodeInstancePB& instance_id() const;
  const std::string& uuid() const;

  // Return the pid of the running process.  Causes a CHECK failure if the process is not running.
  pid_t pid() const;

  const std::string& id() const { return daemon_id_; }

  // Sends a SIGSTOP signal to the daemon.
  Status Pause();

  // Sends a SIGCONT signal to the daemon.
  Status Resume();

  Status Kill(int signal);

  // Return true if we have explicitly shut down the process.
  bool IsShutdown() const;

  // Was SIGKILL used to shutdown the process?
  bool WasUnsafeShutdown() const;

  // Return true if the process is still running.  This may return false if the process crashed,
  // even if we didn't explicitly call Shutdown().
  bool IsProcessAlive(RequireExitCode0 require_exit_code_0 = RequireExitCode0::kFalse) const;

  bool IsProcessPaused() const;

  virtual void Shutdown(
      SafeShutdown safe_shutdown = SafeShutdown::kFalse,
      RequireExitCode0 require_exit_code_0 = RequireExitCode0::kFalse);

  std::vector<std::string> GetDataDirs() const { return data_dirs_; }

  const std::string& exe() const { return exe_; }

  const std::string& GetRootDir() const { return root_dir_; }

  // Return a pointer to the flags used for this server on restart.  Modifying these flags will only
  // take effect on the next restart.
  std::vector<std::string>* mutable_flags() { return &extra_flags_; }

  // Retrieve the value of a given type metric from this server.
  //
  // 'value_field' represents the particular field of the metric to be read.  For example, for a
  // counter or gauge, this should be 'value'. For a histogram, it might be 'total_count' or 'mean'.
  //
  // 'entity_id' may be NULL, in which case the first entity of the same type as 'entity_proto' will
  // be matched.

  template <class ValueType>
  Result<ValueType> GetMetric(
      const MetricEntityPrototype* entity_proto, const char* entity_id,
      const MetricPrototype* metric_proto, const char* value_field) const {
    return GetMetricFromHost<ValueType>(
        bound_http_hostport(), entity_proto, entity_id, metric_proto, value_field);
  }

  template <class ValueType>
  Result<ValueType> GetMetric(
      const char* entity_proto_name, const char* entity_id, const char* metric_proto_name,
      const char* value_field) const {
    return GetMetricFromHost<ValueType>(
        bound_http_hostport(), entity_proto_name, entity_id, metric_proto_name, value_field);
  }

  std::string LogPrefix();

  void SetLogListener(StringListener* listener);

  void RemoveLogListener(StringListener* listener);

  template <class ValueType>
  static Result<ValueType> GetMetricFromHost(
      const HostPort& hostport, const MetricEntityPrototype* entity_proto, const char* entity_id,
      const MetricPrototype* metric_proto, const char* value_field) {
    return GetMetricFromHost<ValueType>(
        hostport, entity_proto->name(), entity_id, metric_proto->name(), value_field);
  }

  template <class ValueType>
  static Result<ValueType> GetMetricFromHost(
      const HostPort& hostport, const char* entity_proto_name, const char* entity_id,
      const char* metric_proto_name, const char* value_field) {
    // Fetch metrics whose name matches the given prototype.
    std::string url = strings::Substitute(
        "http://$0/jsonmetricz?metrics=$1", hostport.ToString(), metric_proto_name);
    EasyCurl curl;
    faststring dst;
    RETURN_NOT_OK(curl.FetchURL(url, &dst));

    // Parse the results, beginning with the top-level entity array.
    JsonReader r(dst.ToString());
    RETURN_NOT_OK(r.Init());
    std::vector<const rapidjson::Value*> entities;
    RETURN_NOT_OK(r.ExtractObjectArray(r.root(), NULL, &entities));
    for (const rapidjson::Value* entity : entities) {
      // Find the desired entity.
      std::string type;
      RETURN_NOT_OK(r.ExtractString(entity, "type", &type));
      if (type != entity_proto_name) {
        continue;
      }
      if (entity_id) {
        std::string id;
        RETURN_NOT_OK(r.ExtractString(entity, "id", &id));
        if (id != entity_id) {
          continue;
        }
      }

      // Find the desired metric within the entity.
      std::vector<const rapidjson::Value*> metrics;
      RETURN_NOT_OK(r.ExtractObjectArray(entity, "metrics", &metrics));
      for (const rapidjson::Value* metric : metrics) {
        std::string name;
        RETURN_NOT_OK(r.ExtractString(metric, "name", &name));
        if (name != metric_proto_name) {
          continue;
        }
        return ExtractMetricValue<ValueType>(r, metric, value_field);
      }
    }
    std::string msg;
    if (entity_id) {
      msg = strings::Substitute(
          "Could not find metric $0.$1 for entity $2", entity_proto_name, metric_proto_name,
          entity_id);
    } else {
      msg =
          strings::Substitute("Could not find metric $0.$1", entity_proto_name, metric_proto_name);
    }
    return STATUS(NotFound, msg);
  }

  template <class ValueType>
  static Result<ValueType> ExtractMetricValue(
      const JsonReader& r, const rapidjson::Value* object, const char* field);

  // Get the current value of the flag for the given daemon.
  Result<std::string> GetFlag(const std::string& flag);
  Result<HybridTime> GetServerTime();

  // Add a flag to the extra flags. A restart is required to get any effect of that change.
  void AddExtraFlag(const std::string& flag, const std::string& value);

  // Remove a flag from the extra flags. A restart is required to get any effect of that change.
  size_t RemoveExtraFlag(const std::string& flag);

 protected:
  friend class RefCountedThreadSafe<ExternalDaemon>;
  virtual ~ExternalDaemon();

  Status StartProcess(const std::vector<std::string>& flags);

  virtual Status DeleteServerInfoPaths();

  virtual bool ServerInfoPathsExist();

  virtual Status BuildServerStateFromInfoPath();

  Status BuildServerStateFromInfoPath(
      const std::string& info_path, std::unique_ptr<server::ServerStatusPB>* server_status);

  std::string GetServerInfoPath();

  // In a code-coverage build, try to flush the coverage data to disk.
  // In a non-coverage build, this does nothing.
  void FlushCoverage();

  std::string ProcessNameAndPidStr();

  const std::string daemon_id_;
  rpc::Messenger* messenger_;
  rpc::ProxyCache* proxy_cache_;
  const std::string exe_;
  const std::string root_dir_;
  std::vector<std::string> data_dirs_;
  std::vector<std::string> extra_flags_;

  std::unique_ptr<Subprocess> process_;
  bool is_paused_ = false;
  bool sigkill_used_for_shutdown_ = false;

  std::unique_ptr<server::ServerStatusPB> status_;

  // These capture the daemons parameters and running ports and
  // are used to Restart() the daemon with the same parameters.
  HostPort bound_rpc_;
  HostPort bound_http_;

 private:
  std::unique_ptr<LogTailerThread> stdout_tailer_thread_, stderr_tailer_thread_;

  DISALLOW_COPY_AND_ASSIGN(ExternalDaemon);
};
}  // namespace yb
