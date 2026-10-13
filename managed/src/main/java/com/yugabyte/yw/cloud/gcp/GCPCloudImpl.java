// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.cloud.gcp;

import static com.google.common.collect.MoreCollectors.onlyElement;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.FORBIDDEN;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.api.services.compute.model.Address;
import com.google.api.services.compute.model.Backend;
import com.google.api.services.compute.model.BackendService;
import com.google.api.services.compute.model.ConnectionDraining;
import com.google.api.services.compute.model.ForwardingRule;
import com.google.api.services.compute.model.HTTPHealthCheck;
import com.google.api.services.compute.model.HealthCheck;
import com.google.api.services.compute.model.InstanceGroup;
import com.google.api.services.compute.model.InstanceReference;
import com.google.common.annotations.VisibleForTesting;
import com.google.common.collect.LinkedHashMultimap;
import com.google.common.collect.SetMultimap;
import com.google.inject.Inject;
import com.yugabyte.yw.cloud.CloudAPI;
import com.yugabyte.yw.common.CloudUtil.Protocol;
import com.yugabyte.yw.common.GCPUtil;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.config.ProviderConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.helpers.CloudInfoInterface;
import com.yugabyte.yw.models.helpers.NLBHealthCheckConfiguration;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeID;
import com.yugabyte.yw.models.helpers.provider.GCPCloudInfo;
import java.io.IOException;
import java.security.GeneralSecurityException;
import java.time.Duration;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;
import java.util.function.Function;
import java.util.regex.Pattern;
import java.util.stream.Collectors;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.codec.digest.DigestUtils;
import org.apache.commons.collections4.CollectionUtils;
import org.apache.commons.collections4.SetUtils;
import org.apache.commons.lang3.StringUtils;

@Slf4j
public class GCPCloudImpl implements CloudAPI {
  public static final String PROJECT_ID_PROPERTY = "gce_project";
  public static final String CUSTOM_GCE_NETWORK_PROPERTY = "CUSTOM_GCE_NETWORK";
  public static final String GCE_PROJECT_PROPERTY = "GCE_PROJECT";
  public static final String GOOGLE_APPLICATION_CREDENTIALS_PROPERTY =
      "GOOGLE_APPLICATION_CREDENTIALS";

  // The names YBA gave before the hashed names: hc-<port><UUID> and ig-<UUID>.
  private static final String UUID_REGEX = "[0-9a-f]{8}(-[0-9a-f]{4}){3}-[0-9a-f]{12}";
  private static final Pattern OLD_HEALTH_CHECK_NAME = Pattern.compile("hc-\\d+" + UUID_REGEX);
  private static final Pattern OLD_INSTANCE_GROUP_NAME = Pattern.compile("ig-" + UUID_REGEX);

  @Inject private RuntimeConfGetter runtimeConfGetter;

  @VisibleForTesting
  protected GCPProjectApiClient getApiClient(Provider provider) {
    return new GCPProjectApiClient(runtimeConfGetter, provider);
  }

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

  // Basic validation to make sure that the credentials work with GCP.
  @Override
  public boolean isValidCreds(Provider provider) {
    try {
      GCPProjectApiClient apiClient = getApiClient(provider);
      // Check if the creds have the required permission(s) to fetch instances
      List<String> reqPermission = new ArrayList<>();
      reqPermission.add(GCPUtil.INSTANCE_LIST_PERMISSION);
      if (apiClient.testIam(reqPermission).size() > 0) {
        String errorMsg =
            "SA validation failed. The SA doesn't have the required permission(s): "
                + reqPermission.stream().collect(Collectors.joining(", "));
        throw new PlatformServiceException(FORBIDDEN, errorMsg);
      }
      apiClient.checkInstanceFetching();
    } catch (GeneralSecurityException | IOException e) {
      log.error("Error in validating GCP credentials", e);
      return false;
    }
    return true;
  }

  @Override
  public boolean isValidCredsKms(ObjectNode config, UUID customerUUID) {
    return true;
  }

