// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.cloud.azu;

import static com.google.common.collect.MoreCollectors.onlyElement;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.azure.core.management.SubResource;
import com.azure.resourcemanager.compute.fluent.models.VirtualMachineInner;
import com.azure.resourcemanager.compute.models.NetworkInterfaceReference;
import com.azure.resourcemanager.network.fluent.models.BackendAddressPoolInner;
import com.azure.resourcemanager.network.fluent.models.FrontendIpConfigurationInner;
import com.azure.resourcemanager.network.fluent.models.LoadBalancerInner;
import com.azure.resourcemanager.network.fluent.models.LoadBalancingRuleInner;
import com.azure.resourcemanager.network.fluent.models.NetworkInterfaceInner;
import com.azure.resourcemanager.network.fluent.models.NetworkInterfaceIpConfigurationInner;
import com.azure.resourcemanager.network.fluent.models.ProbeInner;
import com.azure.resourcemanager.network.models.ProbeProtocol;
import com.azure.resourcemanager.network.models.TransportProtocol;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.annotations.VisibleForTesting;
import com.yugabyte.yw.cloud.CloudAPI;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.helpers.CloudInfoInterface;
import com.yugabyte.yw.models.helpers.NLBHealthCheckConfiguration;
import com.yugabyte.yw.models.helpers.NodeID;
import com.yugabyte.yw.models.helpers.provider.AzureCloudInfo;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collection;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.NoSuchElementException;
import java.util.Set;
import java.util.UUID;
import java.util.function.Function;
import java.util.stream.Collectors;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections4.SetUtils;

@Slf4j
public class AZUCloudImpl implements CloudAPI {

  /**
   * Find the instance types offered in availabilityZones.
   *
   * @param provider the cloud provider bean for the AWS provider.
   * @param azByRegionMap user selected availabilityZones by their parent region.
   * @param instanceTypesFilter list of instanceTypes for which we want to list the offerings.
   * @return a map. Key of this map is instance type like "c5.xlarge" and value is all the
   *     availabilityZones for which the instance type is being offered.
   */
  @Override
  public Map<String, Set<String>> offeredZonesByInstanceType(
      Provider provider, Map<Region, Set<String>> azByRegionMap, Set<String> instanceTypesFilter) {
    // TODO make a call to the cloud provider to populate.
    // Make the instances available in all availabilityZones.
    Set<String> azs =
        azByRegionMap.values().stream().flatMap(s -> s.stream()).collect(Collectors.toSet());
    return instanceTypesFilter.stream().collect(Collectors.toMap(Function.identity(), i -> azs));
  }

  // Basic validation to make sure that the credentials work with Azure.
  @Override
  public boolean isValidCreds(Provider provider, String region) {
    // TODO validation for Azure crashes VM at the moment due to netty and jackson version issues.
    return true;
  }

  @Override
  public boolean isValidCredsKms(ObjectNode config, UUID customerUUID) {
    return true;
  }

  /**
   * Create a new load balancing rule object for the given port, protocol and frontend-backend
   * combination
   *
   * @param protocol Protocol for which the load balancing rule is defined. Currently only TCP is
   *     allowed
   * @param port The port that needs to be forwarded
   * @param backendAddressPools The list of backend address pools to which the traffic needs to be
   *     sent
   * @param frontendIpConfiguration The frontend configuration for the balancing rule
   * @return the newly created load balancing rule object
   */
  @VisibleForTesting
  protected LoadBalancingRuleInner createNewLoadBalancingRuleForPort(
      String protocol,
      Integer port,
      List<SubResource> backendAddressPools,
      SubResource frontendIpConfiguration) {
    if (protocol.equals("TCP")) {
      String ruleName = "lb-rule-" + port.toString() + "-" + UUID.randomUUID().toString();
      return new LoadBalancingRuleInner()
          .withName(ruleName)
          .withFrontendPort(port)
          .withBackendPort(port)
          .withProtocol(TransportProtocol.TCP)
          .withBackendAddressPools(backendAddressPools)
          .withFrontendIpConfiguration(frontendIpConfiguration);
    }
    throw new PlatformServiceException(BAD_REQUEST, "Only TCP protocl is supported");
  }

