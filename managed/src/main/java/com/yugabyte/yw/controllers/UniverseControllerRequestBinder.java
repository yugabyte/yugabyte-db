/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.controllers;

import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.fasterxml.jackson.core.JsonProcessingException;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.gflags.SpecificGFlags;
import com.yugabyte.yw.common.inject.StaticInjectorHolder;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent.K8SNodeResourceSpec;
import com.yugabyte.yw.forms.UpgradeTaskParams;
import com.yugabyte.yw.models.InstanceType;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CommonUtils;
import java.io.IOException;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.function.Consumer;
import lombok.extern.slf4j.Slf4j;
import play.libs.Json;
import play.mvc.Http;

/**
 * This code needs to be fixed (ideally purged) as it is doing lot of raw json manipulation. As a
 * first step to that cleanup we have moved it out of Universe controller to keep the controller
 * sane.
 */
@Slf4j
public class UniverseControllerRequestBinder {

  static <T extends UniverseDefinitionTaskParams> T bindFormDataToTaskParams(
      Http.Request request, Class<T> paramType) {
    ObjectMapper mapper = Json.mapper();
    // Notes about code deleted from here:
    // 1 communicationPorts and expectedUniverseVersion - See UniverseTaskParams.BaseConverter
    //    We also changed the expectedUniverseVersion to non-primitive so that this logic can be
    //    pushed to post deserialization.
    // 2. encryptionAtRestConfig - See @JsonAlias annotations on the EncryptionAtRestConfig props
    // 3. cluster.userIntent gflags list to maps - TODO

    // This is only for until we need the config switch for Usek8sCustomResources.
    // Once we turn turn default we can remove this.
    // Also eventually we will deprecate instanceType for k8s.
    RuntimeConfGetter runtimeConfGetter =
        StaticInjectorHolder.injector().instanceOf(RuntimeConfGetter.class);

    try {
      ObjectNode formData = (ObjectNode) request.body().asJson();
      List<UniverseDefinitionTaskParams.Cluster> clusters = mapClustersInParams(formData, true);
      T taskParams = Json.mapper().treeToValue(formData, paramType);

      taskParams.clusters = clusters;
      taskParams.creatingUser = CommonUtils.getUserFromContext();
      taskParams.platformUrl = request.host();
      if (UniverseDefinitionTaskParams.class.isAssignableFrom(paramType)) {
        // We can get rid of this if when we default to new style resource spec.
        for (Cluster cluster : taskParams.clusters) {
          UserIntent ui = cluster.userIntent;
          if (ui.providerType == CloudType.kubernetes) {
            if (ui.instanceType != null) {
              if (runtimeConfGetter.getGlobalConf(GlobalConfKeys.usek8sCustomResources)) {
                InstanceType instanceType =
                    InstanceType.getOrBadRequest(UUID.fromString(ui.provider), ui.instanceType);
                // set K8s resource spec from instance type data.
                ui.masterK8SNodeResourceSpec = new K8SNodeResourceSpec();
                ui.tserverK8SNodeResourceSpec = new K8SNodeResourceSpec();
                ui.masterK8SNodeResourceSpec.memoryGib = instanceType.getMemSizeGB();
                ui.masterK8SNodeResourceSpec.cpuCoreCount = instanceType.getNumCores();
                ui.tserverK8SNodeResourceSpec.memoryGib = instanceType.getMemSizeGB();
                ui.tserverK8SNodeResourceSpec.cpuCoreCount = instanceType.getNumCores();
              }
            }
          }
        }
      }

      return taskParams;
    } catch (JsonProcessingException exception) {
      throw new PlatformServiceException(
          BAD_REQUEST, "JsonProcessingException parsing request body: " + exception.getMessage());
    }
  }

