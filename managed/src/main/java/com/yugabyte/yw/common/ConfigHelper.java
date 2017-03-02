// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.inject.Inject;
import com.yugabyte.yw.models.YugawareProperty;
import play.Application;
import play.libs.Json;
import play.libs.Yaml;

import javax.inject.Singleton;
import java.util.Collections;
import java.util.HashMap;
import java.util.Map;

@Singleton
public class ConfigHelper {
  private Application app;

  @Inject
  public ConfigHelper(Application application) { app = application; }

  public enum ConfigType {
    AWSRegionMetadata,
    AWSInstanceTypeMetadata,
    GCPRegionMetadata,
    GCPInstanceTypeMetadata,
    DockerRegionMetadata,
    DockerInstanceTypeMetadata;

    public String getConfigFile() {
      switch (this) {
        case AWSRegionMetadata:
          return "configs/aws-region-metadata.yml";
        case AWSInstanceTypeMetadata:
          // TODO: pull this directly from AWS api via ybcloud.
          return "configs/aws-instance-type-metadata.yml";
        case GCPRegionMetadata:
          return "configs/gcp-region-metadata.yml";
        case GCPInstanceTypeMetadata:
          return "configs/gcp-instance-type-metadata.yml";
        case DockerRegionMetadata:
          return "configs/docker-region-metadata.yml";
        case DockerInstanceTypeMetadata:
          return "configs/docker-instance-type-metadata.yml";
        default:
          return null;
      }
    }

    public String getDescription() {
      switch (this) {
        case AWSInstanceTypeMetadata:
          return "AWS Instance Type Metadata";
        case AWSRegionMetadata:
          return "AWS Region Metadata";
        case GCPRegionMetadata:
          return "GCP Region Metadata";
        case DockerRegionMetadata:
          return "Docker Region Metadata";
        default:
          return null;
      }
    }
  }

  public Map<String, Object> getConfig(ConfigType type) {
    YugawareProperty p = YugawareProperty.get(type.toString());
    if (p == null) return Collections.emptyMap();
    JsonNode node = p.getValue();
    if (node == null) return Collections.emptyMap();
    return Json.fromJson(node, Map.class);
  }

  public void loadConfigsToDB() {
    for (ConfigType type: ConfigType.values()) {
      Map<String, Object> config = (HashMap<String, Object>) Yaml.load(
          app.resourceAsStream(type.getConfigFile()),
          app.classloader()
      );
      YugawareProperty.addConfigProperty(type.toString(), Json.toJson(config), type.getDescription());
    }
  }
}