  /**
   * Method to update existing load balancing rules, and create objects for the missing load
   * balancing rules. Note that this function does not create the load balancing rules in the Azure
   * cloud When creating new load balancing rules, the default behaviour is to keep the frontend
   * port the same as the backend port, to be consistent with the GCP API in behaviour
   *
   * @param protocol Protocol for which the load balancing rule is defined. Currently only TCP is
   *     allowed
   * @param portsToCheck List of ports that should have load balancing rules
   * @param rulesToBeUpdated List of existing load balancing rules for the load balancer
   * @param backends The list of backend address pools to which the traffic needs to be sent
   * @param frontendIpConfig The frontend configuration for the balancing rule
   * @return Update list of load balancing rules
   */
  @VisibleForTesting
  protected List<LoadBalancingRuleInner> ensureLoadBalancingRules(
      String protocol,
      List<Integer> portsToCheck,
      List<LoadBalancingRuleInner> rulesToBeUpdated,
      List<BackendAddressPoolInner> backends,
      FrontendIpConfigurationInner frontendIpConfig) {
    Set<Integer> forwardedPorts = new HashSet();
    if (rulesToBeUpdated == null) {
      rulesToBeUpdated = new ArrayList();
    }
    if (!rulesToBeUpdated.isEmpty()) {
      forwardedPorts =
          rulesToBeUpdated.stream()
              .map(loadBalancingRule -> loadBalancingRule.backendPort())
              .collect(Collectors.toSet());
    }
    Set<Integer> portsToForward = new HashSet(portsToCheck);
    Set<Integer> newPortsNeeded = SetUtils.difference(portsToForward, forwardedPorts);
    Set<Integer> portsToVerify = SetUtils.intersection(portsToForward, forwardedPorts);
    SubResource frontendIpConfigSubResource = (new SubResource()).withId(frontendIpConfig.id());
    List<SubResource> backendSubResources =
        backends.stream()
            .map(backend -> (new SubResource()).withId(backend.id()))
            .collect(Collectors.toList());
    // Update existing load balancing rules
    for (LoadBalancingRuleInner balancingRule : rulesToBeUpdated) {
      // There could be other load balancing rules. We are just interested in the ones where the
      // frontend and backend port match
      if (portsToVerify.contains(balancingRule.backendPort())) {
        balancingRule =
            balancingRule
                .withProtocol(TransportProtocol.TCP)
                .withBackendAddressPools(backendSubResources)
                .withFrontendIpConfiguration(frontendIpConfigSubResource);
      }
    }
    // Create missing load balancing rules
    for (Integer port : newPortsNeeded) {
      LoadBalancingRuleInner newLoadBalancingRule =
          createNewLoadBalancingRuleForPort(
              protocol, port, backendSubResources, frontendIpConfigSubResource);
      rulesToBeUpdated.add(newLoadBalancingRule);
    }
    // We do not remove load balancing rules, as they might be customer specific. Check how this
    // would affect YBM from a security standpoint
    return rulesToBeUpdated;
  }

  /**
   * Get a list of virtual machine details corrosponding to the nodeIDs
   *
   * @param apiClient Azure API client to communicate with the Azure cloud
   * @param nodeIDs List of NodeIDs for which details of the VM are needed
   * @return List of VirtualMachineInner objects, containing the details of VMs
   */
  private List<VirtualMachineInner> getVirtualMachinesByNodeIDs(
      AZUResourceGroupApiClient apiClient, List<NodeID> nodeIDs) {
    List<VirtualMachineInner> virtualMachines = new ArrayList();
    for (NodeID nodeID : nodeIDs) {
      virtualMachines.add(apiClient.getVirtulMachineDetailsByName(nodeID.getName()));
    }
    return virtualMachines;
  }

