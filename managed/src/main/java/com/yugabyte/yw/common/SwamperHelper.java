// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.common;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.models.Universe;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.libs.Json;

import java.io.File;
import java.io.FileWriter;
import java.io.IOException;
import java.util.UUID;

@Singleton
public class SwamperHelper {
  public static final Logger LOG = LoggerFactory.getLogger(SwamperHelper.class);

  /*
     Sample targets file
    [
      {
        "targets": [ "10.129.45.47:9300", "10.129.52.122:9300", "10.129.27.0:9300"],
        "labels": {
          "export_type": "node",
          "node_prefix": "1-a-b"

        }
      },
      {
        "targets": [ "10.129.45.47:9303", "10.129.52.122:9303", "10.129.27.0:9303"],
        "labels": {
          "export_type": "collectd",
          "node_prefix": "1-a-b"
        }
      }
    ]
  */

  @Inject
  play.Configuration appConfig;


  public enum TargetType {
    NODE_EXPORT(9300),
    COLLECTD_EXPORT(9303);

    private int port;
    TargetType(int port) {
      this.port = port;
    }
  }

  public enum LabelType {
    EXPORT_TYPE,
    NODE_PREFIX
  }
  private JsonNode getLabels(TargetType targetType, Universe universe) {
    ObjectNode labels = Json.newObject();
    labels.put(LabelType.EXPORT_TYPE.toString().toLowerCase(), targetType.toString().toLowerCase());
    labels.put(LabelType.NODE_PREFIX.toString().toLowerCase(), universe.getUniverseDetails().nodePrefix);
    return labels;
  }

  private ObjectNode getSwamperTargetJson(Universe universe, TargetType targetType) {
    ObjectNode target = Json.newObject();
    ArrayNode targetNodes = Json.newArray();

    universe.getNodes().forEach((node) -> {
      if (node.isActive()) {
        targetNodes.add(node.cloudInfo.private_ip + ":" + targetType.port);
      }
    });
    target.set("targets", targetNodes);
    target.set("labels", getLabels(targetType, universe));
    return target;
  }

  private String getSwamperFile(UUID universeUUID) {
    if (appConfig.getString("yb.swamper.targetPath").isEmpty()) {
      return null;
    }

    File swamperTargetFolder = new File(appConfig.getString("yb.swamper.targetPath"));

    if (swamperTargetFolder.exists() && swamperTargetFolder.isDirectory()) {
      return swamperTargetFolder.toString() + "/" + universeUUID.toString() + ".json";
    }
    return null;
  }

  public void writeUniverseTargetJson(UUID universeUUID) {
    ArrayNode targetsJson = Json.newArray();
    Universe universe = Universe.get(universeUUID);
    targetsJson.add(getSwamperTargetJson(universe, TargetType.NODE_EXPORT));
    targetsJson.add(getSwamperTargetJson(universe, TargetType.COLLECTD_EXPORT));

    String swamperFile = getSwamperFile(universeUUID);
    if (swamperFile != null) {
      try {
        FileWriter file = new FileWriter(swamperFile);
        file.write((Json.prettyPrint(targetsJson)));
        file.flush();
        file.close();
        LOG.info("Wrote Swamper Target file: {}", swamperFile);

      } catch (IOException e) {
        LOG.error("Unable to write to Swamper Target JSON: {}", swamperFile);
        throw new RuntimeException(e.getMessage());
      }
    }
  }

  public void removeUniverseTargetJson(UUID universeUUID) {
    String swamperFile = getSwamperFile(universeUUID);
    if (swamperFile != null) {
      LOG.info("Going to delete the file... {}", swamperFile);
      File file = new File(swamperFile);

      if (file.exists()) {
        LOG.info("Deleting Swamper Target file: {}", swamperFile);
        file.delete();
      }
    }
  }
}

