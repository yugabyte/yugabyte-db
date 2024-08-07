// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.helpers;

import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import javax.validation.constraints.NotNull;
import lombok.AllArgsConstructor;
import lombok.Builder;
import lombok.Data;
import lombok.EqualsAndHashCode;
import lombok.Getter;
import lombok.NoArgsConstructor;
import lombok.ToString;
import lombok.extern.jackson.Jacksonized;

/** Pair of node configuration type and its value. */
@Data
@AllArgsConstructor
@NoArgsConstructor
@ApiModel(description = "A node configuration.")
public class NodeConfig {
  @NotNull
  @ApiModelProperty(required = true)
  public Type type;

  @NotNull
  @ApiModelProperty(value = "true")
  @EqualsAndHashCode.Exclude
  public String value;

  @Builder
  @Getter
  @Jacksonized
  @ToString
  @ApiModel(description = "Validation result of a node config")
  public static class ValidationResult {
    private Type type;

    private boolean valid;
    private boolean required;

    private String description;
    private String value;
  }

  /**
   * Type of the configuration. The predicate can be minimum value comparison like ulimit. By
   * default, all the types are enabled in all modes if no specific available type is specified.
   */
  @Getter
  public enum Type {
    NTP_SERVICE_STATUS("Running status of NTP service"),

    PROMETHEUS_SPACE("Disk space in MB for prometheus"),

    MOUNT_POINTS_WRITABLE("Mount points are writable"),

    USER("Existence of user"),

    USER_GROUP("Existence of user group"),

    HOME_DIR_SPACE("Disk space in MB for home directory"),

    HOME_DIR_EXISTS("Home directory exists"),

    RAM_SIZE("Total RAM size in MB"),

    INTERNET_CONNECTION("Internet connectivity"),

    CPU_CORES("Number of CPU cores"),

    PROMETHEUS_NO_NODE_EXPORTER("No running node exporter"),

    TMP_DIR_SPACE("Temp directory disk space in MB"),

    PAM_LIMITS_WRITABLE("PAM limits writable"),

    PYTHON_VERSION("Supported python version"),

    MOUNT_POINTS_VOLUME("Disk space in MB for mount points"),

    CHRONYD_RUNNING("Chronyd running"),

    SSH_PORT("SSH port is open"),

    SUDO_ACCESS("Sudo access available"),

    OPENSSL("OpenSSL package is installed"),

    POLICYCOREUTILS("Policycoreutils package is installed"),

    RSYNC("Rsync package is installed"),

    XXHASH("Xxhash package is installed"),

    LIBATOMIC1("Libatomic1 package is installed"),

    LIBNCURSES6("Libncurses6 package is installed"),

    LIBATOMIC("Libatomic package is installed"),

    AZCOPY("Azcopy binary is installed"),

    CHRONYC("Chronyc binary is installed"),

    GSUTIL("Gsutil binary is installed"),

    S3CMD("S3cmd binary is installed"),

    NODE_EXPORTER_RUNNING("Node exporter is running"),

    NODE_EXPORTER_PORT("Node exporter is running on the correct port"),

    SWAPPINESS("Swappiness of memory pages"),

    ULIMIT_CORE("Maximum size of core files created"),

    ULIMIT_OPEN_FILES("Maximum number of open file descriptors"),

    ULIMIT_USER_PROCESSES("Maximum number of processes available to a single user"),

    SYSTEMD_SUDOER_ENTRY("Systemd Sudoer entry"),

    SSH_ACCESS("Ability to ssh into node as yugabyte user with key supplied in provider"),

    NODE_AGENT_ACCESS("Reachability of node agent server"),

    MASTER_HTTP_PORT("Master http port is open"),

    MASTER_RPC_PORT("Master rpc port is open"),

    TSERVER_HTTP_PORT("TServer http port is open"),

    TSERVER_RPC_PORT("TServer rpc port is open"),

    YB_CONTROLLER_HTTP_PORT("YbController http port is open"),

    YB_CONTROLLER_RPC_PORT("YbController rpc port is open"),

    REDIS_SERVER_HTTP_PORT("Redis server http port is open"),

    REDIS_SERVER_RPC_PORT("Redis server rpc port is open"),

    YCQL_SERVER_HTTP_PORT("YCQL server http port is open"),

    YCQL_SERVER_RPC_PORT("YCQL server rpc port is open"),

    YSQL_SERVER_HTTP_PORT("YSQL server http port is open"),

    YSQL_SERVER_RPC_PORT("YSQL server rpc port is open"),

    VM_MAX_MAP_COUNT("VM max memory map count");

    private final String description;

    private Type(String description) {
      this.description = description;
    }
  }

  @Getter
  public enum Operation {
    PROVISION,
    CONFIGURE
  }
}