  /**
   * Create a new health probe object on a specific port for a protocol. Note that this function
   * does not create a new probe in the Azure cloud
   *
   * @param protocol Protocol for which the helath probe needs to be created. Currently only TCP is
   *     allowed
   * @param port Port that the health probe needs to probe
   * @return Newly create ProbeInner Object
   */
  @VisibleForTesting
  protected ProbeInner createNewProbeForPort(String protocol, Integer port) {
    if (protocol.equals("TCP")) {
      String probeName = "probe-" + port.toString() + "-" + UUID.randomUUID().toString();
      return new ProbeInner().withName(probeName).withPort(port).withProtocol(ProbeProtocol.TCP);
    }
    throw new PlatformServiceException(BAD_REQUEST, "Only TCP probes are supported");
  }

  /**
   * Method to update existing Health probes, and create objects for the missing ones
   *
   * @param protocol Protocol for which the helath probe needs to be created. Currently only TCP is
   *     allowed
   * @param ports Ports which need to be probed
   * @param probes List of existing health probes
   * @return Updated list of health probes
   */
  @VisibleForTesting
  protected List<ProbeInner> ensureProbesForPorts(
      String protocol, List<Integer> ports, List<ProbeInner> probes) {
    Set<Integer> portsAlreadyProbed = new HashSet();
    if (!probes.isEmpty()) {
      portsAlreadyProbed = probes.stream().map(probe -> probe.port()).collect(Collectors.toSet());
    }
    Set<Integer> portsToProbe = new HashSet(ports);
    Set<Integer> newPortsNeeded = SetUtils.difference(portsToProbe, portsAlreadyProbed);
    Set<Integer> portsToVerify = SetUtils.intersection(portsToProbe, portsAlreadyProbed);
    // Not deleting probes as they can be used by user for other purposes
    for (ProbeInner probe : probes) {
      if (portsToVerify.contains(probe.port())) {
        if (probe.protocol() != ProbeProtocol.TCP) {
          probe = probe.withProtocol(ProbeProtocol.TCP);
        }
      }
    }
    for (Integer port : newPortsNeeded) {
      probes.add(createNewProbeForPort(protocol, port));
    }
    return probes;
  }

  /**
   * Method to get a mapping between the primary IP address of a VM and the VM details object
   *
   * @param apiClient Azure API client to communicate with the Azure cloud
   * @param nodes List of nodes whose primary IP address is require
   * @return Mapping from the primary IP address configuration of a VM to its corrosponding
   *     VirtualMachineInner object
   */
  private Map<NetworkInterfaceIpConfigurationInner, VirtualMachineInner> mapIpToNodes(
      AZUResourceGroupApiClient apiClient, List<VirtualMachineInner> nodes) {
    Map<NetworkInterfaceIpConfigurationInner, VirtualMachineInner> ipToVm = new HashMap();
    for (VirtualMachineInner node : nodes) {
      List<NetworkInterfaceReference> networkInterfaces = node.networkProfile().networkInterfaces();
      NetworkInterfaceReference primaryNetworkInterface;
      try {
        primaryNetworkInterface =
            networkInterfaces.stream()
                .filter(nic -> nic.primary() != Boolean.FALSE)
                .collect(onlyElement());
      } catch (IllegalStateException exception) {
        throw new PlatformServiceException(
            INTERNAL_SERVER_ERROR, "Multiple primary network interfaces found for node: " + node);
      } catch (NoSuchElementException exception) {
        throw new PlatformServiceException(
            INTERNAL_SERVER_ERROR, "No Primary network interface found for node: " + node);
      }
      String networkInterfaceUrl = primaryNetworkInterface.id();
      String networkInterfaceName = CloudAPI.getResourceNameFromResourceUrl(networkInterfaceUrl);
      NetworkInterfaceInner networkInterface =
          apiClient.getNetworkInterfaceByName(networkInterfaceName);
      try {
        NetworkInterfaceIpConfigurationInner primaryIpConfig =
            networkInterface.ipConfigurations().stream()
                .filter(config -> config.primary())
                .collect(onlyElement());
        ipToVm.put(primaryIpConfig, node);
      } catch (IllegalStateException exception) {
        throw new PlatformServiceException(
            INTERNAL_SERVER_ERROR, "Multiple primary IP configurations found for node: " + node);
      } catch (NoSuchElementException exception) {
        throw new PlatformServiceException(
            INTERNAL_SERVER_ERROR, "No Primary IP configuration found for node: " + node);
      }
    }
    return ipToVm;
  }

