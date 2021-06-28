// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.google.common.collect.Sets;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.UniverseDefinitionTaskBase;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.forms.ITaskParams;
import com.yugabyte.yw.models.AsyncReplicationRelationship;
import com.yugabyte.yw.models.Universe;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.cdc.CdcConsumer;
import org.yb.client.YBClient;
import org.yb.master.Master;
import play.api.Play;

import javax.inject.Inject;
import java.util.HashSet;
import java.util.Map;
import java.util.Set;
import java.util.UUID;

public class AsyncReplicationPlatformSync extends UniverseDefinitionTaskBase {
  public static final Logger LOG = LoggerFactory.getLogger(AsyncReplicationPlatformSync.class);

  // The YB client.
  public YBClientService ybService = null;

  @Inject
  protected AsyncReplicationPlatformSync(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  public void initialize(ITaskParams params) {
    super.initialize(params);
    ybService = Play.current().injector().instanceOf(YBClientService.class);
  }

  @Override
  public void run() {
    Universe targetUniverse = Universe.getOrBadRequest(taskParams().universeUUID);
    String masterHostPorts = targetUniverse.getMasterAddresses();
    String certificate = targetUniverse.getCertificateNodetoNode();
    YBClient client = null;

    try {
      LOG.info("Running {}", getName());
      client = ybService.getClient(masterHostPorts, certificate);

      Master.SysClusterConfigEntryPB config = client.getMasterClusterConfig().getConfig();
      // Get all AsyncReplicationRelationship objects represented in the cluster config
      Set<AsyncReplicationRelationship> configRelationships =
          getConfigRelationships(config, targetUniverse);

      // Delete any AsyncReplicationRelationship objects that aren't in the cluster config
      Sets.difference(targetUniverse.targetAsyncReplicationRelationships, configRelationships)
          .forEach(AsyncReplicationRelationship::delete);
    } catch (Exception e) {
      LOG.error("{} hit error : {}", getName(), e.getMessage());
      throw new RuntimeException(e);
    } finally {
      ybService.closeClient(client, masterHostPorts);
    }
  }

  // Ensure that async replication relationships found in cluster config all exist in table
  // (we create a table entry if necessary), and return them all.
  private Set<AsyncReplicationRelationship> getConfigRelationships(
      Master.SysClusterConfigEntryPB config, Universe targetUniverse) {
    Set<AsyncReplicationRelationship> relationships = new HashSet<>();

    // Target universe can receive data from multiple source universes
    Map<String, CdcConsumer.ProducerEntryPB> sourceUniverseMap =
        config.getConsumerRegistry().getProducerMapMap();

    sourceUniverseMap.forEach(
        (key, value) -> {
          UUID sourceUniverseUUID = UUID.fromString(key);
          // Tables being replicated
          Map<String, CdcConsumer.StreamEntryPB> tableMap = value.getStreamMapMap();

          tableMap
              .values()
              .forEach(
                  tableEntry -> {
                    String targetTableID = tableEntry.getConsumerTableId();
                    String sourceTableID = tableEntry.getProducerTableId();

                    // Lookup the corresponding AsyncReplicationRelationship object if it exists
                    AsyncReplicationRelationship relationship =
                        AsyncReplicationRelationship.get(
                            sourceUniverseUUID,
                            sourceTableID,
                            targetUniverse.universeUUID,
                            targetTableID);

                    if (relationship == null) {
                      // No corresponding relationship found in table: we must create one
                      Universe sourceUniverse = Universe.getOrBadRequest(sourceUniverseUUID);
                      relationship =
                          AsyncReplicationRelationship.create(
                              sourceUniverse, sourceTableID, targetUniverse, targetTableID, true);
                    }

                    // Add the corresponding relationship to our set
                    relationships.add(relationship);
                  });
        });

    return relationships;
  }
}
