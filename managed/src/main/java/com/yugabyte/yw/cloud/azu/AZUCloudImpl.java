// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.cloud.azu;

import static com.google.common.collect.MoreCollectors.onlyElement;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.azure.core.credential.TokenCredential;
import com.azure.core.management.SubResource;
import com.azure.identity.ClientSecretCredentialBuilder;
import com.azure.identity.DefaultAzureCredentialBuilder;
import com.azure.resourcemanager.compute.fluent.models.DiskInner;
import com.azure.resourcemanager.compute.fluent.models.VirtualMachineInner;
import com.azure.resourcemanager.compute.models.DataDisk;
import com.azure.resourcemanager.compute.models.NetworkInterfaceReference;
import com.azure.resourcemanager.network.fluent.models.BackendAddressPoolInner;
import com.azure.resourcemanager.network.fluent.models.FrontendIpConfigurationInner;
import com.azure.resourcemanager.network.fluent.models.LoadBalancerInner;
import com.azure.resourcemanager.network.fluent.models.LoadBalancingRuleInner;
import com.azure.resourcemanager.network.fluent.models.NetworkInterfaceInner;
import com.azure.resourcemanager.network.fluent.models.NetworkInterfaceIpConfigurationInner;
import com.azure.resourcemanager.network.fluent.models.ProbeInner;
import com.azure.resourcemanager.network.fluent.models.SubnetInner;
import com.azure.resourcemanager.network.models.IpAllocationMethod;
import com.azure.resourcemanager.network.models.LoadBalancerSku;
import com.azure.resourcemanager.network.models.LoadBalancerSkuName;
import com.azure.resourcemanager.network.models.LoadBalancerSkuTier;
import com.azure.resourcemanager.network.models.ProbeProtocol;
import com.azure.resourcemanager.network.models.TransportProtocol;
import com.azure.resourcemanager.resources.fluentcore.arm.ResourceUtils;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.annotations.VisibleForTesting;
import com.yugabyte.yw.cloud.CloudAPI;
import com.yugabyte.yw.common.CloudUtil.Protocol;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.utils.ManagedLoadBalancerUtil;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.helpers.CloudInfoInterface;
import com.yugabyte.yw.models.helpers.NLBHealthCheckConfiguration;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeID;
import com.yugabyte.yw.models.helpers.provider.AzureCloudInfo;
import com.yugabyte.yw.models.helpers.provider.region.AzureRegionCloudInfo;
import java.util.ArrayList;
import java.util.Collection;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.NoSuchElementException;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;
import java.util.function.Function;
import java.util.stream.Collectors;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections4.CollectionUtils;
import org.apache.commons.collections4.MapUtils;
import org.apache.commons.collections4.SetUtils;
import org.apache.commons.lang3.StringUtils;

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
  public boolean isValidCreds(Provider provider) {
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
   * @param port Port that the health probe needs to probe
   * @return Newly create ProbeInner Object
   */
  @VisibleForTesting
  protected ProbeInner createNewTCPProbeForPort(Integer port) {
    String probeName = "probe-" + port.toString() + "-" + UUID.randomUUID().toString();
    return new ProbeInner().withName(probeName).withPort(port).withProtocol(ProbeProtocol.TCP);
  }

  /**
   * Create a new health probe object on a specific port for a protocol. Note that this function
   * does not create a new probe in the Azure cloud
   *
   * @param port Port that the health probe needs to probe
   * @param requestPath The HTTP path at which the probe hits to check health
   * @return Newly create ProbeInner Object
   */
  @VisibleForTesting
  protected ProbeInner createNewHttpProbeForPort(Integer port, String requestPath) {
    String probeName = "probe-" + port.toString() + "-" + UUID.randomUUID().toString();
    return new ProbeInner()
        .withName(probeName)
        .withPort(port)
        .withProtocol(ProbeProtocol.HTTP)
        .withRequestPath(requestPath);
  }

  /**
   * Method to update existing Health probes, and create objects for the missing ones
   *
   * @param healthCheckConfiguration The configuration to be used to configure health probes
   * @param probes List of existing health probes
   * @return Updated list of health probes
   */
  @VisibleForTesting
  protected List<ProbeInner> ensureProbesForPorts(
      NLBHealthCheckConfiguration healthCheckConfiguration, List<ProbeInner> probes) {
    Protocol healthCheckProtocol = healthCheckConfiguration.getHealthCheckProtocol();
    Set<Integer> portsAlreadyProbed = new HashSet();
    if (!probes.isEmpty()) {
      portsAlreadyProbed =
          probes.stream()
              .filter(
                  probe ->
                      probe.protocol().toString().toUpperCase().equals(healthCheckProtocol.name()))
              .map(probe -> probe.port())
              .collect(Collectors.toSet());
    }
    Set<Integer> portsToProbe = new HashSet(healthCheckConfiguration.getHealthCheckPorts());
    Set<Integer> newPortsNeeded = SetUtils.difference(portsToProbe, portsAlreadyProbed);
    Set<Integer> portsToVerify = SetUtils.intersection(portsToProbe, portsAlreadyProbed);
    // Not deleting probes as they can be used by user for other purposes
    for (ProbeInner probe : probes) {
      if (portsToVerify.contains(probe.port()) && healthCheckProtocol == Protocol.HTTP) {
        probe =
            probe.withRequestPath(
                healthCheckConfiguration.getHealthCheckPortsToPathsMap().get(probe.port()));
      }
    }
    for (Integer port : newPortsNeeded) {
      switch (healthCheckProtocol) {
        case TCP:
          probes.add(createNewTCPProbeForPort(port));
          break;
        case HTTP:
          probes.add(
              createNewHttpProbeForPort(
                  port, healthCheckConfiguration.getHealthCheckPortsToPathsMap().get(port)));
          break;
        default:
          throw new PlatformServiceException(BAD_REQUEST, "Only TCP and HTTP probes are supported");
      }
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
      ipToVm.put(getPrimaryIpConfig(getPrimaryNic(apiClient, node)), node);
    }
    return ipToVm;
  }

  private static NetworkInterfaceInner getPrimaryNic(
      AZUResourceGroupApiClient apiClient, VirtualMachineInner node) {
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
    return apiClient.getNetworkInterface(primaryNetworkInterface.id());
  }

  private static NetworkInterfaceIpConfigurationInner getPrimaryIpConfig(
      NetworkInterfaceInner networkInterface) {
    try {
      return networkInterface.ipConfigurations().stream()
          .filter(config -> config.primary())
          .collect(onlyElement());
    } catch (IllegalStateException exception) {
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR,
          "Multiple primary IP configurations found for network interface: "
              + networkInterface.name());
    } catch (NoSuchElementException exception) {
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR,
          "No Primary IP configuration found for network interface: " + networkInterface.name());
    }
  }

  /**
   * Makes the primary backend pool, at index 0, hold exactly the nodes, and creates it if the load
   * balancer has none. An empty node list empties the pool (detach). A pool holds either IP
   * addresses or network interfaces. A pool that holds IP addresses stays that way, so that load
   * balancers that YBA set up before it used network interfaces keep working; all its VMs must be
   * in one virtual network. A new or empty pool takes network interfaces, because an Azure Private
   * Link service cannot use a load balancer with an IP-based pool.
   *
   * @param apiClient Azure API client to communicate with the Azure cloud
   * @param lbName Name of the load balancer to which the backend pools belong
   * @param backends List of existing backend pools
   * @param nodes List of nodes that need to be present in the backend pools
   * @return Updated list of backend pools
   */
  @VisibleForTesting
  protected List<BackendAddressPoolInner> ensureBackends(
      AZUResourceGroupApiClient apiClient,
      String lbName,
      List<BackendAddressPoolInner> backends,
      List<NodeID> nodeIDs) {
    List<VirtualMachineInner> nodes = getVirtualMachinesByNodeIDs(apiClient, nodeIDs);
    try {
      if (backends == null || backends.isEmpty() || !hasIpAddresses(backends.get(0))) {
        return ensureNicBackends(apiClient, lbName, backends, nodes);
      }
      Map<NetworkInterfaceIpConfigurationInner, VirtualMachineInner> ipToVm =
          mapIpToNodes(apiClient, nodes);
      // Load balancing traffic should be forwarded over the private network. Hence private
      // IP is used
      Map<String, String> ipToVmName =
          ipToVm.entrySet().stream()
              .collect(
                  Collectors.toMap(
                      entry -> entry.getKey().privateIpAddress(),
                      entry -> CloudAPI.getResourceNameFromResourceUrl(entry.getValue().id())));
      if (ipToVmName.isEmpty()) {
        // Detach: no member subnets to derive vnet from (onlyElement() below throws on empty
        // set); vnet unused when writing an empty address list.
        backends.set(
            0, apiClient.updateIPsInBackendPool(lbName, ipToVmName, backends.get(0), null));
        return backends;
      }
      Set<String> subnetIds =
          ipToVm.keySet().stream()
              .map(ipConfig -> ipConfig.subnet().id())
              .collect(Collectors.toSet());
      SubResource virtualNetwork =
          (new SubResource())
              .withId(
                  subnetIds.stream()
                      .map(subnet -> subnet.split("/subnets")[0])
                      .collect(onlyElement()));
      backends.set(
          0, apiClient.updateIPsInBackendPool(lbName, ipToVmName, backends.get(0), virtualNetwork));
      return backends;
    } catch (Exception exception) {
      log.error("Error updating backend pools for load balancer {}", lbName, exception);
      // getMessage() can be null (e.g. NoSuchElementException); fall back to toString() so the
      // task error is not reported as "null".
      String errorDetail =
          StringUtils.isNotBlank(exception.getMessage())
              ? exception.getMessage()
              : exception.toString();
      throw new PlatformServiceException(
          BAD_REQUEST, "Error updating backend pools: " + errorDetail);
    }
  }

  // Azure reports an IP address only for the members of an IP-based pool.
  private static boolean hasIpAddresses(BackendAddressPoolInner pool) {
    return CollectionUtils.emptyIfNull(pool.loadBalancerBackendAddresses()).stream()
        .anyMatch(address -> StringUtils.isNotBlank(address.ipAddress()));
  }

  // A network interface joins a pool through its IP configuration, not through the pool.
  private List<BackendAddressPoolInner> ensureNicBackends(
      AZUResourceGroupApiClient apiClient,
      String lbName,
      List<BackendAddressPoolInner> backends,
      List<VirtualMachineInner> nodes) {
    if (backends == null || backends.isEmpty()) {
      if (nodes.isEmpty()) {
        return new ArrayList<>();
      }
      backends =
          new ArrayList<>(
              List.of(apiClient.createBackendPool(lbName, "bp-" + UUID.randomUUID().toString())));
    }
    BackendAddressPoolInner pool = backends.get(0);
    Map<String, NetworkInterfaceInner> nicsById = new HashMap<>();
    for (VirtualMachineInner node : nodes) {
      NetworkInterfaceInner nic = getPrimaryNic(apiClient, node);
      nicsById.put(nic.id().toLowerCase(), nic);
    }
    removeNicsFromPool(apiClient, pool, nicsById.keySet());
    for (NetworkInterfaceInner nic : nicsById.values()) {
      setPoolMembership(apiClient, nic, getPrimaryIpConfig(nic), pool.id(), true);
    }
    backends.set(0, apiClient.getBackendPool(lbName, pool.name()));
    return backends;
  }

  /** Removes from the pool the network interfaces whose lowercase IDs are not in keepNicIds. */
  private static void removeNicsFromPool(
      AZUResourceGroupApiClient apiClient, BackendAddressPoolInner pool, Set<String> keepNicIds) {
    for (NetworkInterfaceIpConfigurationInner member :
        CollectionUtils.emptyIfNull(pool.backendIpConfigurations())) {
      String nicId = ResourceUtils.parentResourceIdFromResourceId(member.id());
      if (keepNicIds.contains(nicId.toLowerCase())) {
        continue;
      }
      NetworkInterfaceInner nic = apiClient.getNetworkInterface(nicId);
      nic.ipConfigurations().stream()
          .filter(ipConfig -> ipConfig.id().equalsIgnoreCase(member.id()))
          .findFirst()
          .ifPresent(ipConfig -> setPoolMembership(apiClient, nic, ipConfig, pool.id(), false));
    }
  }

  // Updates the network interface only when its membership changes.
  private static void setPoolMembership(
      AZUResourceGroupApiClient apiClient,
      NetworkInterfaceInner nic,
      NetworkInterfaceIpConfigurationInner ipConfig,
      String poolId,
      boolean member) {
    List<BackendAddressPoolInner> pools =
        new ArrayList<>(CollectionUtils.emptyIfNull(ipConfig.loadBalancerBackendAddressPools()));
    boolean wasMember = pools.removeIf(pool -> pool.id().equalsIgnoreCase(poolId));
    if (wasMember == member) {
      return;
    }
    if (member) {
      pools.add(new BackendAddressPoolInner().withId(poolId));
    }
    ipConfig.withLoadBalancerBackendAddressPools(pools);
    apiClient.updateNetworkInterface(nic);
  }

  /**
   * Function to associate health probes with the corrosponding load balancing rules. This is
   * important for health checks to function correctly
   *
   * @param loadBalancer LoadBalancerInner object whose lbRules needs to be updated
   * @param healthCheckConfiguration The health check configuration used to create the health checks
   *     for the load balancer
   * @return Updated LoadBalancerInner object
   */
  private LoadBalancerInner associateProbesWithLbRules(
      LoadBalancerInner loadBalancer, NLBHealthCheckConfiguration healthCheckConfiguration) {
    List<ProbeInner> probes = loadBalancer.probes();
    List<LoadBalancingRuleInner> loadBalancingRules = loadBalancer.loadBalancingRules();
    try {
      // backends and probes cannot be null here as they are taken from a recently updated load
      // balancer
      Map<Integer, ProbeInner> portToProbeMap =
          probes.stream().collect(Collectors.toMap(ProbeInner::port, Function.identity()));
      for (LoadBalancingRuleInner loadBalancingRule : loadBalancingRules) {
        ProbeInner probe = portToProbeMap.get(loadBalancingRule.backendPort());
        if (probe != null) {
          loadBalancingRule = loadBalancingRule.withProbe(probe);
        } else {
          // If there is no health probe corrosponding to the port that is being forwareded, we
          // select the 0th indexed port as the default health check for that forwarding rule
          // This is because this case would only arise in case of custom health checks
          // TODO: Find a way to link the correct custom health check to the correct forwarding rule
          loadBalancingRule =
              loadBalancingRule.withProbe(
                  portToProbeMap.get(healthCheckConfiguration.getHealthCheckPorts().get(0)));
        }
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
    manageNodeGroup(
        provider, regionCode, lbName, azToNodeIDs, "TCP", ports, healthCheckConfig, apiClient);
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
      String lbProtocol,
      List<Integer> portsToForward,
      NLBHealthCheckConfiguration healthCheckConfiguration,
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
        ensureLoadBalancingRules(
            lbProtocol, portsToForward, loadBalancingRules, backends, frontends.get(0));
    loadBalancer = loadBalancer.withLoadBalancingRules(loadBalancingRules);
    // Update health checks
    List<ProbeInner> probes = loadBalancer.probes();
    probes = ensureProbesForPorts(healthCheckConfiguration, probes);
    loadBalancer = loadBalancer.withProbes(probes);
    loadBalancer = apiClient.updateLoadBalancer(lbName, loadBalancer);
    // Double update of load balancer object is required because newly created probes are assigned
    // IDs only after the first update, and those IDs are requires to associate load balancing rules
    // with health probes.
    loadBalancer = associateProbesWithLbRules(loadBalancer, healthCheckConfiguration);
    apiClient.updateLoadBalancer(lbName, loadBalancer);
  }

  @VisibleForTesting
  protected AZUResourceGroupApiClient getApiClient(Provider provider) {
    return new AZUResourceGroupApiClient(CloudInfoInterface.get(provider));
  }

  // Managed load balancer methods

  @Override
  public boolean supportsManagedLoadBalancer() {
    return true;
  }

  /**
   * Creates the internal Standard load balancer with one frontend in the subnet of the first zone,
   * or reuses the one with the same name. It goes in the provider resource group, where
   * manageNodeGroup looks for it and adds the backend pool, rules and probes. The ports are not
   * needed here.
   *
   * @return the private IP of the frontend.
   */
  @Override
  public String ensureManagedLoadBalancer(
      Provider provider,
      String regionCode,
      String name,
      List<AvailabilityZone> zones,
      List<Integer> ports,
      Map<String, String> tags) {
    AZUResourceGroupApiClient apiClient = getApiClient(provider);
    LoadBalancerInner lb = apiClient.getLoadBalancerIfExists(name);
    if (lb == null) {
      FrontendIpConfigurationInner frontend =
          new FrontendIpConfigurationInner()
              .withName("frontend")
              .withPrivateIpAllocationMethod(IpAllocationMethod.DYNAMIC)
              .withSubnet(new SubnetInner().withId(getSubnetId(provider, zones, regionCode, name)));
      lb =
          apiClient.updateLoadBalancer(
              name,
              new LoadBalancerInner()
                  .withLocation(regionCode)
                  .withTags(tags)
                  .withSku(
                      new LoadBalancerSku()
                          .withName(LoadBalancerSkuName.STANDARD)
                          .withTier(LoadBalancerSkuTier.REGIONAL))
                  .withFrontendIpConfigurations(List.of(frontend)));
      log.info("Created load balancer {} in {}", name, regionCode);
    } else {
      addTags(apiClient, name, lb, tags);
    }
    return lb.frontendIpConfigurations().get(0).privateIpAddress();
  }

  /**
   * Deletes the load balancer with its pools, rules and probes. A load balancer that does not exist
   * counts as deleted.
   */
  @Override
  public void deleteManagedLoadBalancer(Provider provider, String regionCode, String name) {
    AZUResourceGroupApiClient apiClient = getApiClient(provider);
    LoadBalancerInner lb = apiClient.getLoadBalancerIfExists(name);
    if (lb == null) {
      log.info("Load balancer {} does not exist", name);
      return;
    }
    // A network interface that still uses a pool can block the delete. Only a destroy that ignored
    // errors leaves one behind: a destroy deletes the VMs and their interfaces first.
    for (BackendAddressPoolInner pool : CollectionUtils.emptyIfNull(lb.backendAddressPools())) {
      removeNicsFromPool(apiClient, pool, Set.of());
    }
    apiClient.deleteLoadBalancer(name);
    log.info("Deleted load balancer {} in {}", name, regionCode);
  }

  // Adds new tags and changes the values of existing ones. It never removes a tag, and a tag error
  // is not worth failing the task.
  private static void addTags(
      AZUResourceGroupApiClient apiClient,
      String name,
      LoadBalancerInner lb,
      Map<String, String> tags) {
    Map<String, String> merged = new HashMap<>(MapUtils.emptyIfNull(lb.tags()));
    merged.putAll(tags);
    if (merged.equals(MapUtils.emptyIfNull(lb.tags()))) {
      return;
    }
    try {
      apiClient.updateLoadBalancerTags(name, merged);
    } catch (RuntimeException e) {
      log.warn("Could not update the tags of load balancer {}: {}", name, e.getMessage());
    }
  }

  /**
   * The subnet ID of the first zone that has a subnet. A subnet name resolves as get_subnet_id in
   * devops azure/utils.py resolves it for the VMs.
   */
  private static String getSubnetId(
      Provider provider, List<AvailabilityZone> zones, String regionCode, String lbName) {
    AvailabilityZone zone =
        ManagedLoadBalancerUtil.getFirstZoneWithSubnet(zones, regionCode, lbName);
    String subnet = zone.getSubnet();
    if (subnet.startsWith("/subscriptions/")) {
      return subnet;
    }
    AzureRegionCloudInfo regionInfo = CloudInfoInterface.get(zone.getRegion());
    String vnet = regionInfo.getVnet();
    if (StringUtils.isBlank(vnet)) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Region " + regionCode + " has no virtual network");
    }
    if (!vnet.startsWith("/subscriptions/")) {
      AzureCloudInfo cloudInfo = CloudInfoInterface.get(provider);
      vnet =
          String.format(
              "/subscriptions/%s/resourceGroups/%s/providers/Microsoft.Network/virtualNetworks/%s",
              StringUtils.firstNonBlank(
                  cloudInfo.getAzuNetworkSubscriptionId(), cloudInfo.getAzuSubscriptionId()),
              StringUtils.firstNonBlank(
                  regionInfo.getAzuNetworkRGOverride(),
                  regionInfo.getAzuRGOverride(),
                  cloudInfo.getAzuNetworkRG(),
                  cloudInfo.getAzuRG()),
              vnet);
    }
    return vnet + "/subnets/" + subnet;
  }

  public static TokenCredential getCredsOrFallbackToDefault(
      String clientID, String clientSecret, String tenantID) {
    if (StringUtils.isNotEmpty(clientSecret)) {
      // Use service principal credentials if available
      return new ClientSecretCredentialBuilder()
          .clientId(clientID)
          .clientSecret(clientSecret)
          .tenantId(tenantID)
          .build();
    } else {
      // Try diff auth methods in a pre-defined order:
      // Environment variables, Workload Identity, Managed Identity
      return new DefaultAzureCredentialBuilder()
          .tenantId(tenantID)
          .managedIdentityClientId(clientID)
          .build();
    }
  }

  public static TokenCredential getCredsOrFallbackToDefault(AzureCloudInfo azureCloudInfo) {
    return getCredsOrFallbackToDefault(
        azureCloudInfo.getAzuClientId(),
        azureCloudInfo.getAzuClientSecret(),
        azureCloudInfo.getAzuTenantId());
  }

  /**
   * Current VM size plus IOPS/throughput/size of data disks. ARM disks have no last-resize field,
   * so {@code lastModificationStart} is always null; the cooldown gate uses local clocks when a
   * modify would actually run.
   */
  @Override
  public Optional<CloudAPI.NodeDiskSpec> describeNodeDataDiskSpec(
      Provider provider, NodeDetails node) {
    if (node == null || StringUtils.isBlank(node.nodeName)) {
      throw new PlatformServiceException(BAD_REQUEST, "Azure node is missing a name");
    }
    AzureCloudInfo azureCloudInfo = CloudInfoInterface.get(provider);
    AZUResourceGroupApiClient apiClient = new AZUResourceGroupApiClient(azureCloudInfo);
    VirtualMachineInner vm = apiClient.getVirtulMachineDetailsByName(node.nodeName);
    if (vm.storageProfile() == null || vm.storageProfile().dataDisks() == null) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Azure VM " + node.nodeName + " has no data disks");
    }
    List<CloudAPI.NodeDiskSpec> perDisk = new ArrayList<>();
    for (DataDisk dataDisk : vm.storageProfile().dataDisks()) {
      String diskId = dataDisk.managedDisk() == null ? null : dataDisk.managedDisk().id();
      String diskName = diskNameFromId(diskId);
      if (StringUtils.isBlank(diskName)) {
        throw new PlatformServiceException(
            BAD_REQUEST, "Azure data disk on " + node.nodeName + " has no managed disk id");
      }
      DiskInner disk = apiClient.getDiskByName(diskName);
      perDisk.add(
          new CloudAPI.NodeDiskSpec(
              null,
              toInt(disk.diskIopsReadWrite()),
              toInt(disk.diskMBpsReadWrite()),
              disk.diskSizeGB(),
              null));
    }
    String instanceType =
        vm.hardwareProfile() == null || vm.hardwareProfile().vmSize() == null
            ? null
            : vm.hardwareProfile().vmSize().toString();
    return Optional.of(CloudAPI.NodeDiskSpec.mergeDataDisks(instanceType, perDisk));
  }

  private static String diskNameFromId(String diskId) {
    if (StringUtils.isBlank(diskId)) {
      return null;
    }
    int slash = diskId.lastIndexOf('/');
    return slash < 0 ? diskId : diskId.substring(slash + 1);
  }

  private static Integer toInt(Long value) {
    return value == null ? null : Math.toIntExact(value);
  }
}
