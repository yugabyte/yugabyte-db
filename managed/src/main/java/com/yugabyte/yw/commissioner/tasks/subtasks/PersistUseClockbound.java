// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.common.config.ProviderConfKeys;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Universe.UniverseUpdater;
import java.util.UUID;
import java.util.concurrent.atomic.AtomicBoolean;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

public class PersistUseClockbound extends UniverseTaskBase {
  public static final Logger LOG = LoggerFactory.getLogger(PersistUseClockbound.class);

  @Inject
  public PersistUseClockbound(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  public void run() {
    try {
      LOG.info("Running {}", getName());

      Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
      AtomicBoolean useClockbound = new AtomicBoolean(true);
      for (Cluster cluster : universe.getUniverseDetails().clusters) {
        Provider provider = Provider.getOrBadRequest(UUID.fromString(cluster.userIntent.provider));
        if (cluster.userIntent.providerType == CloudType.onprem) {
          useClockbound.set(
              useClockbound.get()
                  && provider.getDetails().getCloudInfo().getOnprem().isUseClockbound());
        } else if (cluster.userIntent.providerType == CloudType.kubernetes
            || cluster.userIntent.providerType == CloudType.azu) {
          useClockbound.set(false);
        } else {
          useClockbound.set(
              useClockbound.get()
                  && confGetter.getConfForScope(
                      provider, ProviderConfKeys.configureClockboundInCloudProvisioning));
        }
      }
      LOG.debug(
          "Setting useClockbound: {} in universe: {}", useClockbound, universe.getUniverseUUID());
      UniverseUpdater updater =
          new UniverseUpdater() {
            @Override
            public void run(Universe universe) {
              UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
              // Update useClockbound to true for all clusters.
              universeDetails.getPrimaryCluster().userIntent.setUseClockbound(useClockbound.get());
              universeDetails
                  .getReadOnlyClusters()
                  .forEach(
                      (readReplica) ->
                          readReplica.userIntent.setUseClockbound(useClockbound.get()));
              universe.setUniverseDetails(universeDetails);
            }
          };
      saveUniverseDetails(updater);
    } catch (Exception e) {
      String msg = getName() + " failed with exception " + e.getMessage();
      LOG.warn(msg, e.getMessage());
      throw new RuntimeException(msg, e);
    }
  }
}