  static <T extends UpgradeTaskParams> T bindFormDataToUpgradeTaskParams(
      Http.Request request, Class<T> paramType, Universe universe) {
    try {
      ObjectNode formData = (ObjectNode) request.body().asJson();
      ArrayNode clustersJson = (ArrayNode) formData.get("clusters");
      List<UniverseDefinitionTaskParams.Cluster> clusters = new ArrayList<>();
      if (clustersJson == null || clustersJson.size() == 0) {
        log.debug("No clusters provided, taking current clusters");
        clusters = new ArrayList<>(universe.getUniverseDetails().clusters);
      } else {
        for (JsonNode clusterJson : clustersJson) {
          ObjectNode userIntent = (ObjectNode) clusterJson.get("userIntent");
          JsonNode masterGFlagsNode = null;
          JsonNode tserverGFlagsNode = null;
          JsonNode instanceTagsNode = null;
          JsonNode specificGFlags = null;
          JsonNode userIntentOverrides = null;
          if (userIntent != null) {
            masterGFlagsNode = userIntent.remove("masterGFlags");
            tserverGFlagsNode = userIntent.remove("tserverGFlags");
            instanceTagsNode = userIntent.remove("instanceTags");
            specificGFlags = userIntent.remove("specificGFlags");
            userIntentOverrides = userIntent.remove("userIntentOverrides");
          }
          UniverseDefinitionTaskParams.Cluster currentCluster;
          if (clusterJson.has("uuid")) {
            UUID uuid = UUID.fromString(clusterJson.get("uuid").asText());
            currentCluster = universe.getCluster(uuid);
            if (currentCluster == null) {
              throw new IllegalArgumentException(
                  String.format(
                      "Cluster %s is not found in universe %s", uuid, universe.getUniverseUUID()));
            }
          } else {
            JsonNode clusterType = clusterJson.get("clusterType");
            if (clusterType == null) {
              throw new IllegalArgumentException(
                  String.format(
                      "Unknown cluster in request for universe %s", universe.getUniverseUUID()));
            }
            if (clusterType
                .asText()
                .equals(UniverseDefinitionTaskParams.ClusterType.PRIMARY.toString())) {
              currentCluster = universe.getUniverseDetails().getPrimaryCluster();
            } else {
              if (universe.getUniverseDetails().getReadOnlyClusters().size() != 1) {
                throw new IllegalArgumentException(
                    String.format(
                        "Cannot choose readonly cluster in universe %s (cluster type %s)",
                        universe.getUniverseUUID(), clusterType.asText()));
              }
              currentCluster = universe.getUniverseDetails().getReadOnlyClusters().get(0);
            }
          }

          JsonNode currentClusterJson = Json.mapper().valueToTree(currentCluster);
          CommonUtils.deepMerge(currentClusterJson, clusterJson);
          UniverseDefinitionTaskParams.Cluster cluster =
              Json.mapper()
                  .treeToValue(currentClusterJson, UniverseDefinitionTaskParams.Cluster.class);

          checkAndAddMapField(masterGFlagsNode, gflags -> cluster.userIntent.masterGFlags = gflags);
          checkAndAddMapField(
              tserverGFlagsNode, gflags -> cluster.userIntent.tserverGFlags = gflags);
          checkAndAddMapField(instanceTagsNode, tags -> cluster.userIntent.instanceTags = tags);
          if (specificGFlags != null) {
            cluster.userIntent.specificGFlags = Json.fromJson(specificGFlags, SpecificGFlags.class);
          }
          if (userIntentOverrides != null) {
            cluster.userIntent.setUserIntentOverrides(
                Json.fromJson(
                    userIntentOverrides, UniverseDefinitionTaskParams.UserIntentOverrides.class));
          }
          clusters.add(cluster);
        }
      }
      T taskParams = mergeWithUniverse(formData, universe, paramType);
      taskParams.clusters = clusters;
      taskParams.creatingUser = CommonUtils.getUserFromContext();
      if (formData.has("runOnlyPrechecks")) {
        taskParams.runOnlyPrechecks = formData.get("runOnlyPrechecks").booleanValue();
      }

      return taskParams;
    } catch (JsonProcessingException exception) {
      throw new PlatformServiceException(
          BAD_REQUEST, "JsonProcessingException parsing request body: " + exception.getMessage());
    }
  }

  public static <T extends UniverseDefinitionTaskParams> T mergeWithUniverse(
      T params, Universe universe, Class<? extends T> paramsClass) {
    try {
      ObjectNode paramsJson = Json.mapper().valueToTree(params);
      if (params.clusters != null && params.clusters.isEmpty()) {
        paramsJson.remove("clusters");
      }
      T result = mergeWithUniverse(paramsJson, universe, paramsClass);
      return result;
    } catch (JsonProcessingException e) {
      throw new PlatformServiceException(
          BAD_REQUEST, "JsonProcessingException parsing request body: " + e.getMessage());
    }
  }

  private static <T extends UniverseDefinitionTaskParams> T mergeWithUniverse(
      ObjectNode paramsJson, Universe universe, Class<T> paramsClass)
      throws JsonProcessingException {
    // Merging with universe details.
    ObjectNode universeDetailsNode = Json.mapper().valueToTree(universe.getUniverseDetails());
    CommonUtils.deepMerge(universeDetailsNode, paramsJson);
    universeDetailsNode.remove("targetXClusterConfigs");
    universeDetailsNode.remove("sourceXClusterConfigs");
    T result = Json.mapper().treeToValue(universeDetailsNode, paramsClass);
    if (!paramsClass.equals(result.getClass())) {
      throw new IllegalStateException(
          "Expected " + paramsClass + " but deserialized to " + result.getClass());
    }
    result.setUniverseUUID(universe.getUniverseUUID());
    result.expectedUniverseVersion = universe.getVersion();
    if (universe.isYbcEnabled()) {
      result.installYbc = true;
      result.setEnableYbc(true);
      result.setYbcSoftwareVersion(universe.getUniverseDetails().getYbcSoftwareVersion());
    }
    return result;
  }

