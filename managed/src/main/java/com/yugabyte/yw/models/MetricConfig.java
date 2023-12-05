// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models;

import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_ONLY;
import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_WRITE;

import com.fasterxml.jackson.databind.JsonNode;
import io.ebean.Finder;
import io.ebean.Model;
import io.ebean.annotation.DbJson;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import jakarta.persistence.Column;
import jakarta.persistence.Entity;
import jakarta.persistence.Id;
import java.util.List;
import java.util.Map;
import java.util.stream.Collectors;
import lombok.Data;
import lombok.EqualsAndHashCode;
import play.libs.Json;

@ApiModel(description = "Metric configuration key and value for Prometheus")
@Entity
@Data
@EqualsAndHashCode(callSuper = false)
public class MetricConfig extends Model {

  public static final String METRICS_CONFIG_PATH = "metric/metrics.yml";

  @ApiModelProperty(value = "Metrics configuration key", accessMode = READ_ONLY)
  @Id
  @Column(name = "config_key", length = 100)
  private String key;

  @ApiModelProperty(value = "Metrics configuration value", accessMode = READ_WRITE)
  @Column(nullable = false)
  @DbJson
  private MetricConfigDefinition config;

  public static final Finder<String, MetricConfig> find =
      new Finder<String, MetricConfig>(MetricConfig.class) {};

  /**
   * returns metric config for the given key
   *
   * @param configKey
   * @return MetricConfig
   */
  public static MetricConfig get(String configKey) {
    return MetricConfig.find.byId(configKey);
  }

  /**
   * Create a new instance of metric config for given config key and config data
   *
   * @param configKey
   * @param configData
   * @return returns a instance of MetricConfig
   */
  public static MetricConfig create(String configKey, JsonNode configData) {
    MetricConfig metricConfig = new MetricConfig();
    metricConfig.setKey(configKey);
    metricConfig.setConfig(Json.fromJson(configData, MetricConfigDefinition.class));
    return metricConfig;
  }

  /**
   * Loads the configs into the db, if the config already exists it would update that if not it will
   * create new config.
   *
   * @param configs
   */
  public static void loadConfig(Map<String, Object> configs) {
    List<String> currentConfigs =
        MetricConfig.find.all().stream().map(MetricConfig::getKey).collect(Collectors.toList());

    for (Map.Entry<String, Object> configData : configs.entrySet()) {
      MetricConfig metricConfig =
          MetricConfig.create(configData.getKey(), Json.toJson(configData.getValue()));
      // Check if the config already exists if so, let's update it or else,
      // we will create new one.
      if (currentConfigs.contains(metricConfig.getKey())) {
        metricConfig.update();
      } else {
        metricConfig.save();
      }
    }
  }
}
