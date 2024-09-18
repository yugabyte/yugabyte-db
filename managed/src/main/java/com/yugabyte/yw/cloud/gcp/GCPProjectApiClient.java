package com.yugabyte.yw.cloud.gcp;

import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.google.api.client.googleapis.javanet.GoogleNetHttpTransport;
import com.google.api.client.googleapis.json.GoogleJsonResponseException;
import com.google.api.client.http.HttpBackOffUnsuccessfulResponseHandler;
import com.google.api.client.http.HttpHeaders;
import com.google.api.client.http.HttpRequest;
import com.google.api.client.http.HttpRequestInitializer;
import com.google.api.client.http.HttpTransport;
import com.google.api.client.json.gson.GsonFactory;
import com.google.api.client.util.ExponentialBackOff;
import com.google.api.services.cloudresourcemanager.CloudResourceManager;
import com.google.api.services.cloudresourcemanager.model.TestIamPermissionsRequest;
import com.google.api.services.cloudresourcemanager.model.TestIamPermissionsResponse;
import com.google.api.services.compute.Compute;
import com.google.api.services.compute.model.Backend;
import com.google.api.services.compute.model.BackendService;
import com.google.api.services.compute.model.Firewall;
import com.google.api.services.compute.model.FirewallList;
import com.google.api.services.compute.model.FirewallPolicy;
import com.google.api.services.compute.model.ForwardingRule;
import com.google.api.services.compute.model.ForwardingRuleList;
import com.google.api.services.compute.model.HTTPHealthCheck;
import com.google.api.services.compute.model.HealthCheck;
import com.google.api.services.compute.model.Instance;
import com.google.api.services.compute.model.InstanceGroup;
import com.google.api.services.compute.model.InstanceGroupsAddInstancesRequest;
import com.google.api.services.compute.model.InstanceGroupsListInstances;
import com.google.api.services.compute.model.InstanceGroupsListInstancesRequest;
import com.google.api.services.compute.model.InstanceGroupsRemoveInstancesRequest;
import com.google.api.services.compute.model.InstanceList;
import com.google.api.services.compute.model.InstanceReference;
import com.google.api.services.compute.model.InstanceTemplateList;
import com.google.api.services.compute.model.InstanceWithNamedPorts;
import com.google.api.services.compute.model.Network;
import com.google.api.services.compute.model.NetworkList;
import com.google.api.services.compute.model.Operation;
import com.google.api.services.compute.model.SubnetworkList;
import com.google.api.services.compute.model.TCPHealthCheck;
import com.google.auth.oauth2.ComputeEngineCredentials;
import com.google.auth.oauth2.GoogleCredentials;
import com.yugabyte.yw.cloud.CloudAPI;
import com.yugabyte.yw.common.CloudUtil.Protocol;
import com.yugabyte.yw.common.GCPUtil;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.config.ProviderConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.helpers.CloudInfoInterface;
import com.yugabyte.yw.models.helpers.provider.GCPCloudInfo;
import java.io.ByteArrayInputStream;
import java.io.IOException;
import java.net.URI;
import java.security.GeneralSecurityException;
import java.time.Duration;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.UUID;
import java.util.stream.Collectors;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections4.CollectionUtils;
import org.apache.commons.lang3.StringUtils;
import play.libs.Json;

@Slf4j
public class GCPProjectApiClient {

  private Compute compute;
  private String project;
  private RuntimeConfGetter runtimeConfGetter;
  OperationPoller operationPoller;
  private Provider provider;
  private GoogleCredentials credentials;
  private HttpRequestInitializer requestInitializer;
  private HttpTransport httpTransport;