  private static void checkAndAddMapField(
      JsonNode serializedValue, Consumer<Map<String, String>> setter) {
    if (serializedValue == null) {
      return;
    }
    if (serializedValue.isArray()) {
      setter.accept(mapFromKeyValueArray((ArrayNode) serializedValue));
    } else if (serializedValue.isObject()) {
      setter.accept(Json.fromJson(serializedValue, Map.class));
    }
  }

  // Parses params into targetClass and returns that object.
  public static <T> T deepCopy(UniverseDefinitionTaskParams params, Class<T> targetClass) {
    try {
      ObjectMapper mapper = new ObjectMapper();
      return mapper.readValue(mapper.writeValueAsString(params), targetClass);
    } catch (IOException e) {
      String errMsg = "Serialization/Deserialization of universe details failed.";
      log.error(
          String.format(
              "Error in serializing/deserializing UniverseDefinitonTaskParams into %s "
                  + "for universe: %s, UniverseDetails: %s",
              targetClass, params.getUniverseUUID(), CommonUtils.maskObject(params)),
          e);
      throw new PlatformServiceException(INTERNAL_SERVER_ERROR, errMsg);
    }
  }

  /**
   * The ObjectMapper fails to properly map the array of clusters. Given form data, this method does
   * the translation. Each cluster in the array of clusters is expected to conform to the Cluster
   * definitions, especially including the UserIntent.
   *
   * @param formData Parent FormObject for the clusters array.
   * @return A list of deserialized clusters.
   */
  private static List<UniverseDefinitionTaskParams.Cluster> mapClustersInParams(
      ObjectNode formData, boolean failIfNotPresent) throws JsonProcessingException {
    ArrayNode clustersJsonArray = (ArrayNode) formData.get("clusters");
    if (clustersJsonArray == null) {
      if (failIfNotPresent) {
        throw new PlatformServiceException(BAD_REQUEST, "clusters: This field is required");
      } else {
        return new ArrayList<>();
      }
    }

    ArrayNode newClustersJsonArray = Json.newArray();
    List<UniverseDefinitionTaskParams.Cluster> clusters = new ArrayList<>();
    for (int i = 0; i < clustersJsonArray.size(); ++i) {
      ObjectNode clusterJson = (ObjectNode) clustersJsonArray.get(i);
      ObjectNode userIntent = (ObjectNode) clusterJson.get("userIntent");
      if (userIntent == null) {
        if (failIfNotPresent) {
          throw new PlatformServiceException(BAD_REQUEST, "userIntent: This field is required");
        } else {
          newClustersJsonArray.add(clusterJson);
          UniverseDefinitionTaskParams.Cluster cluster =
              (new ObjectMapper())
                  .treeToValue(clusterJson, UniverseDefinitionTaskParams.Cluster.class);
          clusters.add(cluster);
          continue;
        }
      }

      // TODO: (ram) add tests for all these.
      Map<String, String> masterGFlagsMap = serializeGFlagListToMap(userIntent, "masterGFlags");
      Map<String, String> tserverGFlagsMap = serializeGFlagListToMap(userIntent, "tserverGFlags");
      Map<String, String> instanceTags = serializeGFlagListToMap(userIntent, "instanceTags");
      clusterJson.set("userIntent", userIntent);
      newClustersJsonArray.add(clusterJson);
      UniverseDefinitionTaskParams.Cluster cluster =
          (new ObjectMapper()).treeToValue(clusterJson, UniverseDefinitionTaskParams.Cluster.class);
      cluster.userIntent.masterGFlags = masterGFlagsMap;
      cluster.userIntent.tserverGFlags = tserverGFlagsMap;
      cluster.userIntent.instanceTags = instanceTags;
      clusters.add(cluster);
    }
    formData.set("clusters", newClustersJsonArray);

    return clusters;
  }

  /**
   * Method serializes the GFlag ObjectNode into a Map and then deletes it from its parent node.
   *
   * @param formNode Parent FormObject for the GFlag Node.
   * @param listType Type of GFlag object
   * @return Serialized JSON array into Map
   */
  private static Map<String, String> serializeGFlagListToMap(ObjectNode formNode, String listType) {
    Map<String, String> gflagMap = new HashMap<>();
    JsonNode formNodeList = formNode.remove(listType);
    if (formNodeList != null) {
      if (formNodeList.isArray()) {
        gflagMap = mapFromKeyValueArray((ArrayNode) formNodeList);
      } else if (formNodeList instanceof ObjectNode) {
        gflagMap = Json.fromJson(formNodeList, Map.class);
      }
    }
    return gflagMap;
  }

  private static Map<String, String> mapFromKeyValueArray(ArrayNode flagNodeArray) {
    Map<String, String> gflagMap = new HashMap<>();
    for (JsonNode gflagNode : flagNodeArray) {
      if (gflagNode.has("name")) {
        gflagMap.put(gflagNode.get("name").asText(), gflagNode.get("value").asText());
      }
    }
    return gflagMap;
  }
}
