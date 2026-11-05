// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.yugabyte.yw.cloud.CloudAPI;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.Provider;
import java.util.UUID;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

/**
 * Deletes one managed load balancer. The state keeps the entry until the cloud delete succeeds, so
 * a retry deletes it again.
 */
@Slf4j
public class DeleteManagedLoadBalancer extends UniverseTaskBase {

  private final CloudAPI.Factory cloudAPIFactory;

  @Inject
  protected DeleteManagedLoadBalancer(
      BaseTaskDependencies baseTaskDependencies, CloudAPI.Factory cloudAPIFactory) {
    super(baseTaskDependencies);
    this.cloudAPIFactory = cloudAPIFactory;
  }

  public static class Params extends UniverseTaskParams {
    public UUID clusterUUID;
    public UUID providerUUID;
    public UUID regionUUID;
    public String regionCode;
    public String lbName;
    // Set on force delete of the universe.
    public boolean ignoreErrors;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public String getName() {
    return super.getName()
        + "("
        + taskParams().getUniverseUUID()
        + ", region="
        + taskParams().regionCode
        + ", lbName="
        + taskParams().lbName
        + ")";
  }

  @Override
  public void run() {
    Params params = taskParams();
    Provider provider = Provider.getOrBadRequest(params.providerUUID);
    CloudAPI cloudAPI = cloudAPIFactory.get(provider.getCloudCode().name());
    try {
      cloudAPI.deleteManagedLoadBalancer(provider, params.regionCode, params.lbName);
    } catch (RuntimeException e) {
      if (!params.ignoreErrors) {
        throw e;
      }
      log.warn(
          "Ignoring error deleting load balancer {} in {}", params.lbName, params.regionCode, e);
      return;
    }
    updateManagedLoadBalancerState(
        state -> state.remove(params.clusterUUID, params.regionUUID, params.lbName));
  }
}
