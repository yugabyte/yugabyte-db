// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.supportbundle;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.google.protobuf.util.JsonFormat;
import com.yugabyte.yw.commissioner.tasks.params.SupportBundleTaskParams;
import com.yugabyte.yw.common.RedactingService;
import com.yugabyte.yw.common.RedactingService.RedactionTarget;
import com.yugabyte.yw.common.SupportBundleUtil;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.forms.SupportBundleFormData;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.Date;
import java.util.HashMap;
import java.util.Map;
import lombok.extern.slf4j.Slf4j;
import org.yb.client.GetMasterClusterConfigResponse;
import org.yb.client.YBClientApi;
import org.yb.master.CatalogEntityInfo.SysClusterConfigEntryPB;
import play.libs.Json;

/**
 * Global-level support bundle component that captures the master's {@link SysClusterConfigEntryPB}
 * and YBA's {@link com.yugabyte.yw.models.helpers.PlacementInfo} for each universe cluster.
 * Collected once per bundle (not per node) and reused by the v2 flow through the same {@code
 * ComponentType.ClusterConfig} mapping.
 */
@Slf4j
@Singleton
public class ClusterConfigComponent implements SupportBundleComponent {

  public static final String CLUSTER_CONFIG_FOLDER = "ClusterConfig";
  public static final String CLUSTER_CONFIG_FILE = "cluster_config.json";
  public static final String PLACEMENT_INFO_FILE = "placement_info.json";

  private final YBClientService ybClientService;
  private final SupportBundleUtil supportBundleUtil;

  @Inject
  public ClusterConfigComponent(
      YBClientService ybClientService, SupportBundleUtil supportBundleUtil) {
    this.ybClientService = ybClientService;
    this.supportBundleUtil = supportBundleUtil;
  }

  @Override
  public void downloadComponent(
      SupportBundleTaskParams supportBundleTaskParams,
      Customer customer,
      Universe universe,
      Path bundlePath,
      NodeDetails node)
      throws Exception {
    Path destDir = Files.createDirectories(Paths.get(bundlePath.toString(), CLUSTER_CONFIG_FOLDER));
    try {
      supportBundleUtil.saveMetadata(
          customer, destDir.toString(), buildPlacementsJson(universe), PLACEMENT_INFO_FILE);
    } catch (Exception e) {
      log.error("Error while collecting placement info for universe: {} ", universe.getName(), e);
    }
    try {
      supportBundleUtil.saveMetadata(
          customer, destDir.toString(), fetchClusterConfigJson(universe), CLUSTER_CONFIG_FILE);
    } catch (Exception e) {
      log.error(
          "Error while collecting master cluster config for universe: {} ", universe.getName(), e);
    }
  }

  @Override
  public void downloadComponentBetweenDates(
      SupportBundleTaskParams supportBundleTaskParams,
      Customer customer,
      Universe universe,
      Path bundlePath,
      Date startDate,
      Date endDate,
      NodeDetails node)
      throws Exception {
    downloadComponent(supportBundleTaskParams, customer, universe, bundlePath, node);
  }

  @Override
  public Map<String, Long> getFilesListWithSizes(
      Customer customer,
      SupportBundleFormData bundleData,
      Universe universe,
      Date startDate,
      Date endDate,
      NodeDetails node)
      throws Exception {
    Map<String, Long> res = new HashMap<>();
    long size = 0;
    try {
      size += buildPlacementsJson(universe).toPrettyString().length();
    } catch (Exception e) {
      log.warn(
          "Unable to estimate placement info size for universe {}: {}",
          universe.getName(),
          e.getMessage());
    }
    try {
      size += fetchClusterConfigJson(universe).toPrettyString().length();
    } catch (Exception e) {
      log.warn(
          "Unable to estimate master cluster config size for universe {}: {}",
          universe.getName(),
          e.getMessage());
      size += 20_000;
    }
    res.put(CLUSTER_CONFIG_FOLDER, size);
    return res;
  }

  JsonNode buildPlacementsJson(Universe universe) {
    ArrayNode clusters = Json.newArray();
    for (Cluster cluster : universe.getUniverseDetails().clusters) {
      ObjectNode clusterNode = Json.newObject();
      if (cluster.uuid != null) {
        clusterNode.put("uuid", cluster.uuid.toString());
      }
      if (cluster.clusterType != null) {
        clusterNode.put("clusterType", cluster.clusterType.name());
      }
      clusterNode.set("placementInfo", Json.toJson(cluster.placementInfo));
      clusters.add(clusterNode);
    }
    return clusters;
  }

  JsonNode fetchClusterConfigJson(Universe universe) throws Exception {
    try (YBClientApi client = ybClientService.getUniverseClient(universe)) {
      GetMasterClusterConfigResponse response = client.getMasterClusterConfig();
      if (response.hasError()) {
        throw new RuntimeException(response.errorMessage());
      }
      SysClusterConfigEntryPB.Builder builder = response.getConfig().toBuilder();
      if (builder.hasEncryptionInfo()) {
        builder.getEncryptionInfoBuilder().clearUniverseKeyRegistryEncoded();
      }
      return RedactingService.filterSecretFields(
          Json.parse(JsonFormat.printer().print(builder.build())), RedactionTarget.LOGS);
    }
  }
}
