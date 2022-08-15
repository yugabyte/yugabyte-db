// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.annotations.VisibleForTesting;
import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.models.YugawareProperty;
import java.io.InputStream;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.stream.Collectors;
import java.util.stream.Stream;
import javax.inject.Singleton;
import org.apache.commons.io.FilenameUtils;
import org.apache.commons.lang3.math.NumberUtils;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yaml.snakeyaml.Yaml;
import org.yaml.snakeyaml.constructor.CustomClassLoaderConstructor;
import play.Application;
import play.libs.Json;

@Singleton
public class ConfigHelper {

  @Inject RuntimeConfigFactory runtimeConfigFactory;

  private static final List<String> AWS_INSTANCE_PREFIXES_SUPPORTED =
      ImmutableList.of("m3.", "c5.", "c5d.", "c4.", "c3.", "i3.");
  private static final List<String> GRAVITON_AWS_INSTANCE_PREFIXES_SUPPORTED =
      ImmutableList.of("m6g.", "c6gd.", "c6g.", "t4g.");
  private static final List<String> CLOUD_AWS_INSTANCE_PREFIXES_SUPPORTED =
      ImmutableList.of(
          "m3.", "c5.", "c5d.", "c4.", "c3.", "i3.", "t2.", "t3.", "t4g.", "m6i.", "m5.");

  public List<String> getAWSInstancePrefixesSupported() {
    if (runtimeConfigFactory.globalRuntimeConf().getBoolean("yb.cloud.enabled")) {
      return CLOUD_AWS_INSTANCE_PREFIXES_SUPPORTED;
    }
    return Stream.concat(
            AWS_INSTANCE_PREFIXES_SUPPORTED.stream(),
            GRAVITON_AWS_INSTANCE_PREFIXES_SUPPORTED.stream())
        .collect(Collectors.toList());
  }

  public static final Logger LOG = LoggerFactory.getLogger(ConfigHelper.class);

  public enum ConfigType {
    // TODO: investigate why many of these are null description. Is it intended or a bug?
    // Hopefully this less error prone way will prevent any bugs in future.
    AWSRegionMetadata("AWS Region Metadata", "configs/aws-region-metadata.yml"),
    AWSInstanceTypeMetadata("AWS Instance Type Metadata", "configs/aws-instance-type-metadata.yml"),
    GCPRegionMetadata("GCP Region Metadata", "configs/gcp-region-metadata.yml"),
    GCPInstanceTypeMetadata(null, "configs/gcp-instance-type-metadata.yml"),
    AZURegionMetadata("Azure Region Metadata", "configs/azu-region-metadata.yml"),
    AZUInstanceTypeMetadata(null),
    DockerRegionMetadata("Docker Region Metadata", "configs/docker-region-metadata.yml"),
    DockerInstanceTypeMetadata(null, "configs/docker-instance-type-metadata.yml"),
    SoftwareReleases("Software Releases"),
    YbcSoftwareReleases("Ybc Software Releases"),
    SoftwareVersion("Software Version"),
    YugawareMetadata("Yugaware Metadata"),
    Security("Security Level");

    private final String description;
    private final String configFile;

    ConfigType(String description, String configFile) {
      this.description = description;
      this.configFile = configFile;
    }

    ConfigType(String description) {
      this(description, null);
    }

    @VisibleForTesting
    String getConfigFile() {
      return configFile;
    }

    public String getDescription() {
      return this.description;
    }
  }

  public Map<String, Object> getConfig(ConfigType type) {
    YugawareProperty p = YugawareProperty.get(type.toString());
    if (p == null) return Collections.emptyMap();
    JsonNode node = p.getValue();
    if (node == null) return Collections.emptyMap();
    return Json.fromJson(node, Map.class);
  }

  public Map<String, Object> getRegionMetadata(Common.CloudType type) {
    return type.getRegionMetadataConfigType().map(this::getConfig).orElse(Collections.emptyMap());
  }

  public static String getCurrentVersion(Application app) {

    String configFile = "version_metadata.json";
    InputStream inputStream = app.resourceAsStream(configFile);
    if (inputStream == null) { // version_metadata.json not found
      LOG.info(
          "{} file not found. Reading version from version.txt file",
          FilenameUtils.getName(configFile));
      Yaml yaml = new Yaml(new CustomClassLoaderConstructor(app.classloader()));
      String version = yaml.load(app.resourceAsStream("version.txt"));
      return version;
    }
    JsonNode jsonNode = Json.parse(inputStream);
    String buildNumber = jsonNode.get("build_number").asText();
    String version =
        jsonNode.get("version_number").asText()
            + "-"
            + (NumberUtils.isDigits(buildNumber) ? "b" : "")
            + buildNumber;

    return version;
  }

  public void loadSoftwareVersiontoDB(Application app) {
    String version = getCurrentVersion(app);
    loadConfigToDB(ConfigType.SoftwareVersion, ImmutableMap.of("version", version));

    // TODO: Version added to Yugaware metadata, now slowly decomission SoftwareVersion property
    Map<String, Object> ywMetadata = new HashMap<>();
    // Assign a new Yugaware UUID if not already present in the DB i.e. first install
    Object ywUUID =
        getConfig(ConfigType.YugawareMetadata).getOrDefault("yugaware_uuid", UUID.randomUUID());
    ywMetadata.put("yugaware_uuid", ywUUID);
    ywMetadata.put("version", version);
    loadConfigToDB(ConfigType.YugawareMetadata, ywMetadata);
  }

  public void loadConfigsToDB(Application app) {
    for (ConfigType type : ConfigType.values()) {
      if (type.getConfigFile() == null) {
        continue;
      }
      Yaml yaml = new Yaml(new CustomClassLoaderConstructor(app.classloader()));
      Map<String, Object> config = yaml.load(app.resourceAsStream(type.getConfigFile()));
      loadConfigToDB(type, config);
    }
  }

  public void loadConfigToDB(ConfigType type, Map<String, Object> config) {
    YugawareProperty.addConfigProperty(type.toString(), Json.toJson(config), type.getDescription());
  }
}