  /**
   * Method to update existing backend pools, as well as create the missing backend pools. This
   * methods leads to the creation of the bools on Azure cloud as well The vms that are a part of
   * the backend pool, all need to be a part of the same virtual network in which the existing
   * backend pool is already a part of
   *
   * @param apiClient Azure API client to communicate with the Azure cloud
   * @param lbName Name of the load balancer to which the backend pools belong
   * @param backends List of existing backend pools
   * @param nodes List of nodes that need to be present in the backend pools
   * @return Updated list of backend pools
   */
  private List<BackendAddressPoolInner> ensureBackends(
      AZUResourceGroupApiClient apiClient,
      String lbName,
      List<BackendAddressPoolInner> backends,
      List<NodeID> nodeIDs) {
    List<VirtualMachineInner> nodes = getVirtualMachinesByNodeIDs(apiClient, nodeIDs);
    Map<NetworkInterfaceIpConfigurationInner, VirtualMachineInner> ipToVm =
        mapIpToNodes(apiClient, nodes);
    // Load balancing traffic should be forwarded over the private network. Hence private IP is used
    Map<String, String> ipToVmName =
        ipToVm.entrySet().stream()
            .collect(
                Collectors.toMap(
                    entry -> entry.getKey().privateIpAddress(),
                    entry -> CloudAPI.getResourceNameFromResourceUrl(entry.getValue().id())));
    Set<String> subnetIds =
        ipToVm.keySet().stream()
            .map(ipConfig -> ipConfig.subnet().id())
            .collect(Collectors.toSet());
    try {
      SubResource virtualNetwork =
          (new SubResource())
              .withId(
                  subnetIds.stream()
                      .map(subnet -> subnet.split("/subnets")[0])
                      .collect(onlyElement()));
      // Assumption: The backend pool at 0th index is primary backend pool, and we only change this
      // pool
      if (backends == null || backends.size() == 0) {
        return Arrays.asList(
            apiClient.createNewBackendPoolForIPs(lbName, ipToVmName, virtualNetwork));
      } else {
        BackendAddressPoolInner backendAddressPool = backends.get(0);
        backendAddressPool =
            apiClient.updateIPsInBackendPool(
                lbName, ipToVmName, backendAddressPool, virtualNetwork);
        backends.set(0, backendAddressPool);
      }
      return backends;
    } catch (Exception exception) {
      throw new PlatformServiceException(
          BAD_REQUEST, "All nodes added to a load balancer must belong to the same virtualNetwork");
    }
  }