  // Given a mapping from AvailablityZone to a set of NodeIDs, get a mapping from zoneName to a set
  // of InstnceReferences for those nodeIDs
  @VisibleForTesting
  protected Map<String, List<InstanceReference>> getAzToInstanceReferenceMap(
      GCPProjectApiClient apiClient, Map<AvailabilityZone, Set<NodeID>> azToNodeIDs) {
    Map<String, List<InstanceReference>> nodesMap = new HashMap<>();
    if (azToNodeIDs.isEmpty()) {
      return nodesMap;
    }
    for (Map.Entry<AvailabilityZone, Set<NodeID>> azToNodeID : azToNodeIDs.entrySet()) {
      String zone = azToNodeID.getKey().getName();
      List<String> nodeNames =
          azToNodeID.getValue().stream()
              .map(nodeId -> nodeId.getName())
              .collect(Collectors.toList());
      List<InstanceReference> instances = apiClient.getInstancesInZoneByNames(zone, nodeNames);
      if (instances.size() != azToNodeID.getValue().size()) {
        throw new PlatformServiceException(
            INTERNAL_SERVER_ERROR, "Cannot find all instances in zone " + zone);
      }
      if (!instances.isEmpty()) {
        nodesMap.put(zone, instances);
      }
    }
    log.info("Sucessfully mapped all nodes with availablity zones");
    return nodesMap;
  }

  /**
   * Create new instnce groups based on the zone to instnce reference map
   *
   * @param apiClient GCP API client
   * @param nodeAzMap zone to instnce reference map
   * @return List of the newly created Backend objects
   */
  @VisibleForTesting
  protected List<Backend> createNewBackends(
      GCPProjectApiClient apiClient,
      String lbName,
      Map<String, List<InstanceReference>> nodeAzMap) {
    List<Backend> backends = new ArrayList<>();
    if (nodeAzMap == null) {
      return backends;
    }
    for (Map.Entry<String, List<InstanceReference>> mapEntry : nodeAzMap.entrySet()) {
      Backend backend = new Backend();
      String zone = mapEntry.getKey();
      List<InstanceReference> instances = mapEntry.getValue();
      if (instances != null && !CollectionUtils.isEmpty(instances)) {
        try {
          String instanceGroupName = getInstanceGroupName(lbName);
          // Left by a failed attempt, or by a failed delete after the zone lost its nodes.
          InstanceGroup existing = apiClient.getInstanceGroupIfExists(zone, instanceGroupName);
          String instanceGroupUrl;
          if (existing != null) {
            instanceGroupUrl = existing.getSelfLink();
            syncInstanceGroupMembers(apiClient, zone, instanceGroupName, instances);
          } else {
            instanceGroupUrl = apiClient.createNewInstanceGroupInZone(zone, instanceGroupName);
            apiClient.addInstancesToInstaceGroup(zone, instanceGroupName, instances);
          }
          backend.setGroup(instanceGroupUrl);
          backends.add(backend);
        } catch (IOException e) {
          log.error(e.getMessage());
          throw new PlatformServiceException(
              INTERNAL_SERVER_ERROR, "Failed to create new instance group in zone " + zone);
        }
      }
    }
    return backends;
  }

  /**
   * Update existing backends (Instance Groups) to ensure that each backend contains the given
   * instances only It adds instances that are not already present in the backend, and removes
   * instances that are no longer required
   *
   * @param apiClient GCP API client
   * @param zonesToBackend Mapping between the zones and backend that is a part of that zone
   * @param zonesToNodes Mapping btween zones and the references for instances that should be a part
   *     of the backend belonging to that zone
   */
  @VisibleForTesting
  protected void updateInstancesInInstanceGroup(
      GCPProjectApiClient apiClient,
      Map<String, Backend> zonesToBackend,
      Map<String, List<InstanceReference>> zonesToNodes) {
    for (Map.Entry<String, List<InstanceReference>> zoneToNodes : zonesToNodes.entrySet()) {
      String zone = zoneToNodes.getKey();
      Backend backend = zonesToBackend.get(zone);
      String instanceGroupUrl = backend.getGroup();
      String instanceGroupName = CloudAPI.getResourceNameFromResourceUrl(instanceGroupUrl);
      InstanceGroup instanceGroup = apiClient.getInstanceGroup(zone, instanceGroupName);
      log.info("Sucessfully fetched instance group " + instanceGroupName);
      try {
        syncInstanceGroupMembers(apiClient, zone, instanceGroupName, zoneToNodes.getValue());
      } catch (IOException e) {
        log.error(e.getMessage());
        throw new PlatformServiceException(
            INTERNAL_SERVER_ERROR, "Unable to update instance groups in zone " + zone);
      }
    }
  }

