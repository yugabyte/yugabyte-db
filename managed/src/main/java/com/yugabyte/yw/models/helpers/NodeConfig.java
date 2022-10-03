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
  @ToString
  @ApiModel(description = "Validation result of a node config")
  public static class ValidationResult {
    private Type type;
    private boolean isValid;
    private boolean isRequired;
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

    MOUNT_POINTS("Existence of mount points"),

    USER("Existence of user"),

    USER_GROUP("Existence of user group"),

    HOME_DIR_SPACE("Disk space in MB for home directory"),

    RAM_SIZE("Total RAM size in MB"),

    INTERNET_CONNECTION("Internet connectivity"),

    CPU_CORES("Number of CPU cores"),

    PROMETHEUS_NO_NODE_EXPORTER("No running node exporter"),

    TMP_DIR_SPACE("Temp directory disk space in MB"),

    PAM_LIMITS_WRITABLE("PAM limits writable"),

    PORTS("Available ports"),

    PYTHON_VERSION("Min python version");

    private final String description;

    private Type(String description) {
      this.description = description;
    }
  }
}
