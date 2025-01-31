// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.annotations.VisibleForTesting;
import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.models.YugawareProperty;
import java.io.InputStream;
import java.util.Collections;
import java.util.HashMap;
import java.util.Map;
import java.util.UUID;
import javax.inject.Singleton;
import org.apache.commons.io.FilenameUtils;
import org.apache.commons.io.IOUtils;
import org.apache.commons.lang3.math.NumberUtils;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yaml.snakeyaml.LoaderOptions;
import org.yaml.snakeyaml.Yaml;
import org.yaml.snakeyaml.constructor.CustomClassLoaderConstructor;
import play.Environment;
import play.libs.Json;

@Singleton
public class ConfigHelper {

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
    EKSKubernetesRegionMetadata(
        "EKS Kubernetes Region Metadata", "configs/kubernetes/eks-region-metadata.yml"),
    AKSKubernetesRegionMetadata(
        "AKS Kubernetes Region Metadata", "configs/kubernetes/aks-region-metadata.yml"),
    GKEKubernetesRegionMetadata(
        "GKE Kubernetes Region Metadata", "configs/kubernetes/gke-region-metadata.yml"),
    DockerRegionMetadata("Docker Region Metadata", "configs/docker-region-metadata.yml"),
    DockerInstanceTypeMetadata(null, "configs/docker-instance-type-metadata.yml"),
    SoftwareReleases("Software Releases"),
    YbcSoftwareReleases("Ybc Software Releases"),
    SoftwareVersion("Software Version"),
    YugawareMetadata("Yugaware Metadata"),
    Security("Security Level"),
    FileDataSync("Sync File System Data in the DB"),
    YBADefaultAMI("Default AMIs version for YBA");

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

  public static String getCurrentVersion(Environment environment) {

    String configFile = "version_metadata.json";
    LoaderOptions loaderOptions = new LoaderOptions();

    InputStream inputStream = environment.resourceAsStream(configFile);
    if (inputStream == null) { // version_metadata.json not found
      LOG.info(
          "{} file not found. Reading version from version.txt file",
          FilenameUtils.getName(configFile));
      Yaml yaml =
          new Yaml(new CustomClassLoaderConstructor(environment.classLoader(), loaderOptions));
      inputStream = environment.resourceAsStream("version.txt");
      try {
        return yaml.load(inputStream);
      } finally {
        IOUtils.closeQuietly(inputStream);
      }
    }
    // Method parse closes the stream.
    JsonNode jsonNode = Json.parse(inputStream);
    String buildNumber = jsonNode.get("build_number").asText();

    return jsonNode.get("version_number").asText()
        + "-"
        + (NumberUtils.isDigits(buildNumber) ? "b" : "")
        + buildNumber;
  }

  public void loadSoftwareVersiontoDB(Environment environment) {
    String version = getCurrentVersion(environment);
    loadConfigToDB(ConfigType.SoftwareVersion, ImmutableMap.of("version", version));

    // TODO: Version added to Yugaware metadata, now slowly decommission SoftwareVersion property
    Map<String, Object> ywMetadata = new HashMap<>();
    // Assign a new Yugaware UUID if not already present in the DB i.e. first install
    Object ywUUID =
        getConfig(ConfigType.YugawareMetadata).getOrDefault("yugaware_uuid", UUID.randomUUID());
    ywMetadata.put("yugaware_uuid", ywUUID);
    ywMetadata.put("version", version);
    loadConfigToDB(ConfigType.YugawareMetadata, ywMetadata);
  }

  public UUID getYugawareUUID() {
    Object ywUUID = getConfig(ConfigHelper.ConfigType.YugawareMetadata).get("yugaware_uuid");
    if (ywUUID != null) {
      return UUID.fromString(ywUUID.toString());
    }
    return null;
  }

  public void loadConfigsToDB(Environment environment) {

    LoaderOptions loaderOptions = new LoaderOptions();
    for (ConfigType type : ConfigType.values()) {
      if (type.getConfigFile() == null) {
        continue;
      }
      Yaml yaml =
          new Yaml(new CustomClassLoaderConstructor(environment.classLoader(), loaderOptions));
      Map<String, Object> config = yaml.load(environment.resourceAsStream(type.getConfigFile()));
      loadConfigToDB(type, config);
    }
  }

  public void loadConfigToDB(ConfigType type, Map<String, Object> config) {
    YugawareProperty.addConfigProperty(type.toString(), Json.toJson(config), type.getDescription());
  }
}