  // Adds the missing instances to the group and removes the others.
  private void syncInstanceGroupMembers(
      GCPProjectApiClient apiClient,
      String zone,
      String instanceGroupName,
      List<InstanceReference> instances)
      throws IOException {
    Set<InstanceReference> newInstances = new HashSet<>(instances);
    Set<InstanceReference> existingInstances =
        new HashSet<>(apiClient.getInstancesForInstanceGroup(zone, instanceGroupName));
    apiClient.addInstancesToInstaceGroup(
        zone,
        instanceGroupName,
        new ArrayList<>(SetUtils.difference(newInstances, existingInstances)));
    apiClient.removeInstancesFromInstaceGroup(
        zone,
        instanceGroupName,
        new ArrayList<>(SetUtils.difference(existingInstances, newInstances)));
  }

  // Helper function to get map a given list of backends to the zones in which they belong
  private Map<String, Backend> mapBackendsToZones(List<Backend> backends) {
    Map<String, Backend> backendToZoneMap = new HashMap<>();
    for (Backend backend : backends) {
      backendToZoneMap.put(getZoneFromResourceUrl(backend.getGroup()), backend);
    }
    return backendToZoneMap;
  }

  // .../zones/<zone>/instanceGroups/<name>
  private static String getZoneFromResourceUrl(String url) {
    String[] parts = url.split("/");
    return parts[parts.length - 3];
  }

  /**
   * Check and perform modifications on an existing list of bakcends to ensure that the nodes that
   * should be a part of the various AZs
   *
   * @param apiClient GCP API client
   * @param nodeAzMap Mapping from avilablity zone to list of references of instnces that should be
   *     a part of the backend in that AZ
   * @param backends List of existing backends
   * @return List of final backends after the modifications
   */
  @VisibleForTesting
  protected List<Backend> ensureBackends(
      GCPProjectApiClient apiClient,
      String lbName,
      Map<String, List<InstanceReference>> nodeAzMap,
      List<Backend> backends) {
    if (backends == null) {
      backends = new ArrayList<Backend>();
    }
    Map<String, Backend> backendToZoneMap = mapBackendsToZones(backends);
    Set<String> zonesToAdd = SetUtils.difference(nodeAzMap.keySet(), backendToZoneMap.keySet());
    Set<String> zonesToRemove = SetUtils.difference(backendToZoneMap.keySet(), nodeAzMap.keySet());
    Set<String> zonesToUpdate =
        SetUtils.intersection(nodeAzMap.keySet(), backendToZoneMap.keySet());
    Map<String, List<InstanceReference>> nodesInNewZones =
        nodeAzMap.entrySet().stream()
            .filter(mapEntry -> zonesToAdd.contains(mapEntry.getKey()))
            .collect(Collectors.toMap(e -> e.getKey(), e -> e.getValue()));
    Map<String, List<InstanceReference>> nodesInUpdateZones =
        nodeAzMap.entrySet().stream()
            .filter(mapEntry -> zonesToUpdate.contains(mapEntry.getKey()))
            .collect(Collectors.toMap(e -> e.getKey(), e -> e.getValue()));
    // The backend service still uses the groups of removed zones, so manageNodeGroup deletes them
    // after it updates the service.
    backends =
        new ArrayList(
            backendToZoneMap.entrySet().stream()
                .filter(mapEntry -> !zonesToRemove.contains(mapEntry.getKey()))
                .collect(Collectors.toMap(e -> e.getKey(), e -> e.getValue()))
                .values());
    backends.addAll(createNewBackends(apiClient, lbName, nodesInNewZones));
    updateInstancesInInstanceGroup(apiClient, backendToZoneMap, nodesInUpdateZones);
    return backends;
  }

  private Integer getPortForHealthCheck(HealthCheck healthCheck) {
    Protocol healthCheckProtocol = Protocol.valueOf(healthCheck.getType());
    switch (healthCheckProtocol) {
      case TCP:
        return healthCheck.getTcpHealthCheck().getPort();
      case HTTP:
        return healthCheck.getHttpHealthCheck().getPort();
      default:
        throw new PlatformServiceException(
            INTERNAL_SERVER_ERROR,
            "Health check with protocol: " + healthCheckProtocol.name() + " not supported");
    }
  }

