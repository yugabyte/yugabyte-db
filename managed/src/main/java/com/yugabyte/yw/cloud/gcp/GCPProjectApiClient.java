package com.yugabyte.yw.cloud.gcp;

import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.google.api.client.googleapis.javanet.GoogleNetHttpTransport;
import com.google.api.client.googleapis.json.GoogleJsonResponseException;
import com.google.api.client.http.HttpRequestInitializer;
import com.google.api.client.http.HttpTransport;
import com.google.api.client.json.gson.GsonFactory;
import com.google.api.services.compute.Compute;
import com.google.api.services.compute.model.Backend;
import com.google.api.services.compute.model.BackendService;
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
import com.google.api.services.compute.model.InstanceWithNamedPorts;
import com.google.api.services.compute.model.Operation;
import com.google.api.services.compute.model.TCPHealthCheck;
import com.google.auth.http.HttpCredentialsAdapter;
import com.google.auth.oauth2.GoogleCredentials;
import com.yugabyte.yw.cloud.CloudAPI;
import com.yugabyte.yw.common.CloudUtil.Protocol;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.config.ProviderConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.helpers.CloudInfoInterface;
import com.yugabyte.yw.models.helpers.provider.GCPCloudInfo;
import java.io.ByteArrayInputStream;
import java.io.IOException;
import java.security.GeneralSecurityException;
import java.time.Duration;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.stream.Collectors;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections.CollectionUtils;
import play.libs.Json;

@Slf4j
public class GCPProjectApiClient {

  private Compute compute;
  private String project;
  private RuntimeConfGetter runtimeConfGetter;
  OperationPoller operationPoller;
  private Provider provider;

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

  public void checkInstanceTempelate(String instanceTempelateName)
      throws IOException, GeneralSecurityException, GoogleJsonResponseException {
    compute.instanceTemplates().get(project, instanceTempelateName).execute();
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
    ObjectMapper mapper = Json.mapper();
    JsonNode gcpCredentials = cloudInfo.getGceApplicationCredentials();
    GoogleCredentials credentials =
        GoogleCredentials.fromStream(
            new ByteArrayInputStream(mapper.writeValueAsBytes(gcpCredentials)));
    HttpRequestInitializer requestInitializer = new HttpCredentialsAdapter(credentials);
    HttpTransport httpTransport = GoogleNetHttpTransport.newTrustedTransport();
    // Create Compute Engine object.
    return new Compute.Builder(httpTransport, GsonFactory.getDefaultInstance(), requestInitializer)
        .setApplicationName("")
        .build();
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
}
