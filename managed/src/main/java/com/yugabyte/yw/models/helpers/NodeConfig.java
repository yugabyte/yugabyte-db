// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.helpers;

import static com.yugabyte.yw.models.helpers.NodeConfigValidator.PredicateType.GREATER_EQUAL;
import static com.yugabyte.yw.models.helpers.NodeConfigValidator.PredicateType.JSON_STRINGS_EQUAL;
import static com.yugabyte.yw.models.helpers.NodeConfigValidator.PredicateType.MIN_VERSION;
import static com.yugabyte.yw.models.helpers.NodeConfigValidator.PredicateType.STRING_EQUALS;

import com.fasterxml.jackson.annotation.JsonIgnore;
import com.yugabyte.yw.models.Provider;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.function.Predicate;
import javax.validation.constraints.NotNull;
import lombok.AllArgsConstructor;
import lombok.Builder;
import lombok.Data;
import lombok.EqualsAndHashCode;
import lombok.Getter;
import lombok.NoArgsConstructor;

/** Pair of node configuration type and its value. */
@Data
@NoArgsConstructor
@AllArgsConstructor
@ApiModel(description = "A node configuration.")
public class NodeConfig {
  @NotNull
  @ApiModelProperty(required = true)
  public Type type;

  @NotNull
  @ApiModelProperty(value = "unlimited", required = true)
  @EqualsAndHashCode.Exclude
  public String value;

  /**
   * Checks if the value is accepted according to the predicate in the type.
   *
   * @return true if it is accepted or configured, else false.
   */
  @JsonIgnore
  public boolean isConfigured(Provider provider) {
    return type.isConfigured(PredicateParam.builder().value(value).provider(provider).build());
  }

  /** Parameter to the predicate to validate a node configuration. */
  @Builder
  @Getter
  public static class PredicateParam {
    private String value;
    private Provider provider;
  }

  /** Type of the configuration. The predicate can be minimum value comparison like ulimit. */
  public enum Type {
    NTP_SERVICE_STATUS(STRING_EQUALS.withPathSuffix("ntp_service")),
    PROMETHEUS_SPACE(GREATER_EQUAL.withPathSuffix("min_prometheus_space_mb")),
    MOUNT_POINTS(JSON_STRINGS_EQUAL.withPathSuffix("mount_points")),
    USER(STRING_EQUALS.withPathSuffix("user")),
    USER_GROUP(STRING_EQUALS.withPathSuffix("user_group")),
    HOME_DIR_SPACE(GREATER_EQUAL.withPathSuffix("min_home_dir_space_mb")),
    RAM_SIZE(GREATER_EQUAL.withPathSuffix("min_ram_size_mb")),
    INTERNET_CONNECTION(STRING_EQUALS.withPathSuffix("internet_connection")),
    CPU_CORES(GREATER_EQUAL.withPathSuffix("min_cpu_cores")),
    PROMETHEUS_NO_NODE_EXPORTER(STRING_EQUALS.withPathSuffix("prometheus_no_node_exporter")),
    TMP_DIR_SPACE(GREATER_EQUAL.withPathSuffix("min_tmp_dir_space_mb")),
    PAM_LIMITS_WRITABLE(STRING_EQUALS.withPathSuffix("pam_limits_writable")),
    PORTS(JSON_STRINGS_EQUAL.withPathSuffix("ports")),
    PYTHON_VERSION(MIN_VERSION.withPathSuffix("min_python_version"));

    // Predicate to test if a value is acceptable.
    private final Predicate<PredicateParam> predicate;

    private Type(Predicate<PredicateParam> predicate) {
      this.predicate = predicate;
    }

    public boolean isConfigured(PredicateParam value) {
      if (predicate == null) {
        return true;
      }
      return predicate.test(value);
    }
  }
}