  public class OperationPoller {
    /**
     * Utility method to make execution blocking until GCP completes resource creation operations We
     * poll the operation object to check if the operation has completed or not
     *
     * @param operation the operation that we are waiting to get completed
     * @return the error, if any, else {@code null} if there was no error
     */
    public void waitForOperationCompletion(Operation operation) {
      Duration pollingInterval =
          runtimeConfGetter.getConfForScope(
              provider, ProviderConfKeys.operationStatusPollingInterval);
      Duration timeoutInterval =
          runtimeConfGetter.getConfForScope(provider, ProviderConfKeys.operationTimeoutInterval);
      long start = System.currentTimeMillis();
      String zone = CloudAPI.getResourceNameFromResourceUrl(operation.getZone());
      String region = CloudAPI.getResourceNameFromResourceUrl(operation.getRegion());
      String status = operation.getStatus();
      String opId = operation.getName();
      try {
        while (operation != null && !status.equals("DONE")) {
          Thread.sleep(pollingInterval.toMillis());
          long elapsed = System.currentTimeMillis() - start;
          if (elapsed >= timeoutInterval.toMillis()) {
            throw new InterruptedException("Timed out waiting for operation to complete");
          }
          log.info("Waiting for operation to complete: " + operation.getName());
          if (zone != null) {
            Compute.ZoneOperations.Get get = compute.zoneOperations().get(project, zone, opId);
            operation = get.execute();
          } else if (region != null) {
            Compute.RegionOperations.Get get =
                compute.regionOperations().get(project, region, opId);
            operation = get.execute();
          } else {
            Compute.GlobalOperations.Get get = compute.globalOperations().get(project, opId);
            operation = get.execute();
          }
          if (operation != null) {
            status = operation.getStatus();
          }
        }
      } catch (InterruptedException e) {
        throw new PlatformServiceException(
            INTERNAL_SERVER_ERROR,
            "Timed out waiting for operation to complete: " + operation.getKind());
      } catch (IOException e) {
        throw new PlatformServiceException(INTERNAL_SERVER_ERROR, "Failed to connect to GCP.");
      }
      if (operation != null && operation.getError() != null) {
        throw new PlatformServiceException(
            INTERNAL_SERVER_ERROR, operation.getError().getErrors().toString());
      }
    }
  }

