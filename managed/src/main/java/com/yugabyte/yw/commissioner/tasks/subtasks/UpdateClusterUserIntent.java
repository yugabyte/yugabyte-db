package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Universe.UniverseUpdater;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.HashMap;
import java.util.Map;
import java.util.UUID;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class UpdateClusterUserIntent extends UniverseTaskBase {

  @Inject
  protected UpdateClusterUserIntent(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  public static class Params extends UniverseDefinitionTaskParams {
    public Map<String, UUID> nodeToImageBundleMap;
  }

  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public String getName() {
    return super.getName() + "(" + taskParams().getUniverseUUID() + ")";
  }

  @Override
  public void run() {
    try {
      log.info("Running {}", getName());
      Map<UUID, Map<UUID, UUID>> bundleMapByClusterUUID = new HashMap<>();
      Universe universe = getUniverse();

      taskParams()
          .nodeToImageBundleMap
          .forEach(
              (nodeName, imageBundleUUID) -> {
                NodeDetails node = universe.getNode(nodeName);
                UniverseDefinitionTaskParams.Cluster cluster =
                    universe.getCluster(node.placementUuid);
                Map<UUID, UUID> bundleMap =
                    bundleMapByClusterUUID.computeIfAbsent(cluster.uuid, x -> new HashMap<>());
                UUID providerUUID = cluster.getProviderUUIDForNode(node);
                bundleMap.put(providerUUID, imageBundleUUID);
              });

      // Create the update lambda.
      UniverseUpdater updater =
          new UniverseUpdater() {
            @Override
            public void run(Universe universe) {
              // If this universe is not being edited, fail the request.
              UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
              if (!universeDetails.updateInProgress) {
                String errMsg =
                    "UserUniverse " + taskParams().getUniverseUUID() + " is not being edited.";
                log.error(errMsg);
                throw new RuntimeException(errMsg);
              }

              universeDetails.clusters.forEach(
                  (cluster) -> {
                    Map<UUID, UUID> bundleMap =
                        bundleMapByClusterUUID.getOrDefault(cluster.uuid, new HashMap<>());
                    bundleMap.forEach(
                        (providerUUID, imageBundleUUID) -> {
                          // Update the imageBundle reference for the cluster in which node
                          // is provisioned.
                          cluster.userIntent.setProviderImageBundleUUID(
                              providerUUID, imageBundleUUID);
                        });
                  });
              universe.setUniverseDetails(universeDetails);
            }
          };
      // Perform the update. If unsuccessful, this will throw a runtime exception which we do not
      // catch as we want to fail.
      saveUniverseDetails(updater);
    } catch (Exception e) {
      String msg = getName() + " failed with exception " + e.getMessage();
      log.warn(msg, e.getMessage());
      throw new RuntimeException(msg, e);
    }
  }
}