  /**
   * Check if the list of health checks need to be updated or not based on the list of protocols and
   * ports For now, only TCP healthchecks are supported
   *
   * @param apiClient GCP API client
   * @param region Region where the health checks exists
   * @param healthCheckConfiguration Health check configuration to be used while creating the health
   *     checks
   * @param healthCheckUrls Existing health checks already present in the backend service
   * @return Updated list of health checks that should be associated with the backend service
   */
  @VisibleForTesting
  protected List<String> ensureHealthChecks(
      GCPProjectApiClient apiClient,
      String region,
      String lbName,
      NLBHealthCheckConfiguration healthCheckConfiguration,
      List<String> healthCheckUrls) {
    List<HealthCheck> healthChecks = new ArrayList();
    Protocol healthCheckProtocol = healthCheckConfiguration.getHealthCheckProtocol();
    // Since GCP currently allows configurtion of a single health check, only the first port from
    // the list of ports is selected
    Integer healthCheckPort = healthCheckConfiguration.getHealthCheckPorts().get(0);
    // This list will always either be empty or a singleton list as GCP doesn't allow multiple
    // health checks
    // However, we are not making that assumption here, to support future changes in the GCP API
    if (healthCheckUrls == null) {
      healthCheckUrls = new ArrayList();
    }
    for (String healthCheckUrl : healthCheckUrls) {
      String healthCheckName = CloudAPI.getResourceNameFromResourceUrl(healthCheckUrl);
      healthChecks.add(apiClient.getRegionalHelathCheckByName(region, healthCheckName));
    }
    List<String> newHealthCheckUrls = new ArrayList();
    Set<Integer> portsWithHealthCheck =
        new HashSet(
            healthChecks.stream()
                .filter(hc -> hc.getType().equals(healthCheckProtocol.name()))
                .map(hc -> getPortForHealthCheck(hc))
                .collect(Collectors.toList()));
    if (!portsWithHealthCheck.contains(healthCheckPort)) {
      log.debug("Creating new health checks on port " + healthCheckPort);
      String healthCheckName = getHealthCheckName(lbName, healthCheckProtocol);
      String requestPath =
          healthCheckProtocol == Protocol.HTTP
              ? healthCheckConfiguration.getHealthCheckPortsToPathsMap().get(healthCheckPort)
              : null;
      // One check per protocol, so a port change updates it instead of leaving the old one.
      HealthCheck existing = apiClient.getRegionalHealthCheckIfExists(region, healthCheckName);
      String newHealthCheckUrl;
      if (existing != null) {
        if (healthCheckProtocol == Protocol.HTTP) {
          existing.getHttpHealthCheck().setPort(healthCheckPort).setRequestPath(requestPath);
        } else {
          existing.getTcpHealthCheck().setPort(healthCheckPort);
        }
        newHealthCheckUrl = apiClient.updateHealthCheck(region, existing);
      } else if (healthCheckProtocol == Protocol.HTTP) {
        newHealthCheckUrl =
            apiClient.createNewHTTPHealthCheckForPort(
                region, healthCheckName, healthCheckPort, requestPath);
      } else {
        newHealthCheckUrl =
            apiClient.createNewTCPHealthCheckForPort(region, healthCheckName, healthCheckPort);
      }
      newHealthCheckUrls.add(newHealthCheckUrl);
      return newHealthCheckUrls;
    } else {
      // Because of the filter applied while creating the portsWithHealthCheck set, this we are sure
      // that type of health check would always be correct
      // Only in case of HTTP health checks, we need to check the path as well
      if (healthCheckProtocol == Protocol.HTTP) {
        HealthCheck existingHealthCheckToCheck =
            healthChecks.stream()
                .filter(hc -> getPortForHealthCheck(hc).equals(healthCheckPort))
                .collect(onlyElement());
        HTTPHealthCheck httpHealthCheck = existingHealthCheckToCheck.getHttpHealthCheck();
        if (!httpHealthCheck
            .getRequestPath()
            .equals(
                healthCheckConfiguration.getHealthCheckPortsToPathsMap().get(healthCheckPort))) {
          try {
            httpHealthCheck =
                httpHealthCheck.setRequestPath(
                    healthCheckConfiguration.getHealthCheckPortsToPathsMap().get(healthCheckPort));
            existingHealthCheckToCheck =
                existingHealthCheckToCheck.setHttpHealthCheck(httpHealthCheck);
            String updatedHealthCheck =
                apiClient.updateHealthCheck(region, existingHealthCheckToCheck);
            newHealthCheckUrls.add(updatedHealthCheck);
            return newHealthCheckUrls;
          } catch (Exception e) {
            log.error(e.getMessage());
            throw new PlatformServiceException(
                INTERNAL_SERVER_ERROR, "Failed to update health check on port " + healthCheckPort);
          }
        }
      }
    }
    return healthCheckUrls;
  }

