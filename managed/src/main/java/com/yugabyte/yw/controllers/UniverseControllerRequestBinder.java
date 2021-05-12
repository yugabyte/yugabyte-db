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

import com.fasterxml.jackson.core.JsonProcessingException;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.YWServiceException;
import com.yugabyte.yw.common.kms.util.AwsEARServiceUtil;
import com.yugabyte.yw.forms.DiskIncreaseFormData;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.forms.UpgradeParams;
import com.yugabyte.yw.models.helpers.NodeDetails;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.libs.Json;

import java.util.*;

import static play.mvc.Http.Status.BAD_REQUEST;

/**
 * This code needs to be fixed (ideally purged) as it is doing lot of raw json manipulation.
 * As a first step to that cleanup we have moved it out of Universe controller to
 * keep the controller sane.
 */
public class UniverseControllerRequestBinder {
  private static final Logger LOG = LoggerFactory.getLogger(UniverseControllerRequestBinder.class);

  static UniverseDefinitionTaskParams bindFormDataToTaskParams(ObjectNode formData,
                                                               UUID customerUUID) {
    return bindFormDataToTaskParams(formData, false);
  }

  static UniverseDefinitionTaskParams bindFormDataToTaskParams(
    ObjectNode formData, boolean isUpgrade) {
    return bindFormDataToTaskParams(formData, isUpgrade, false);
  }

  static UniverseDefinitionTaskParams bindFormDataToTaskParams(ObjectNode formData,
                                                               boolean isUpgrade,
                                                               boolean isDisk) {
    ObjectMapper mapper = new ObjectMapper();
    ArrayNode nodeSetArray = null;
    UniverseTaskParams.EncryptionAtRestConfig encryptionConfig =
      new UniverseTaskParams.EncryptionAtRestConfig();
    UniverseTaskParams.CommunicationPorts communicationPorts =
      new UniverseTaskParams.CommunicationPorts();
    int expectedUniverseVersion = -1;
    if (formData.get("nodeDetailsSet") != null && formData.get("nodeDetailsSet").size() > 0) {
      nodeSetArray = (ArrayNode) formData.get("nodeDetailsSet");
      formData.remove("nodeDetailsSet");
    }
    if (formData.get("expectedUniverseVersion") != null) {
      expectedUniverseVersion = formData.get("expectedUniverseVersion").asInt();
    }

    JsonNode config = formData.get("encryptionAtRestConfig");
    if (config != null) {
      formData.remove("encryptionAtRestConfig");

      if (config.get("configUUID") != null) {
        encryptionConfig.kmsConfigUUID = UUID.fromString(config.get("configUUID").asText());

        if (config.get("type") != null) {
          encryptionConfig.type = Enum.valueOf(AwsEARServiceUtil.KeyType.class,
            config.get("type").asText());
        }

        if (config.get("key_op") != null) {
          encryptionConfig.opType =
            Enum.valueOf(UniverseTaskParams.EncryptionAtRestConfig.OpType.class, config.get(
              "key_op").asText());
        }
      }
    }

    try {
      JsonNode communicationPortsJson = formData.get("communicationPorts");

      if (communicationPortsJson != null) {
        formData.remove("communicationPorts");
        communicationPorts = mapper.treeToValue(communicationPortsJson,
          UniverseTaskParams.CommunicationPorts.class);
      }

      UniverseDefinitionTaskParams taskParams;
      List<UniverseDefinitionTaskParams.Cluster> clusters = mapClustersInParams(formData);
      if (isUpgrade) {
        taskParams = mapper.treeToValue(formData, UpgradeParams.class);
      } else if (isDisk) {
        taskParams = mapper.treeToValue(formData, DiskIncreaseFormData.class);
      } else {
        taskParams = mapper.treeToValue(formData, UniverseDefinitionTaskParams.class);
      }
      taskParams.clusters = clusters;
      if (nodeSetArray != null) {
        taskParams.nodeDetailsSet = new HashSet<>();
        for (JsonNode nodeItem : nodeSetArray) {
          NodeDetails nodeDetail = mapper.treeToValue(nodeItem, NodeDetails.class);
          UniverseTaskParams.CommunicationPorts.setCommunicationPorts(
            communicationPorts, nodeDetail);

          taskParams.nodeDetailsSet.add(nodeDetail);
        }
      }
      taskParams.expectedUniverseVersion = expectedUniverseVersion;
      taskParams.encryptionAtRestConfig = encryptionConfig;
      taskParams.communicationPorts = communicationPorts;
      return taskParams;
    } catch (JsonProcessingException exception) {
      throw new YWServiceException(BAD_REQUEST,
        "JsonProcessingException parsing request body: " + exception.getMessage());
    }
  }

  /**
   * The ObjectMapper fails to properly map the array of clusters. Given form data, this method
   * does the translation. Each cluster in the array of clusters is expected to conform to the
   * Cluster definitions, especially including the UserIntent.
   *
   * @param formData Parent FormObject for the clusters array.
   * @return A list of deserialized clusters.
   */
  private static List<UniverseDefinitionTaskParams.Cluster> mapClustersInParams(
    ObjectNode formData) throws JsonProcessingException {
    ArrayNode clustersJsonArray = (ArrayNode) formData.get("clusters");
    if (clustersJsonArray == null) {
      throw new YWServiceException(BAD_REQUEST, "clusters: This field is required");
    }
    ArrayNode newClustersJsonArray = Json.newArray();
    List<UniverseDefinitionTaskParams.Cluster> clusters = new ArrayList<>();
    for (int i = 0; i < clustersJsonArray.size(); ++i) {
      ObjectNode clusterJson = (ObjectNode) clustersJsonArray.get(i);
      if (clusterJson.has("regions")) {
        clusterJson.remove("regions");
      }
      ObjectNode userIntent = (ObjectNode) clusterJson.get("userIntent");
      if (userIntent == null) {
        throw new YWServiceException(BAD_REQUEST, "userIntent: This field is required");
      }
      // TODO: (ram) add tests for all these.
      Map<String, String> masterGFlagsMap = serializeGFlagListToMap(userIntent, "masterGFlags");
      Map<String, String> tserverGFlagsMap = serializeGFlagListToMap(userIntent, "tserverGFlags");
      Map<String, String> instanceTags = serializeGFlagListToMap(userIntent, "instanceTags");
      clusterJson.set("userIntent", userIntent);
      newClustersJsonArray.add(clusterJson);
      UniverseDefinitionTaskParams.Cluster cluster = (new ObjectMapper()).treeToValue(clusterJson
        , UniverseDefinitionTaskParams.Cluster.class);
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
    JsonNode formNodeList = formNode.get(listType);
    if (formNodeList != null && formNodeList.isArray()) {
      ArrayNode flagNodeArray = (ArrayNode) formNodeList;
      for (JsonNode gflagNode : flagNodeArray) {
        if (gflagNode.has("name")) {
          gflagMap.put(gflagNode.get("name").asText(), gflagNode.get("value").asText());
        }
      }
    }
    formNode.remove(listType);
    return gflagMap;
  }
}
