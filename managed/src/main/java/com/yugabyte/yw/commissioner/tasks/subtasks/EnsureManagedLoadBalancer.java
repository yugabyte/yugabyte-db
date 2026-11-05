// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.yugabyte.yw.cloud.CloudAPI;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.helpers.ManagedLoadBalancer;
import com.yugabyte.yw.models.helpers.ManagedLoadBalancerState;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.stream.Collectors;
import javax.inject.Inject;

/** Creates one managed load balancer of a cluster, or reconciles the existing one. */
public class EnsureManagedLoadBalancer extends UniverseTaskBase {

  private final CloudAPI.Factory cloudAPIFactory;

  @Inject
  protected EnsureManagedLoadBalancer(
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
    public ManagedLoadBalancer.Scheme scheme;
    public List<UUID> azUUIDs;
    public Map<String, String> tags;
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
    ManagedLoadBalancerState state =
        getUniverse().getUniverseDetails().getManagedLoadBalancerState();
    if (state == null
        || state.find(params.clusterUUID, params.regionUUID, params.lbName).isEmpty()) {
      // Record the name before the cloud call, so a destroy after a crash finds the load balancer.
      saveState(null);
    }
    List<AvailabilityZone> zones =
        params.azUUIDs.stream().map(AvailabilityZone::getOrBadRequest).collect(Collectors.toList());
    // Read when the subtask runs: ConfigureDBApis stores the new ports after it creates this task.
    List<Integer> ports =
        ManageLoadBalancerGroup.getForwardedPorts(
            getUserIntent(), getUniverse().getUniverseDetails().communicationPorts);
    String address =
        cloudAPI.ensureManagedLoadBalancer(
            provider, params.regionCode, params.lbName, zones, ports, params.tags);
    saveState(address);
  }

  private void saveState(String address) {
    Params params = taskParams();
    updateManagedLoadBalancerState(
        state ->
            state.put(
                new ManagedLoadBalancer(
                    params.clusterUUID,
                    params.regionUUID,
                    params.scheme,
                    params.azUUIDs,
                    params.lbName,
                    address)));
  }
}
