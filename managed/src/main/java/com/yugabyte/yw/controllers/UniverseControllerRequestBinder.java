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

import com.fasterxml.jackson.core.JsonProcessingException;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UpgradeTaskParams;
import com.yugabyte.yw.models.helpers.CommonUtils;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import play.libs.Json;
import play.mvc.Http;

/**
 * This code needs to be fixed (ideally purged) as it is doing lot of raw json manipulation. As a
 * first step to that cleanup we have moved it out of Universe controller to keep the controller
 * sane.
 */
public class UniverseControllerRequestBinder {

  static <T extends UniverseDefinitionTaskParams> T bindFormDataToTaskParams(
      Http.Context ctx, Http.Request request, Class<T> paramType) {
    ObjectMapper mapper = Json.mapper();
    // Notes about code deleted from here:
    // 1 communicationPorts and expectedUniverseVersion - See UniverseTaskParams.BaseConverter
    //    We also changed the expectedUniverseVersion to non-primitive so that this logic can be
    //    pushed to post deserialization.
    // 2. encryptionAtRestConfig - See @JsonAlias annotations on the EncryptionAtRestConfig props
    // 3. cluster.userIntent gflags list to maps - TODO
    try {
      ObjectNode formData = (ObjectNode) request.body().asJson();
      List<UniverseDefinitionTaskParams.Cluster> clusters = mapClustersInParams(formData, true);
      T taskParams = Json.mapper().treeToValue(formData, paramType);
      taskParams.clusters = clusters;
      taskParams.creatingUser = CommonUtils.getUserFromContext(ctx);
      return taskParams;
    } catch (JsonProcessingException exception) {
      throw new PlatformServiceException(
          BAD_REQUEST, "JsonProcessingException parsing request body: " + exception.getMessage());
    }
  }

  static <T extends UpgradeTaskParams> T bindFormDataToUpgradeTaskParams(
      Http.Context ctx, Http.Request request, Class<T> paramType) {
    try {
      ObjectNode formData = (ObjectNode) request.body().asJson();
      List<UniverseDefinitionTaskParams.Cluster> clusters = mapClustersInParams(formData, false);
      T taskParams = Json.mapper().treeToValue(formData, paramType);
      taskParams.clusters = clusters;
      taskParams.creatingUser = CommonUtils.getUserFromContext(ctx);
      return taskParams;
    } catch (JsonProcessingException exception) {
      throw new PlatformServiceException(
          BAD_REQUEST, "JsonProcessingException parsing request body: " + exception.getMessage());
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
        ArrayNode flagNodeArray = (ArrayNode) formNodeList;
        for (JsonNode gflagNode : flagNodeArray) {
          if (gflagNode.has("name")) {
            gflagMap.put(gflagNode.get("name").asText(), gflagNode.get("value").asText());
          }
        }
      } else if (formNodeList instanceof ObjectNode) {
        gflagMap = Json.fromJson(formNodeList, Map.class);
      }
    }
    return gflagMap;
  }
}