  public GCPProjectApiClient(RuntimeConfGetter runtimeConfGetter, Provider provider) {
    this.provider = provider;
    this.runtimeConfGetter = runtimeConfGetter;
    GCPCloudInfo cloudInfo = CloudInfoInterface.get(provider);
    try {
      compute = buildComputeClient(cloudInfo);
    } catch (GeneralSecurityException | IOException e) {
      log.error(e.getMessage());
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR, "Failed to initialilze GCP service.");
    }
    project = cloudInfo.getGceProject();
    this.operationPoller = new OperationPoller();
  }

  /**
   * Get details about an Instance Group within a zone from its name
   *
   * @param zone Zone in which the instance group resides
   * @param instanceGroupName Name of the instance group
   * @return InstanceGroup object with details about the instance group
   */
  public InstanceGroup getInstanceGroup(String zone, String instanceGroupName) {
    InstanceGroup instanceGroup;
    try {
      instanceGroup = compute.instanceGroups().get(project, zone, instanceGroupName).execute();
    } catch (GoogleJsonResponseException e) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Failed to fetch instance group name: " + instanceGroupName);
    } catch (IOException e) {
      throw new PlatformServiceException(INTERNAL_SERVER_ERROR, "Failed to connect to GCP.");
    }
    if (instanceGroup == null) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Failed to find instance group with name " + instanceGroupName);
    }
    return instanceGroup;
  }

  public boolean checkInstanceTempelate(String instanceTempelateName) {
    try {
      String filter = "name eq " + instanceTempelateName;
      InstanceTemplateList instanceTemplateList =
          compute.instanceTemplates().list(project).setFilter(filter).execute();
      return instanceTemplateList.getItems() != null;
    } catch (Exception e) {
      log.error("Error in retrieving instance template: ", e);
      throw new PlatformServiceException(
          BAD_REQUEST, "Error in retrieving instance template [check logs for more info]");
    }
  }

  public void checkInstanceFetching() throws IOException, GeneralSecurityException {
    compute.instances().aggregatedList(project).setMaxResults(1L).execute();
  }

  /**
   * Get details about a regionalBackendService from its name
   *
   * @param region Region in which the backend service was created
   * @param backendServiceName Name of the backend service
   * @return BackendService object, containing details about the backend service
   */
  public BackendService getBackendService(String region, String backendServiceName) {
    BackendService backendService;
    try {
      backendService =
          compute.regionBackendServices().get(project, region, backendServiceName).execute();
    } catch (GoogleJsonResponseException e) {
      log.error("Response error = " + e.getMessage());
      throw new PlatformServiceException(BAD_REQUEST, e.getMessage());
    } catch (IOException e) {
      log.error("IO Exception = " + e.toString());
      throw new PlatformServiceException(INTERNAL_SERVER_ERROR, "Failed to connect to GCP.");
    }
    if (backendService == null) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "Cannot find backed service (Load balancer) with given name " + backendServiceName);
    }
    return backendService;
  }

  /**
   * Delete the given list of instance groups from the GCP Project Input is a map from availablity
   * zone to backend instead of just a list of backends to make the request more efficient We delete
   * all the instance groups in a single zone throguh a single HTTP request instead of making a
   * serperate call for each instance group
   *
   * @param zonesToBackend Mapping from Availablity zone to the list of backends in that zone to be
   *     deleted
   * @throws IOException when connection to GCP fails
   */
  public void deleteBackends(Map<String, Backend> zonesToBackend) throws IOException {
    for (Map.Entry<String, Backend> zoneToBackend : zonesToBackend.entrySet()) {
      String zone = zoneToBackend.getKey();
      Backend backend = zoneToBackend.getValue();
      String instanceGroupUrl = backend.getGroup();
      String instanceGroupName = CloudAPI.getResourceNameFromResourceUrl(instanceGroupUrl);
      log.warn("Deleting instance group: " + instanceGroupName);
      Operation response =
          compute.instanceGroups().delete(project, zone, instanceGroupName).execute();
      operationPoller.waitForOperationCompletion(response);
      log.info("Sucessfully deleted instance group: " + instanceGroupName);
    }
  }

  /**
   * Remove the list of instances from the instance group Note that this method does not delete the
   * instances themselves. It just removes the instances form the instance group It also does not
   * delete the instance group if it becomes empty after removing the instances
   *
   * @param zone Zone for the instance group
   * @param instanceGroupName Name of the instance group
   * @param instances List of instance references that need to be removed from the instance group
   * @throws IOException when connection to GCP fails
   */
  public void removeInstancesFromInstaceGroup(
      String zone, String instanceGroupName, List<InstanceReference> instances) throws IOException {
    if (instances != null && !CollectionUtils.isEmpty(instances)) {
      InstanceGroupsRemoveInstancesRequest request = new InstanceGroupsRemoveInstancesRequest();
      request.setInstances(instances);
      log.debug("Removing instances " + instances + "from instance group " + instanceGroupName);
      Operation response =
          compute
              .instanceGroups()
              .removeInstances(project, zone, instanceGroupName, request)
              .execute();
      operationPoller.waitForOperationCompletion(response);
      log.info("Sucessfully removed instances from instance group " + instanceGroupName);
    }
  }

  /**
   * Add the given list of instances to the instance group The instances must be present in the same
   * zone as that of the instance group
   *
   * @param zone Zone in which the instance group and instances are present
   * @param instanceGroupName Name of the instance group
   * @param instances List of instance references for the instances that need to be added to the
   *     instance group
   * @throws IOException when connection to GCP fails
   */
  public void addInstancesToInstaceGroup(
      String zone, String instanceGroupName, List<InstanceReference> instances) throws IOException {
    if (instances != null && !CollectionUtils.isEmpty(instances)) {
      InstanceGroupsAddInstancesRequest request = new InstanceGroupsAddInstancesRequest();
      request.setInstances(instances);
      log.debug("Adding instances " + instances + "to instance group " + instanceGroupName);
      Operation response =
          compute
              .instanceGroups()
              .addInstances(project, zone, instanceGroupName, request)
              .execute();
      operationPoller.waitForOperationCompletion(response);
      log.info("Sucessfully added instances to instance group: " + instanceGroupName);
    }
  }

  /**
   * Creates a new regional TCP health check on the specified port, with default parameters
   *
   * @param region Region for which the health check needs to be created
   * @param port Port at which the health check will probe to check for the health of the VM
   * @return URL for the newly created health check
   * @throws IOException when connection to GCP fails
   */
  public String createNewTCPHealthCheckForPort(String region, Integer port) throws IOException {
    String healthCheckName = "hc-" + port.toString() + UUID.randomUUID().toString();
    HealthCheck healthCheck = new HealthCheck();
    healthCheck.setName(healthCheckName);
    healthCheck.setType(Protocol.TCP.name());
    TCPHealthCheck tcpHealthCheck = new TCPHealthCheck();
    tcpHealthCheck.setPort(port);
    healthCheck.setTcpHealthCheck(tcpHealthCheck);
    log.debug("Creating new health check " + healthCheck);
    Operation response =
        compute.regionHealthChecks().insert(project, region, healthCheck).execute();
    operationPoller.waitForOperationCompletion(response);
    log.info("Sucessfully created new TCP health check for port " + port);
    return response.getTargetLink();
  }

  /**
   * Creates a new regional HTTP health check on the specified port, with default parameters
   *
   * @param region Region for which the health check needs to be created
   * @param port Port at which the health check will probe to check for the health of the VM
   * @param requestPath Path at which the health check will probe to check status
   * @return URL for the newly created health check
   * @throws IOException when connection to GCP fails
   */
  public String createNewHTTPHealthCheckForPort(String region, Integer port, String requestPath)
      throws IOException {
    String healthCheckName = "hc-" + port.toString() + UUID.randomUUID().toString();
    HealthCheck healthCheck = new HealthCheck();
    healthCheck.setName(healthCheckName);
    healthCheck.setType(Protocol.HTTP.name());
    HTTPHealthCheck httpHealthCheck = new HTTPHealthCheck();
    httpHealthCheck.setPort(port);
    httpHealthCheck.setRequestPath(requestPath);
    healthCheck.setHttpHealthCheck(httpHealthCheck);
    log.debug("Creating new health check " + healthCheck);
    Operation response =
        compute.regionHealthChecks().insert(project, region, healthCheck).execute();
    operationPoller.waitForOperationCompletion(response);
    log.info("Sucessfully created new HTTP health check for port " + port);
    return response.getTargetLink();
  }

  public String updateHealthCheck(String region, HealthCheck healthCheck) throws IOException {
    String healthCheckName = healthCheck.getName();
    log.debug("Updating health check " + healthCheck);
    Operation response =
        compute
            .regionHealthChecks()
            .update(project, region, healthCheckName, healthCheck)
            .execute();
    operationPoller.waitForOperationCompletion(response);
    log.info("Sucessfully updated health check " + healthCheckName);
    return response.getTargetLink();
  }

  /**
   * Get details about a regional health check from its name
   *
   * @param region Region in which the health check lives
   * @param healthCheckName Name of the health check
   * @return HelathCheck object containing the details of the health check
   */
  public HealthCheck getRegionalHelathCheckByName(String region, String healthCheckName) {
    try {
      HealthCheck healthCheck =
          compute.regionHealthChecks().get(project, region, healthCheckName).execute();
      return healthCheck;
    } catch (GoogleJsonResponseException e) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Failed to fetch health check for name: " + healthCheckName);
    } catch (IOException e) {
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR, "Failed to fetch health check " + healthCheckName);
    }
  }

  public Compute buildComputeClient(GCPCloudInfo cloudInfo)
      throws GeneralSecurityException, IOException {
    boolean useHostCredentials =
        Optional.ofNullable(cloudInfo.getUseHostCredentials()).orElse(false);
    if (useHostCredentials) {
      log.info("using host's service account credentials for provisioning the provider");
      credentials = ComputeEngineCredentials.create();
    } else {
      ObjectMapper mapper = Json.mapper();
      JsonNode gceCreds = mapper.readTree(cloudInfo.getGceApplicationCredentials());
      credentials =
          GoogleCredentials.fromStream(
              new ByteArrayInputStream(mapper.writeValueAsBytes(gceCreds)));
    }
    requestInitializer = getHttpRequestInitializerWithBackoff();
    httpTransport = GoogleNetHttpTransport.newTrustedTransport();
    // Create Compute Engine object.
    return new Compute.Builder(httpTransport, GsonFactory.getDefaultInstance(), requestInitializer)
        .setApplicationName("")
        .build();
  }

  /**
   * Build a custom HttpRequestInitializer which retries Http requests on 5xx server errors via a
   * handler. Rest of the logic is same as original library code here :
   * https://github.com/googleapis/google-auth-library-java/blob/
   * d42f30acae7c7bd81afbecbfa83ebde5c6db931a/oauth2_http/java/com/
   * google/auth/http/HttpCredentialsAdapter.java#L85
   */
  private HttpRequestInitializer getHttpRequestInitializerWithBackoff() {
    return new HttpRequestInitializer() {
      @Override
      public void initialize(HttpRequest request) throws IOException {
        HttpBackOffUnsuccessfulResponseHandler handler =
            new HttpBackOffUnsuccessfulResponseHandler(new ExponentialBackOff());
        request.setUnsuccessfulResponseHandler(handler);

        if (!credentials.hasRequestMetadata()) {
          return;
        }
        HttpHeaders requestHeaders = request.getHeaders();
        URI uri = null;
        if (request.getUrl() != null) {
          uri = request.getUrl().toURI();
        }
        Map<String, List<String>> credentialHeaders = credentials.getRequestMetadata(uri);
        if (credentialHeaders == null) {
          return;
        }
        for (Map.Entry<String, List<String>> entry : credentialHeaders.entrySet()) {
          String headerName = entry.getKey();
          List<String> requestValues = new ArrayList<>();
          requestValues.addAll(entry.getValue());
          requestHeaders.put(headerName, requestValues);
        }
      }
    };
  }

  /**
   * Create a new instnce group An instance group can only hold instances that are in the same zone
   * as that of the instance group
   *
   * @param zone Zone for which the instance group needs to be created
   * @return URL of the newly created instance group
   * @throws IOException when connection to GCP fails
   */
  public String createNewInstanceGroupInZone(String zone) throws IOException {
    String instanceGroupName = "ig-" + UUID.randomUUID().toString();
    InstanceGroup instanceGroup = new InstanceGroup();
    instanceGroup.setName(instanceGroupName);
    Operation response = compute.instanceGroups().insert(project, zone, instanceGroup).execute();
    operationPoller.waitForOperationCompletion(response);
    log.info("New instance group created with name: " + instanceGroupName);
    return response.getTargetLink();
  }

  /**
   * Get a list of all the regional forwarding rules that have one of the backend as the given
   * backend
   *
   * @param region Region for which the forwarding rules are needed
   * @param backendUrl Url of the backend
   * @return List of ForwardingRule objects
   */
  public List<ForwardingRule> getRegionalForwardingRulesForBackend(
      String region, String backendUrl) {
    List<ForwardingRule> forwardingRules = new ArrayList();
    try {
      Compute.ForwardingRules.List request = compute.forwardingRules().list(project, region);
      request.setFilter("backendService:\"" + backendUrl + "\"");
      ForwardingRuleList response;
      do {
        response = request.execute();
        if (response.getItems() == null) {
          continue;
        }
        for (ForwardingRule forwardingRule : response.getItems()) {
          forwardingRules.add(forwardingRule);
        }
        request.setPageToken(response.getNextPageToken());
      } while (response.getNextPageToken() != null);
      log.info("Sucessfully fetched all forwarding rules");
      return forwardingRules;
    } catch (GoogleJsonResponseException e) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Failed to fetch forwarding rules for region: " + region);
    } catch (IOException e) {
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR, "Failed to fetch forwarding rules for backend " + backendUrl);
    }
  }

  /**
   * Get details for all nodes in a zone froom their names
   *
   * @param zone Zone in which the instance was created
   * @param nodeNames List of names of all instances in the zone, for which details are required
   * @return List of InstanceReference objects, each containing details about one instance
   */
  public List<InstanceReference> getInstancesInZoneByNames(String zone, List<String> nodeNames) {
    List<InstanceReference> instances = new ArrayList<>();
    if (nodeNames.isEmpty()) {
      return instances;
    }
    try {
      String fillterString =
          "name: \"" + nodeNames.stream().collect(Collectors.joining("\" OR name: \"")) + "\"";
      Compute.Instances.List request = compute.instances().list(project, zone);
      request.setFilter(fillterString);
      InstanceList response;
      log.debug("Starting to fetch instance lists for Availablity zone: " + zone);
      do {
        response = request.execute();
        if (response.getItems() == null) {
          continue;
        }
        for (Instance instance : response.getItems()) {
          instances.add((new InstanceReference()).setInstance(instance.getSelfLink()));
        }
      } while (response.getNextPageToken() != null);
    } catch (GoogleJsonResponseException e) {
      throw new PlatformServiceException(BAD_REQUEST, "Failed to fetch instances in zone: " + zone);
    } catch (IOException e) {
      throw new PlatformServiceException(INTERNAL_SERVER_ERROR, "Failed to connect to GCP.");
    }
    if (instances.size() != nodeNames.size()) {
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR, "Could not find details for all nodes in zone: " + zone);
    }
    return instances;
  }

  /**
   * Get a list of all instances that are a part of the given instance group
   *
   * @param zone Zone in which the instance group belongs
   * @param instanceGroupName Name of the instance group
   * @return List of InstanceReference objects, each one containing details about one of the
   *     instacne that is a part of the group
   */
  public List<InstanceReference> getInstancesForInstanceGroup(
      String zone, String instanceGroupName) {
    List<InstanceReference> instances = new ArrayList();
    if (instanceGroupName == null || zone == null) {
      return instances;
    }
    InstanceGroupsListInstancesRequest requestBody = new InstanceGroupsListInstancesRequest();
    try {
      Compute.InstanceGroups.ListInstances request =
          compute.instanceGroups().listInstances(project, zone, instanceGroupName, requestBody);
      InstanceGroupsListInstances response;
      do {
        response = request.execute();
        if (response.getItems() == null) {
          continue;
        }
        for (InstanceWithNamedPorts instanceWithNamedPorts : response.getItems()) {
          instances.add(
              (new InstanceReference()).setInstance(instanceWithNamedPorts.getInstance()));
        }
        request.setPageToken(response.getNextPageToken());
      } while (response.getNextPageToken() != null);
      log.info("Sucessfully fetched instances for instance group " + instanceGroupName);
    } catch (GoogleJsonResponseException e) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Failed to fetch instances for instance group: " + instanceGroupName);
    } catch (IOException e) {
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR, "Error in getting instances for group " + instanceGroupName);
    }
    return instances;
  }

  /**
   * Update details about an existing regional backend service
   *
   * @param region Region in which the backend service was orignally created
   * @param backendService Updated Backend Service object that needs to be written
   * @throws IOException when connection to GCP fails
   */
  public void updateBackendService(String region, BackendService backendService)
      throws IOException {
    String backendServiceUrl = backendService.getSelfLink();
    String backendServiceName = CloudAPI.getResourceNameFromResourceUrl(backendServiceUrl);
    Operation response =
        compute
            .regionBackendServices()
            .update(project, region, backendServiceName, backendService)
            .execute();
    operationPoller.waitForOperationCompletion(response);
  }

  public List<String> checkTagsExistence(
      List<String> firewallTagsList, String vpcNetwork, String vpcProject) {
    try {
      String networkSelfLink = String.format(GCPUtil.NETWORK_SELFLINK, vpcProject, vpcNetwork);
      String pageToken = null;
      String filter = "(network eq " + networkSelfLink + ")(disabled eq false)";
      Compute.Firewalls.List listFirewallsRequest = compute.firewalls().list(vpcProject);
      listFirewallsRequest.setFilter(filter);
      do {
        if (pageToken != null) {
          listFirewallsRequest.setPageToken(pageToken);
        }
        FirewallList firewallList = listFirewallsRequest.execute();
        if (firewallList.getItems() != null) {
          // Filter out rules with null target tags
          List<String> allTags =
              firewallList.getItems().stream()
                  .filter(rule -> rule.getTargetTags() != null)
                  .flatMap(
                      rule -> rule.getTargetTags().stream()) // Flatten target tags from all rules
                  .distinct() // Remove duplicates
                  .collect(Collectors.toList());
          firewallTagsList.removeAll(allTags); // Clear the list
        }
        pageToken = firewallList.getNextPageToken();
      } while (!firewallTagsList.isEmpty() && pageToken != null);
      log.info("Sucessfully fetched all firewall rules");

      return firewallTagsList;
    } catch (Exception e) {
      log.error("Error in retrieving tags: ", e);
      String errorMsg = "Error in retrieving tags [check logs for more info]";
      throw new PlatformServiceException(BAD_REQUEST, errorMsg);
    }
  }

  public boolean checkVpcExistence(String vpcProject, String vpcNetwork) {
    try {
      String filter = "name eq " + vpcNetwork;
      NetworkList network = compute.networks().list(vpcProject).setFilter(filter).execute();
      return network.getItems() != null;
    } catch (Exception e) {
      log.error("Error in retrieving vpc: ", e);
      String errorMsg = "Error in retrieving vpc [check logs for more info]";
      throw new PlatformServiceException(BAD_REQUEST, errorMsg);
    }
  }

  public List<String> testIam(List<String> reqPermissions) {
    try {
      CloudResourceManager crmService =
          new CloudResourceManager.Builder(
                  httpTransport, GsonFactory.getDefaultInstance(), requestInitializer)
              .setApplicationName("")
              .build();

      TestIamPermissionsRequest request =
          new TestIamPermissionsRequest().setPermissions(reqPermissions);
      TestIamPermissionsResponse response =
          crmService.projects().testIamPermissions(project, request).execute();
      // Returns the list of granted permissions from the list of requested permissions
      List<String> grantedPermissions = response.getPermissions();
      if (grantedPermissions != null && !grantedPermissions.isEmpty()) {
        List<String> missingPermissions =
            reqPermissions.stream()
                .filter(p -> !grantedPermissions.contains(p))
                .collect(Collectors.toList());
        return missingPermissions;
      }
      return reqPermissions;
    } catch (Exception e) {
      log.error("Error in testing permissions: ", e);
      String errorMsg = "Error in testing permissions of the SA [check logs for more info]";
      throw new PlatformServiceException(BAD_REQUEST, errorMsg);
    }
  }

  public void checkImageExistence(String imageSelflink) {
    String imageName = imageSelflink.substring(imageSelflink.lastIndexOf("/") + 1);
    String imageProject = Util.extractRegexValue(imageSelflink, "projects/(.*?)/");
    try {
      // Validate existence in the specified image project
      compute.images().get(imageProject, imageName).execute();
    } catch (Exception e) {
      log.error("Error in retrieving images: ", e);
      String errorMsg = "Error in retrieving images [check logs for more info]";
      if (e instanceof GoogleJsonResponseException) {
        errorMsg =
            String.format("The resource image/%s is not found in the given GCP project", imageName);
      }
      throw new PlatformServiceException(BAD_REQUEST, errorMsg);
    }
  }

  public void validateSubnet(
      String regionCode, String subnet, String vpcNetwork, String vpcProject) {
    String errorMsg;
    try {
      String filter = "name eq " + subnet;
      SubnetworkList subnetworks =
          compute.subnetworks().list(vpcProject, regionCode).setFilter(filter).execute();
      if (subnetworks.getItems() == null) {
        errorMsg =
            String.format(
                "The resource subnet/%s is not found in the given GCP project/region", subnet);
        throw new PlatformServiceException(BAD_REQUEST, errorMsg);
      }

      // if subnet exists, check if it is associated to the VPC
      String networkUrl = subnetworks.getItems().get(0).get("network").toString();
      // ex: networkURL:
      // https://www.googleapis.com/compute/v1/projects/<p-name>/global/networks/<n-name>
      if (StringUtils.isEmpty(networkUrl)
          || !networkUrl.substring(networkUrl.lastIndexOf("/") + 1).equals(vpcNetwork)) {
        errorMsg = String.format("The subnet/%s is not associated with the given VPC", subnet);
        throw new PlatformServiceException(BAD_REQUEST, errorMsg);
      }
    } catch (PlatformServiceException e) {
      throw e;
    } catch (Exception e) {
      log.error("Error in retrieving subnets: ", e);
      errorMsg = "Error in retrieving subnets [check logs for more info]";
      throw new PlatformServiceException(BAD_REQUEST, errorMsg);
    }
  }

  public List<Firewall> getFirewallRules(String filter, String vpcProject) {
    List<Firewall> firewalls = new ArrayList<>();
    try {
      Compute.Firewalls.List request = compute.firewalls().list(vpcProject);

      // Optional: Add filter criteria if provided
      if (StringUtils.isNotEmpty(filter)) {
        request.setFilter(filter);
      }
      FirewallList response;
      do {
        response = request.execute();
        if (response.getItems() == null) {
          continue;
        }
        for (Firewall firewall : response.getItems()) {
          firewalls.add(firewall);
        }
        request.setPageToken(response.getNextPageToken());
      } while (response.getNextPageToken() != null);
      log.info("Sucessfully fetched all firewall rules");
      return firewalls;
    } catch (Exception e) {
      log.error("Error in retrieving firewall rules: ", e);
      String errorMsg = "Error in retrieving firewall rules [check logs for more info]";
      throw new PlatformServiceException(BAD_REQUEST, errorMsg);
    }
  }

  /*
   * FirewallPolicy object looks like:
   * {"kind": "compute#firewallPolicy",
   * "id": "651******077",
   * "creationTimestamp": "2024-02-14T23:14:58.536-08:00",
   * "name": "firewall-policy",
   * "description": "Created for GCP Provider Validation",
   * "rules": [
   *  {
   *    "kind": "compute#firewallPolicyRule",
   *    "description": "Exclude communication ....",
   *    "priority": 1000,
   *    "match": {
   *      "destIpRanges": [
   *        "10.0.0.0/8"
   *      ],
   *      "layer4Configs": [
   *        {
   *          "ipProtocol": "all"
   *        }
   *      ]},
   *    "action": "goto_next",
   *    "direction": "EGRESS",
   *    "enableLogging": false,
   *    "ruleTupleCount": 4
   *  }],
   * "fingerprint": "od7ks1hJI=",
   * "selfLink":
   * "https://www.googleapis.com/compute/v1/projects/yugabyte/global/firewallPolicies/policy",
   * "selfLinkWithId":
   * "https://www.googleapis.com/compute/v1/projects/yugabyte/global/firewallPolicies/651***648",
   * "associations": [
   *   {
   *     "name": "projects/yugabyte/global/networks/test",
   *     "attachmentTarget":
   * "https://www.googleapis.com/compute/v1/projects/yugabyte/global/networks/test"
   *   }],
   * "ruleTupleCount": 22}
   */
  public FirewallPolicy getNetworkFirewallPolicy(String vpcNetwork, String vpcProject) {
    FirewallPolicy policy = null;
    try {
      String filter = "name eq " + vpcNetwork;
      NetworkList network = compute.networks().list(vpcProject).setFilter(filter).execute();
      if (network.getItems() != null) {
        Object firewallPolicy = network.getItems().get(0).get("firewallPolicy");
        if (firewallPolicy != null) {
          String firewallPolicyName =
              firewallPolicy.toString().substring(firewallPolicy.toString().lastIndexOf("/") + 1);
          policy = compute.networkFirewallPolicies().get(vpcProject, firewallPolicyName).execute();
          return policy;
        }
      }
      return policy;
    } catch (Exception e) {
      log.error("Error in retrieving firewall policy: ", e);
      String errorMsg = "Error in retrieving firewall policy [check logs for more info]";
      throw new PlatformServiceException(BAD_REQUEST, errorMsg);
    }
  }

  public String getNetworkFirewallPolicyEnforcementOrder(String vpcProject, String vpcNetwork) {
    if (checkVpcExistence(vpcProject, vpcNetwork)) {
      try {
        Network network = compute.networks().get(vpcProject, vpcNetwork).execute();
        return network.getNetworkFirewallPolicyEnforcementOrder();
      } catch (Exception e) {
        log.error("Error in retrieving firewall policy enforcement order: ", e);
        String errorMsg =
            "Error in retrieving firewall policy enforcement order [check logs for more info]";
        throw new PlatformServiceException(BAD_REQUEST, errorMsg);
      }
    }
    return "";
  }
}