  /**
   * Function to associate health probes with the corrosponding load balancing rules. This is
   * important for health checks to function correctly
   *
   * @param loadBalancer LoadBalancerInner object whose lbRules needs to be updated
   * @return Updated LoadBalancerInner object
   */
  private LoadBalancerInner associateProbesWithLbRules(LoadBalancerInner loadBalancer) {
    List<ProbeInner> probes = loadBalancer.probes();
    List<LoadBalancingRuleInner> loadBalancingRules = loadBalancer.loadBalancingRules();
    try {
      // backends and probes cannot be null here as they are taken from a recently updated load
      // balancer
      Map<Integer, ProbeInner> portToProbeMap =
          probes.stream().collect(Collectors.toMap(ProbeInner::port, Function.identity()));
      for (LoadBalancingRuleInner loadBalancingRule : loadBalancingRules) {
        loadBalancingRule =
            loadBalancingRule.withProbe(portToProbeMap.get(loadBalancingRule.backendPort()));
      }
      return loadBalancer.withLoadBalancingRules(loadBalancingRules);
    } catch (IllegalStateException exception) {
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR, "Multiple probes found with the same port");
    }
  }

  // Wrapper function to keep the exposed API consistent, as well as allow for unit testing
  @Override
  public void manageNodeGroup(
      Provider provider,
      String regionCode,
      String lbName,
      Map<AvailabilityZone, Set<NodeID>> azToNodeIDs,
      List<Integer> ports,
      NLBHealthCheckConfiguration healthCheckConfig) {
    AzureCloudInfo azureCloudInfo = CloudInfoInterface.get(provider);
    AZUResourceGroupApiClient apiClient = new AZUResourceGroupApiClient(azureCloudInfo);
    String protocol = healthCheckConfig.getHealthCheckProtocol().toString();
    manageNodeGroup(provider, regionCode, lbName, azToNodeIDs, protocol, ports, apiClient);
  }

  /**
   * Update the existing load balancer with the given nodes and parameters.
   *
   * @param provider the cloud provider bean for the AZU provider.
   * @param regionCode the region code.
   * @param lbName the load balancer name.
   * @param nodeIDs the DB node IDs (name, uuid).
   * @param protocol the listening protocol. (Only TCP supported for now)
   * @param ports the listening ports enabled (YSQL, YCQL, YEDIS).
   * @param apiClient client object to make requests to the Azure cloud platform
   */
  private void manageNodeGroup(
      Provider provider,
      String regionCode,
      String lbName,
      Map<AvailabilityZone, Set<NodeID>> azToNodeIDs,
      String protocol,
      List<Integer> ports,
      AZUResourceGroupApiClient apiClient) {
    LoadBalancerInner loadBalancer = apiClient.getLoadBalancerByName(lbName);
    if (!loadBalancer.location().equals(regionCode)) {
      throw new PlatformServiceException(
          BAD_REQUEST, "No load balancer in region " + regionCode + " with name " + lbName);
    }
    List<NodeID> nodeIDs =
        azToNodeIDs.values().stream().flatMap(Collection::stream).collect(Collectors.toList());
    // Just ensure that the LB has atleast one frontend IP configuration
    // We expect this to be configured by the user
    List<FrontendIpConfigurationInner> frontends = loadBalancer.frontendIpConfigurations();
    if (frontends.isEmpty()) {
      throw new PlatformServiceException(
          BAD_REQUEST, "No frontend IPs configured for load balancer " + lbName);
    }
    // Update the backend address pools
    List<BackendAddressPoolInner> backends = loadBalancer.backendAddressPools();
    backends = ensureBackends(apiClient, lbName, backends, nodeIDs);
    loadBalancer = loadBalancer.withBackendAddressPools(backends);
    // Update load balancing rules with the correct backends
    List<LoadBalancingRuleInner> loadBalancingRules = loadBalancer.loadBalancingRules();
    // The 0th index forwarding IP configuration is assumed to be the primary
    loadBalancingRules =
        ensureLoadBalancingRules(protocol, ports, loadBalancingRules, backends, frontends.get(0));
    loadBalancer = loadBalancer.withLoadBalancingRules(loadBalancingRules);
    // Update health checks
    List<ProbeInner> probes = loadBalancer.probes();
    probes = ensureProbesForPorts(protocol, ports, probes);
    loadBalancer = loadBalancer.withProbes(probes);
    loadBalancer = apiClient.updateLoadBalancer(lbName, loadBalancer);
    // Double update of load balancer object is required because newly created probes are assigned
    // IDs only after the first update, and those IDs are requires to associate load balancing rules
    // with health probes.
    loadBalancer = associateProbesWithLbRules(loadBalancer);
    apiClient.updateLoadBalancer(lbName, loadBalancer);
  }

  @Override
  public void validateInstanceTemplate(Provider provider, String instanceTemplate) {
    throw new PlatformServiceException(
        BAD_REQUEST, "Instance templates are currently not supported for Azure");
  }
}
