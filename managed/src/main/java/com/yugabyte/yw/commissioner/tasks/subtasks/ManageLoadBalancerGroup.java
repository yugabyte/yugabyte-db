package com.yugabyte.yw.commissioner.tasks.subtasks;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.google.api.client.util.Throwables;
import com.yugabyte.yw.cloud.CloudAPI;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.common.CloudUtil.Protocol;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.LoadBalancerConfig;
import com.yugabyte.yw.models.helpers.NLBHealthCheckConfiguration;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeID;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class ManageLoadBalancerGroup extends UniverseTaskBase {

  private final CloudAPI.Factory cloudAPIFactory;

  @Inject
  protected ManageLoadBalancerGroup(
      BaseTaskDependencies baseTaskDependencies, CloudAPI.Factory cloudAPIFactory) {
    super(baseTaskDependencies);
    this.cloudAPIFactory = cloudAPIFactory;
  }

  // Parameters for manage load balancer group task
  public static class Params extends UniverseTaskParams {
    // Cloud provider of load balancer
    public UUID providerUUID;
    // Region of nodes
    public String regionCode;
    // Load Balancer AZ -> node set details
    public LoadBalancerConfig lbConfig;
  }

  @Override
  protected ManageLoadBalancerGroup.Params taskParams() {
    return (ManageLoadBalancerGroup.Params) taskParams;
  }

  @Override
  public String getName() {
    return super.getName()
        + "("
        + taskParams().getUniverseUUID()
        + ", provider="
        + taskParams().providerUUID
        + ", lbName="
        + taskParams().lbConfig.getLbName()
        + ")";
  }

  @Override
  public void run() {
    Provider provider = Provider.getOrBadRequest(taskParams().providerUUID);
    CloudAPI cloudAPI = cloudAPIFactory.get(provider.getCloudCode().name());

    Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
    UniverseDefinitionTaskParams.UserIntent userIntent = getUserIntent();
    List<Integer> ports = new ArrayList<>();
    if (userIntent.enableYSQL)
      ports.add(universe.getUniverseDetails().communicationPorts.ysqlServerRpcPort);
    if (userIntent.enableYCQL)
      ports.add(universe.getUniverseDetails().communicationPorts.yqlServerRpcPort);
    if (userIntent.enableYEDIS)
      ports.add(universe.getUniverseDetails().communicationPorts.redisServerRpcPort);
    LoadBalancerConfig lbConfig = taskParams().lbConfig;
    try {
      Map<AvailabilityZone, Set<NodeID>> azToNodeIDs = getNodeIDs(lbConfig.getAzNodes());
      NLBHealthCheckConfiguration healthCheckConfiguration =
          getNlbHealthCheckConfiguration(universe, ports);
      cloudAPI.manageNodeGroup(
          provider,
          taskParams().regionCode,
          lbConfig.getLbName(),
          azToNodeIDs,
          ports,
          healthCheckConfiguration);
    } catch (Exception e) {
      String msg =
          "Error "
              + e.getMessage()
              + " for "
              + taskParams().lbConfig.getLbName()
              + " load balancer";
      log.error(msg, e);
      Throwables.propagate(e);
    }
  }

  NLBHealthCheckConfiguration getNlbHealthCheckConfiguration(
      Universe universe, List<Integer> ports) {
    List<Integer> healthCheckPorts =
        confGetter.getConfForScope(universe, UniverseConfKeys.customHealthCheckPorts);
    List<String> healthCheckPaths =
        confGetter.getConfForScope(universe, UniverseConfKeys.customHealthCheckPaths);
    Protocol healthCheckProtocol =
        confGetter.getConfForScope(universe, UniverseConfKeys.customHealthCheckProtocol);
    if (healthCheckProtocol == Protocol.TCP) {
      if (healthCheckPorts.isEmpty()) {
        healthCheckPorts.addAll(ports);
      } else {
        healthCheckPorts =
            healthCheckPorts.stream()
                .distinct()
                .filter(port -> (port > 0 && port <= 65535))
                .collect(Collectors.toList());
      }
    } else {
      if (healthCheckPorts.isEmpty()) {
        throw new PlatformServiceException(
            BAD_REQUEST,
            "For HTTP health checks, custom health check ports and paths both must be specified.");
      } else if (healthCheckPorts.size() != healthCheckPaths.size()) {
        throw new PlatformServiceException(
            BAD_REQUEST,
            "For HTTP health checks, custom health check ports paths must be of the same size,"
                + " with one to one corrospondence.");
      }
    }

    log.debug(
        "Heach check config = {}, {}, {}", healthCheckPorts, healthCheckProtocol, healthCheckPaths);
    NLBHealthCheckConfiguration healthCheckConfiguration =
        new NLBHealthCheckConfiguration(healthCheckPorts, healthCheckProtocol, healthCheckPaths);
    return healthCheckConfiguration;
  }

  public Map<AvailabilityZone, Set<NodeID>> getNodeIDs(
      Map<AvailabilityZone, Set<NodeDetails>> azToNodes) {
    Map<AvailabilityZone, Set<NodeID>> azToNodeIDs = new HashMap();
    for (Map.Entry<AvailabilityZone, Set<NodeDetails>> azToNode : azToNodes.entrySet()) {
      azToNodeIDs.put(
          azToNode.getKey(),
          azToNode.getValue().stream()
              .map(node -> new NodeID(node.nodeName, node.nodeUuid.toString()))
              .collect(Collectors.toSet()));
    }
    return azToNodeIDs;
  }
}
