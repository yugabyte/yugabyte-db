// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers.handlers;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.google.common.collect.Sets;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.gflags.GFlagDetails;
import com.yugabyte.yw.common.gflags.GFlagDiffEntry;
import com.yugabyte.yw.common.gflags.GFlagsAuditPayload;
import com.yugabyte.yw.common.gflags.GFlagsUtil;
import com.yugabyte.yw.forms.GFlagsUpgradeParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Universe;
import java.io.IOException;
import java.util.ArrayList;
import java.util.Collection;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.UUID;
import java.util.stream.Collectors;
import javax.inject.Inject;
import javax.inject.Singleton;
import lombok.extern.slf4j.Slf4j;

@Singleton
@Slf4j
public class GFlagsAuditHandler {

  private final GFlagsValidationHandler gFlagsValidationHandler;

  @Inject
  public GFlagsAuditHandler(GFlagsValidationHandler gFlagsValidationHandler) {
    this.gFlagsValidationHandler = gFlagsValidationHandler;
  }

  public JsonNode constructGFlagAuditPayload(GFlagsUpgradeParams requestParams) {
    Universe universe = Universe.getOrBadRequest(requestParams.getUniverseUUID());
    Map<UUID, UniverseDefinitionTaskParams.Cluster> newVersionsOfClusters =
        requestParams.getNewVersionsOfClusters(universe);
    Map<UUID, UniverseDefinitionTaskParams.Cluster> oldVersionsOfClusters =
        universe.getUniverseDetails().clusters.stream()
            .collect(Collectors.toMap(c -> c.uuid, c -> c));
    String softwareVersion = requestParams.getPrimaryCluster().userIntent.ybSoftwareVersion;

    Map<String, GFlagsAuditPayload> auditPayload = new HashMap<>();

    for (UniverseDefinitionTaskParams.Cluster newCluster : newVersionsOfClusters.values()) {
      Map<String, String> newMasterGFlags =
          GFlagsUtil.getBaseGFlags(
              UniverseTaskBase.ServerType.MASTER, newCluster, newVersionsOfClusters.values());
      Map<String, String> newTserverGFlags =
          GFlagsUtil.getBaseGFlags(
              UniverseTaskBase.ServerType.TSERVER, newCluster, newVersionsOfClusters.values());
      UniverseDefinitionTaskParams.Cluster oldCluster = oldVersionsOfClusters.get(newCluster.uuid);
      Map<String, String> oldMasterGFlags =
          GFlagsUtil.getBaseGFlags(
              UniverseTaskBase.ServerType.MASTER, oldCluster, oldVersionsOfClusters.values());
      Map<String, String> oldTserverGFlags =
          GFlagsUtil.getBaseGFlags(
              UniverseTaskBase.ServerType.TSERVER, oldCluster, oldVersionsOfClusters.values());
      if (!Objects.equals(newMasterGFlags, oldMasterGFlags)
          || !Objects.equals(newTserverGFlags, oldTserverGFlags)) {
        GFlagsAuditPayload payload = new GFlagsAuditPayload();

        payload.master =
            generateGFlagEntries(
                oldMasterGFlags,
                newMasterGFlags,
                UniverseTaskBase.ServerType.MASTER.toString(),
                softwareVersion);
        payload.tserver =
            generateGFlagEntries(
                oldTserverGFlags,
                newTserverGFlags,
                UniverseTaskBase.ServerType.TSERVER.toString(),
                softwareVersion);

        if (newCluster.clusterType == UniverseDefinitionTaskParams.ClusterType.PRIMARY) {
          auditPayload.put("gflags", payload);
        } else if (!GFlagsUtil.areGflagsInheritedFromPrimary(newCluster)
            || !GFlagsUtil.areGflagsInheritedFromPrimary(oldCluster)) {
          auditPayload.put("readonly_cluster_gflags", payload);
        }
      }
    }
    ObjectMapper mapper = new ObjectMapper();
    return mapper.valueToTree(auditPayload);
  }

  public List<GFlagDiffEntry> generateGFlagEntries(
      Map<String, String> oldGFlags,
      Map<String, String> newGFlags,
      String serverType,
      String softwareVersion) {
    List<GFlagDiffEntry> gFlagChanges = new ArrayList<>();
    if (oldGFlags == null) {
      oldGFlags = new HashMap<>();
    }
    if (newGFlags == null) {
      newGFlags = new HashMap<>();
    }
    GFlagDiffEntry tEntry;
    Collection<String> modifiedGFlags = Sets.union(oldGFlags.keySet(), newGFlags.keySet());

    for (String gFlagName : modifiedGFlags) {
      String oldGFlagValue = oldGFlags.getOrDefault(gFlagName, null);
      String newGFlagValue = newGFlags.getOrDefault(gFlagName, null);
      if (oldGFlagValue == null || !oldGFlagValue.equals(newGFlagValue)) {
        String defaultGFlagValue = getGFlagDefaultValue(softwareVersion, serverType, gFlagName);
        tEntry = new GFlagDiffEntry(gFlagName, oldGFlagValue, newGFlagValue, defaultGFlagValue);
        gFlagChanges.add(tEntry);
      }
    }

    return gFlagChanges;
  }

  private String getGFlagDefaultValue(String softwareVersion, String serverType, String gFlagName) {
    GFlagDetails defaultGFlag;
    String defaultGFlagValue;
    try {
      defaultGFlag =
          gFlagsValidationHandler.getGFlagsMetadata(softwareVersion, serverType, gFlagName);
    } catch (IOException | PlatformServiceException e) {
      defaultGFlag = null;
    }
    defaultGFlagValue = (defaultGFlag == null) ? null : defaultGFlag.defaultValue;
    return defaultGFlagValue;
  }
}