  /**
   * Returns the name of the health check of a load balancer and protocol: hc-[22 hex
   * characters]-tcp or -http. The name holds a hash of the load balancer name, which can have 63
   * characters. Fixed names let a retry reuse what an earlier attempt created and let
   * deleteManagedLoadBalancer find the checks after the backend service is gone.
   */
  @VisibleForTesting
  static String getHealthCheckName(String lbName, Protocol protocol) {
    return "hc-" + lbNameHash(lbName) + "-" + protocol.name().toLowerCase();
  }

  /** The name of the instance group in each zone of a load balancer: ig-[22 hex characters]. */
  @VisibleForTesting
  static String getInstanceGroupName(String lbName) {
    return "ig-" + lbNameHash(lbName);
  }

  private static String lbNameHash(String lbName) {
    return DigestUtils.sha256Hex(lbName).substring(0, 22);
  }

  /**
   * Deletes the instance groups and health checks that the backend service stopped using. GCP
   * refuses to delete one that a backend service uses, so this runs after the update. It deletes
   * only the names that YBA gives, so a user's own check or group stays. A failed delete logs a
   * warning and does not fail the task.
   */
  private void deleteUnusedGroupsAndChecks(
      GCPProjectApiClient apiClient,
      String region,
      String lbName,
      List<Backend> oldBackends,
      List<Backend> backends,
      List<String> oldHealthChecks,
      List<String> healthChecks) {
    // Compare names: two links to one resource can differ in host or API version.
    Set<String> usedGroups =
        backends.stream().map(b -> zoneAndName(b.getGroup())).collect(Collectors.toSet());
    for (Backend backend : oldBackends) {
      if (usedGroups.contains(zoneAndName(backend.getGroup()))) {
        continue;
      }
      String zone = getZoneFromResourceUrl(backend.getGroup());
      String name = CloudAPI.getResourceNameFromResourceUrl(backend.getGroup());
      if (!name.equals(getInstanceGroupName(lbName))
          && !OLD_INSTANCE_GROUP_NAME.matcher(name).matches()) {
        log.info("Not deleting instance group {} in {}: YBA did not create it", name, zone);
        continue;
      }
      try {
        apiClient.deleteInstanceGroup(zone, name);
      } catch (RuntimeException e) {
        log.warn("Could not delete unused instance group {} in {}", name, zone, e);
      }
    }
    Set<String> usedChecks =
        healthChecks.stream()
            .map(CloudAPI::getResourceNameFromResourceUrl)
            .collect(Collectors.toSet());
    for (String url : oldHealthChecks) {
      String name = CloudAPI.getResourceNameFromResourceUrl(url);
      if (usedChecks.contains(name)) {
        continue;
      }
      if (!name.equals(getHealthCheckName(lbName, Protocol.TCP))
          && !name.equals(getHealthCheckName(lbName, Protocol.HTTP))
          && !OLD_HEALTH_CHECK_NAME.matcher(name).matches()) {
        log.info("Not deleting health check {}: YBA did not create it", name);
        continue;
      }
      try {
        apiClient.deleteRegionalHealthCheck(region, name);
      } catch (RuntimeException e) {
        log.warn("Could not delete unused health check {}", name, e);
      }
    }
  }

  private static String zoneAndName(String instanceGroupUrl) {
    return getZoneFromResourceUrl(instanceGroupUrl)
        + "/"
        + CloudAPI.getResourceNameFromResourceUrl(instanceGroupUrl);
  }

  /**
   * Check if the list of forwarding rules cover all protocls and ports required
   *
   * @param protocol Protocol for the forwarding rule. Only TCP supported for now
   * @param portsToCheck List of ports that should be forwarded by the forwarding service
   * @param forwardingRules List of existing forwarding rules
   */
  @VisibleForTesting
  protected void ensureForwardingRules(
      String protocol, List<Integer> portsToCheck, List<ForwardingRule> forwardingRules) {
    for (ForwardingRule forwardingRule : forwardingRules) {
      if (forwardingRule.getAllPorts() != null && forwardingRule.getAllPorts() == true) {
        return;
        // We found a forwarding rule that forwards all ports. So no need to check
        // if protocol specific ports are forwarded or not
      }
      if (forwardingRule.getIPProtocol().equals(protocol)) {
        portsToCheck.removeAll(
            forwardingRule.getPorts().stream()
                .map(port -> Integer.parseInt(port))
                .collect(Collectors.toList()));
      }
    }
    if (!portsToCheck.isEmpty()) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Forwarding rule missing for some ports: " + portsToCheck.toString());
    }
  }

  // Wrapper function to keep the exposed API consistent, as well as allow for unit testing
  @Override
  public void manageNodeGroup(
      Provider provider,
      String regionCode,
      String lbName,
      Map<AvailabilityZone, Set<NodeID>> azToNodeIDs,
      List<Integer> portsToForward,
      NLBHealthCheckConfiguration healthCheckConfig) {
    GCPProjectApiClient apiClient = getApiClient(provider);
    manageNodeGroup(
        provider,
        regionCode,
        lbName,
        azToNodeIDs,
        "TCP",
        portsToForward,
        healthCheckConfig,
        apiClient);
  }

  /**
   * Update the existing load balancer with the given nodes and parameters. Google Cloud API has no
   * seperate data structure for a load balancer. Thus the load balancer name provided by the user
   * is actually the name of the backendService
   *
   * @param provider the cloud provider bean for the AWS provider.
   * @param regionCode the region code.
   * @param lbName the load balancer name.
   * @param nodeIDs the DB node IDs (name, uuid).
   * @param lbProtocol the protocol that lb would be listenting and forwarding packets for. (Only
   *     TCP supported for now)
   * @param portsToForward the listening ports to be forwarded by the load balancer (eg: YSQL port,
   *     YCQL port, YEDIS port).
   * @param healthCheckConfigurationn configuration to be used when setting up health checks
   * @param apiClient client object to make requests to the Google compute platform
   */
  @VisibleForTesting
  protected void manageNodeGroup(
      Provider provider,
      String regionCode,
      String lbName,
      Map<AvailabilityZone, Set<NodeID>> azToNodeIDs,
      String lbProtocol,
      List<Integer> portsToForward,
      NLBHealthCheckConfiguration healthCheckConfiguration,
      GCPProjectApiClient apiClient) {
    String backendServiceName = lbName;
    if (!lbProtocol.equals("TCP")) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Currently only TCP load balancers are supported.");
    }
    if (portsToForward.isEmpty()) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Load balancer must be configured for atleast one port");
    }
    try {
      // Group NodeIDs based on availablity zones, as backends (InstanceGroups) are per-zone
      Map<String, List<InstanceReference>> nodeAzMap =
          getAzToInstanceReferenceMap(apiClient, azToNodeIDs);
      BackendService backendService = apiClient.getBackendService(regionCode, backendServiceName);
      List<Backend> backends = backendService.getBackends();
      List<Backend> oldBackends = new ArrayList<>(CollectionUtils.emptyIfNull(backends));
      log.debug("Reconciling LB backends....");
      backends = ensureBackends(apiClient, lbName, nodeAzMap, backends);
      Duration connectionDrainingTimeout =
          runtimeConfGetter.getConfForScope(
              provider, ProviderConfKeys.gcpConnectionDrainingTimeout);
      backendService.setConnectionDraining(
          (new ConnectionDraining())
              .setDrainingTimeoutSec((int) connectionDrainingTimeout.getSeconds()));
      backendService.setBackends(backends);
      backendService.setProtocol(lbProtocol);
      log.debug("Checking health checks....");
      List<String> healthChecks = backendService.getHealthChecks();
      List<String> oldHealthChecks = new ArrayList<>(CollectionUtils.emptyIfNull(healthChecks));
      healthChecks =
          ensureHealthChecks(apiClient, regionCode, lbName, healthCheckConfiguration, healthChecks);
      backendService.setHealthChecks(healthChecks);
      apiClient.updateBackendService(regionCode, backendService);
      deleteUnusedGroupsAndChecks(
          apiClient, regionCode, lbName, oldBackends, backends, oldHealthChecks, healthChecks);

      // Get forwarding rules for backend service
      log.debug("Checking forwarding rules....");
      List<ForwardingRule> forwardingRules =
          apiClient.getRegionalForwardingRulesForBackend(regionCode, backendService.getSelfLink());
      ensureForwardingRules(lbProtocol, portsToForward, forwardingRules);
    } catch (Exception e) {
      String message = "Error executing task {manageNodeGroup()} " + e.toString();
      throw new PlatformServiceException(INTERNAL_SERVER_ERROR, message);
    }
  }

  // Managed load balancer methods

  @Override
  public boolean supportsManagedLoadBalancer() {
    return true;
  }

  /**
   * Creates the health check, the INTERNAL backend service, the internal address and the forwarding
   * rule, or reuses the ones with the load balancer's name. manageNodeGroup adds the instance
   * groups. GCP cannot change the ports of a forwarding rule, so a rule that misses a port is
   * replaced; the reserved address keeps the IP.
   *
   * @return the internal IP of the load balancer.
   */
  @Override
  public String ensureManagedLoadBalancer(
      Provider provider,
      String regionCode,
      String name,
      List<AvailabilityZone> zones,
      List<Integer> ports,
      Map<String, String> tags) {
    GCPCloudInfo cloudInfo = CloudInfoInterface.get(provider);
    String vpcProject = GCPUtil.getVpcProject(cloudInfo);
    String network = GCPUtil.getVpcNetwork(cloudInfo);
    if (StringUtils.isEmpty(network)) {
      throw new PlatformServiceException(BAD_REQUEST, "The provider has no VPC network");
    }
    String networkUrl = String.format(GCPUtil.NETWORK_SELFLINK, vpcProject, network);
    String subnetworkUrl =
        String.format(
            GCPUtil.SUBNETWORK_SELFLINK,
            vpcProject,
            regionCode,
            getSubnet(zones, regionCode, name));
    GCPProjectApiClient apiClient = getApiClient(provider);

    BackendService backendService = apiClient.getRegionalBackendServiceIfExists(regionCode, name);
    String backendServiceUrl;
    if (backendService != null) {
      backendServiceUrl = backendService.getSelfLink();
    } else {
      // GCP requires a health check. ensureHealthChecks finds this one by name.
      String healthCheckName = getHealthCheckName(name, Protocol.TCP);
      HealthCheck healthCheck =
          apiClient.getRegionalHealthCheckIfExists(regionCode, healthCheckName);
      String healthCheckUrl =
          healthCheck != null
              ? healthCheck.getSelfLink()
              : apiClient.createNewTCPHealthCheckForPort(regionCode, healthCheckName, ports.get(0));
      backendServiceUrl =
          apiClient.createInternalBackendService(regionCode, name, networkUrl, healthCheckUrl);
    }
    Address address = apiClient.getRegionalAddressIfExists(regionCode, name);
    if (address == null) {
      address = apiClient.createInternalAddress(regionCode, name, subnetworkUrl);
    }
    ForwardingRule rule = apiClient.getRegionalForwardingRuleIfExists(regionCode, name);
    // Read before a replacement, which starts without labels.
    Map<String, String> ruleLabels = rule != null ? rule.getLabels() : null;
    if (rule == null || !forwardsPorts(rule, ports)) {
      if (rule != null) {
        log.info(
            "Replacing forwarding rule {} in {}: it forwards {} and not {}",
            name,
            regionCode,
            rule.getPorts(),
            ports);
        apiClient.deleteRegionalForwardingRule(regionCode, name);
      }
      rule =
          apiClient.createInternalForwardingRule(
              regionCode,
              name,
              backendServiceUrl,
              address.getSelfLink(),
              networkUrl,
              subnetworkUrl,
              ports);
    }
    applyLabels(apiClient, regionCode, name, address, rule, ruleLabels, tags);
    log.info("Load balancer {} in {} has address {}", name, regionCode, address.getAddress());
    return address.getAddress();
  }

  /**
   * Deletes the forwarding rule, the backend service, its health checks and instance groups, then
   * the address: GCP refuses to delete a resource that another still references. The fixed names
   * find the checks and groups of a run that deleted the backend service and then failed; the
   * backend service references also find ones under other names.
   */
  @Override
  public void deleteManagedLoadBalancer(Provider provider, String regionCode, String name) {
    GCPProjectApiClient apiClient = getApiClient(provider);
    apiClient.deleteRegionalForwardingRule(regionCode, name);
    Set<String> healthCheckNames = new LinkedHashSet<>();
    SetMultimap<String, String> instanceGroupsByZone = LinkedHashMultimap.create();
    BackendService backendService = apiClient.getRegionalBackendServiceIfExists(regionCode, name);
    if (backendService != null) {
      for (String url : CollectionUtils.emptyIfNull(backendService.getHealthChecks())) {
        healthCheckNames.add(CloudAPI.getResourceNameFromResourceUrl(url));
      }
      for (Backend backend : CollectionUtils.emptyIfNull(backendService.getBackends())) {
        instanceGroupsByZone.put(
            getZoneFromResourceUrl(backend.getGroup()),
            CloudAPI.getResourceNameFromResourceUrl(backend.getGroup()));
      }
      apiClient.deleteRegionalBackendService(regionCode, name);
    }
    healthCheckNames.add(getHealthCheckName(name, Protocol.TCP));
    healthCheckNames.add(getHealthCheckName(name, Protocol.HTTP));
    Region region = Region.getByCode(provider, regionCode);
    if (region != null) {
      // Inactive zones too: a zone can be deactivated after its group was created.
      for (AvailabilityZone zone : AvailabilityZone.getAZsForRegion(region.getUuid(), false)) {
        instanceGroupsByZone.put(zone.getCode(), getInstanceGroupName(name));
      }
    }
    healthCheckNames.forEach(check -> apiClient.deleteRegionalHealthCheck(regionCode, check));
    instanceGroupsByZone.forEach(apiClient::deleteInstanceGroup);
    apiClient.deleteRegionalAddress(regionCode, name);
    log.info("Deleted load balancer {} in {}", name, regionCode);
  }

  private static boolean forwardsPorts(ForwardingRule rule, List<Integer> ports) {
    if (Boolean.TRUE.equals(rule.getAllPorts())) {
      return true;
    }
    List<String> rulePorts = rule.getPorts() == null ? List.of() : rule.getPorts();
    return ports.stream().map(String::valueOf).allMatch(rulePorts::contains);
  }

  // Only the address and the forwarding rule take labels. Never removes a label; a label error is
  // not worth failing the task.
  private static void applyLabels(
      GCPProjectApiClient apiClient,
      String region,
      String name,
      Address address,
      ForwardingRule rule,
      Map<String, String> ruleLabels,
      Map<String, String> tags) {
    Map<String, String> labels = toLabels(tags);
    if (labels.isEmpty()) {
      return;
    }
    try {
      apiClient.setAddressLabels(
          region, name, address.getLabelFingerprint(), withLabels(address.getLabels(), labels));
      apiClient.setForwardingRuleLabels(
          region, name, rule.getLabelFingerprint(), withLabels(ruleLabels, labels));
    } catch (RuntimeException e) {
      log.warn("Could not update the labels of load balancer {}: {}", name, e.getMessage());
    }
  }

  private static Map<String, String> withLabels(
      Map<String, String> existing, Map<String, String> labels) {
    Map<String, String> merged = new HashMap<>();
    if (existing != null) {
      merged.putAll(existing);
    }
    merged.putAll(labels);
    return merged;
  }

  // GCP labels: lowercase letters, digits, '-' and '_', at most 63 characters, key starts with a
  // letter. A tag whose key cannot become a label is skipped.
  @VisibleForTesting
  static Map<String, String> toLabels(Map<String, String> tags) {
    Map<String, String> labels = new HashMap<>();
    for (Map.Entry<String, String> tag : tags.entrySet()) {
      String key = toLabelPart(tag.getKey());
      if (!key.isEmpty() && Character.isLetter(key.charAt(0))) {
        labels.put(key, toLabelPart(tag.getValue()));
      }
    }
    return labels;
  }

  private static String toLabelPart(String value) {
    String part = StringUtils.defaultString(value).toLowerCase().replaceAll("[^a-z0-9_-]", "_");
    return part.length() > 63 ? part.substring(0, 63) : part;
  }

  // The zones of a GCP region share one subnet.
  private static String getSubnet(List<AvailabilityZone> zones, String regionCode, String name) {
    return zones.stream()
        .map(AvailabilityZone::getSubnet)
        .filter(StringUtils::isNotBlank)
        .findFirst()
        .orElseThrow(
            () ->
                new PlatformServiceException(
                    BAD_REQUEST,
                    "No zone of region " + regionCode + " has a subnet for load balancer " + name));
  }

  @Override
  public Optional<CloudAPI.NodeDiskSpec> describeNodeDataDiskSpec(
      Provider provider, NodeDetails node) {
    if (node == null
        || node.cloudInfo == null
        || StringUtils.isBlank(node.cloudInfo.az)
        || StringUtils.isBlank(node.nodeName)) {
      throw new PlatformServiceException(BAD_REQUEST, "GCP node is missing zone or name");
    }
    try {
      GCPProjectApiClient apiClient = getApiClient(provider);
      return Optional.of(apiClient.describeNodeDataDiskSpec(node.cloudInfo.az, node.nodeName));
    } catch (PlatformServiceException e) {
      throw e;
    } catch (Exception e) {
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR,
          "Failed to describe GCP data disk performance for "
              + node.nodeName
              + ": "
              + e.getMessage());
    }
  }
}
