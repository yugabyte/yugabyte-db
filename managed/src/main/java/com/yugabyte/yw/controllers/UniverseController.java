// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;


import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import java.util.Random;

import java.nio.charset.Charset;

import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.common.CertificateHelper;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.EncryptionAtRestKeyParams;
import com.yugabyte.yw.forms.EncryptionAtRestKeyParams.OpType.*;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.ClusterType;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.forms.RollingRestartParams;
import com.yugabyte.yw.metrics.MetricQueryHelper;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import com.yugabyte.yw.models.helpers.TaskType;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.collect.ImmutableList;
import com.google.inject.Inject;
import com.yugabyte.yw.cloud.UniverseResourceDetails;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.tasks.DestroyUniverse;
import com.yugabyte.yw.commissioner.tasks.ReadOnlyClusterDelete;
import com.yugabyte.yw.common.ApiHelper;
import com.yugabyte.yw.common.ApiResponse;
import com.yugabyte.yw.common.kms.services.EncryptionAtRestService;
import com.yugabyte.yw.common.kms.EncryptionAtRestManager;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.HealthCheck;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import org.yb.client.YBClient;
import play.data.FormFactory;
import play.libs.Json;
import play.mvc.Result;
import play.mvc.Results;

import static com.yugabyte.yw.common.PlacementInfoUtil.checkIfNodeParamsValid;
import static com.yugabyte.yw.common.PlacementInfoUtil.updatePlacementInfo;


public class UniverseController extends AuthenticatedController {
  public static final Logger LOG = LoggerFactory.getLogger(UniverseController.class);

  @Inject
  FormFactory formFactory;

  @Inject
  Commissioner commissioner;

  @Inject
  MetricQueryHelper metricQueryHelper;

  @Inject
  play.Configuration appConfig;

  @Inject
  ApiHelper apiHelper;

  @Inject
  EncryptionAtRestManager keyManager;

  // The YB client to use.
  public YBClientService ybService;

  @Inject
  public UniverseController(YBClientService service) {
    this.ybService = service;
  }

  /**
   * API that checks if a Universe with a given name already exists.
   * @return true if universe already exists, false otherwise
   */
  public Result findByName(UUID customerUUID, String universeName) {
    // Verify the customer with this universe is present.
    Customer customer = Customer.get(customerUUID);
    if (customer == null) {
      return ApiResponse.error(BAD_REQUEST, "Invalid Customer UUID: " + customerUUID);
    }
    LOG.info("Finding Universe with name {}.", universeName);
    if (Universe.checkIfUniverseExists(universeName)) {
      return ApiResponse.error(BAD_REQUEST, "Universe already exists");
    } else {
      return ApiResponse.success("Universe does not Exist");
    }
  }

  /**
   * API that binds the UniverseDefinitionTaskParams class by merging
   * the UserIntent with the generated taskParams.
   * @param customerUUID the ID of the customer configuring the Universe.
   * @return UniverseDefinitionTasksParams in a serialized form
   */
  public Result configure(UUID customerUUID) {
    try {
      ObjectNode formData = (ObjectNode)request().body().asJson();

      // Verify the customer with this universe is present.
      Customer customer = Customer.get(customerUUID);
      if (customer == null) {
        return ApiResponse.error(BAD_REQUEST, "Invalid Customer UUID: " + customerUUID);
      }

      JsonNode clustType = formData.get("currentClusterType");
      JsonNode clustOp = formData.get("clusterOperation");
      if (!formData.hasNonNull("currentClusterType") || clustType.asText().isEmpty() ||
          !formData.hasNonNull("clusterOperation") || clustOp.asText().isEmpty()) {
        return ApiResponse.error(BAD_REQUEST, "Invalid currentClusterType or clusterOperation.");
      }
      ClusterType currentClusterType = ClusterType.valueOf(clustType.asText());
      UniverseDefinitionTaskParams.ClusterOperationType clusterOpType =
          UniverseDefinitionTaskParams.ClusterOperationType.valueOf(clustOp.asText());
      UniverseDefinitionTaskParams taskParams = bindFormDataToTaskParams(formData);

      taskParams.currentClusterType = currentClusterType;
      // TODO(Rahul): When we support multiple read only clusters, change clusterType to cluster uuid.
      Cluster c = taskParams.currentClusterType .equals(ClusterType.PRIMARY) ?
          taskParams.getPrimaryCluster() : taskParams.getReadOnlyClusters().get(0);
      if (checkIfNodeParamsValid(taskParams, c)) {
        PlacementInfoUtil.updateUniverseDefinition(taskParams, customer.getCustomerId(), c.uuid, clusterOpType);
      } else {
        return ApiResponse.error(BAD_REQUEST, "Invalid Node/AZ combination for given instance type " +
            c.userIntent.instanceType);
      }

      return ApiResponse.success(taskParams);
    } catch (Exception e) {
      LOG.error("Unable to Configure Universe for Customer with ID {} Failed with message: {}.",
                customerUUID, e);
      return ApiResponse.error(INTERNAL_SERVER_ERROR, e.getMessage());
    }
  }

  /**
   * API that calculates the resource estimate for the NodeDetailSet
   * @param customerUUID the ID of the Customer
   * @return the Result object containing the Resource JSON data.
   */
  public Result getUniverseResources(UUID customerUUID) {
    try {
      ObjectNode formData = (ObjectNode) request().body().asJson();
      UniverseDefinitionTaskParams taskParams = bindFormDataToTaskParams(formData);

      Set<NodeDetails> nodesInCluster = null;

      if (taskParams.currentClusterType.equals(ClusterType.PRIMARY)) {
        nodesInCluster = taskParams.nodeDetailsSet.stream()
                .filter(n -> n.isInPlacement(taskParams.getPrimaryCluster().uuid))
                .collect(Collectors.toSet());
      } else {
        nodesInCluster = taskParams.nodeDetailsSet.stream()
                .filter(n -> n.isInPlacement(taskParams.getReadOnlyClusters().get(0).uuid))
                .collect(Collectors.toSet());
      }
      return ApiResponse.success(UniverseResourceDetails.create(nodesInCluster,
          taskParams));
    } catch (Throwable t) {
      t.printStackTrace();
      return ApiResponse.error(INTERNAL_SERVER_ERROR, t.getMessage());
    }
  }

  /**
   * API that queues a task to create a new universe. This does not wait for the creation.
   * @return result of the universe create operation.
   */
  public Result create(UUID customerUUID) {
    UniverseDefinitionTaskParams taskParams;
    try {
      LOG.info("Create for {}.", customerUUID);
      // Get the user submitted form data.
      ObjectNode formData = (ObjectNode) request().body().asJson();
      taskParams = bindFormDataToTaskParams(formData);
    } catch (Throwable t) {
      return ApiResponse.error(BAD_REQUEST, t.getMessage());
    }
    // Verify the customer with this universe is present.
    Customer customer = Customer.get(customerUUID);
    if (customer == null) {
      return ApiResponse.error(BAD_REQUEST, "Invalid Customer UUID: " + customerUUID);
    }

    if (taskParams.getPrimaryCluster() != null &&
        !Util.isValidUniverseNameFormat(taskParams.getPrimaryCluster().userIntent.universeName)) {
      return ApiResponse.error(BAD_REQUEST, Util.UNIV_NAME_ERROR_MESG);
    }

    try {
      // Set the provider code.
      for (Cluster c : taskParams.clusters) {
        Provider provider = Provider.find.byId(UUID.fromString(c.userIntent.provider));
        c.userIntent.providerType = CloudType.valueOf(provider.code);
        updatePlacementInfo(taskParams.getNodesInCluster(c.uuid), c.placementInfo);
      }

      // Create a new universe. This makes sure that a universe of this name does not already exist
      // for this customer id.
      Universe universe = Universe.create(taskParams, customer.getCustomerId());
      LOG.info("Created universe {} : {}.", universe.universeUUID, universe.name);

      // Add an entry for the universe into the customer table.
      customer.addUniverseUUID(universe.universeUUID);
      customer.save();

      LOG.info("Added universe {} : {} for customer [{}].",
        universe.universeUUID, universe.name, customer.getCustomerId());

      TaskType taskType = TaskType.CreateUniverse;
      Cluster primaryCluster = taskParams.getPrimaryCluster();

      if (primaryCluster != null) {
        if (primaryCluster.userIntent.providerType.equals(CloudType.kubernetes)) {
          taskType = TaskType.CreateKubernetesUniverse;
          taskParams.isKubernetesUniverse = true;
        }

        if (primaryCluster.userIntent.enableNodeToNodeEncrypt ||
                primaryCluster.userIntent.enableClientToNodeEncrypt) {
          if (taskParams.rootCA == null) {
            taskParams.rootCA = CertificateHelper.createRootCA(taskParams.nodePrefix,
                    customerUUID, appConfig.getString("yb.storage.path"));
          }
          // Set the flag to mark the universe as using TLS enabled and therefore not allowing
          // insecure connections.
          taskParams.allowInsecure = false;
        }

        // Setup encryption for the universe
        if (primaryCluster.userIntent.enableEncryptionAtRest) {
          // Generate encryption master key
          byte[] data = keyManager.generateUniverseKey(
                  customerUUID,
                  universe.universeUUID,
                  taskParams.encryptionAtRestConfig,
                  true
          );
          if (data == null || data.length == 0) {
            String errMsg = "Was not able to retrieve a universe key from desired KMS provider. " +
                    "Reverting to unencrypted universe";
            LOG.warn(errMsg);
            primaryCluster.userIntent.enableEncryptionAtRest = false;
          } else {
            int numRotations = keyManager.getNumKeyRotations(
                    customerUUID,
                    universe.universeUUID,
                    taskParams.encryptionAtRestConfig
            );
            taskParams.encryptionKeyFilePath = CertificateHelper.createEncryptionKeyFile(
                    customerUUID,
                    universe.universeUUID,
                    data,
                    appConfig.getString("yb.storage.path"),
                    numRotations
            );
          }
        } else if (
                primaryCluster.userIntent.enableVolumeEncryption
                        && primaryCluster.userIntent.providerType.equals(CloudType.aws)
        ) {
          byte[] cmkArnBytes = keyManager.generateUniverseKey(
                  customerUUID,
                  universe.universeUUID,
                  taskParams.encryptionAtRestConfig,
                  true
          );
          if (cmkArnBytes.length == 0) {
            primaryCluster.userIntent.enableVolumeEncryption = false;
          } else {
            taskParams.cmkArn = new String(cmkArnBytes);
          }
        }
      }

      // Submit the task to create the universe.
      UUID taskUUID = commissioner.submit(taskType, taskParams);
      LOG.info("Submitted create universe for {}:{}, task uuid = {}.",
        universe.universeUUID, universe.name, taskUUID);

      // Add this task uuid to the user universe.
      CustomerTask.create(customer,
                          universe.universeUUID,
                          taskUUID,
                          CustomerTask.TargetType.Universe,
                          CustomerTask.TaskType.Create,
                          universe.name);
      LOG.info("Saved task uuid " + taskUUID + " in customer tasks table for universe " +
        universe.universeUUID + ":" + universe.name);

      ObjectNode resultNode = (ObjectNode)universe.toJson();
      resultNode.put("taskUUID", taskUUID.toString());
      return Results.status(OK, resultNode);
    } catch (Throwable t) {
      LOG.error("Error creating universe", t);
      return ApiResponse.error(INTERNAL_SERVER_ERROR, t.getMessage());
    }
  }

  public Result setUniverseKey(UUID customerUUID, UUID universeUUID) {
    EncryptionAtRestKeyParams taskParams;
    try {
      LOG.info("Updating universe key {} for {}.", customerUUID, universeUUID);
      // Get the user submitted form data.
      ObjectNode formData = (ObjectNode) request().body().asJson();
      taskParams = EncryptionAtRestKeyParams.bindFromFormData(formData);

    } catch (Throwable t) {
      return ApiResponse.error(BAD_REQUEST, t.getMessage());
    }
    Customer customer = Customer.get(customerUUID);
    if (customer == null) {
      return ApiResponse.error(BAD_REQUEST, "Invalid Customer UUID: " + customerUUID);
    }
    // Make sure the universe exists, this method will throw an exception if it does not.
    Universe universe;
    try {
      universe = Universe.get(universeUUID);
    } catch (RuntimeException e) {
      return ApiResponse.error(BAD_REQUEST, "No universe found with UUID: " + universeUUID);
    }

    try {
      TaskType taskType = TaskType.SetUniverseKey;
      switch (taskParams.op) {
        case ENABLE:
          // Generate encryption master key
          byte[] data = keyManager.generateUniverseKey(
                  customerUUID,
                  universeUUID,
                  taskParams.encryptionAtRestConfig
          );
          if (data == null || data.length == 0) {
            String errMsg = "Was not able to retrieve a universe key from desired KMS provider";
            throw new RuntimeException(errMsg);
          }
          int numRotations = keyManager.getNumKeyRotations(
                  customerUUID,
                  universeUUID,
                  taskParams.encryptionAtRestConfig
          );
          taskParams.encryptionKeyFilePath = CertificateHelper.createEncryptionKeyFile(
                  customerUUID,
                  universe.universeUUID,
                  data,
                  appConfig.getString("yb.storage.path"),
                  numRotations
          );
          break;
        case DISABLE:
          break;
        default:
          throw new IllegalArgumentException(
                  String.format("Unsupported key operation: %s", taskParams.op.name())
          );
      }

      Cluster primaryCluster = universe.getUniverseDetails().getPrimaryCluster();
      if (primaryCluster != null) {
        if (primaryCluster.userIntent.providerType.equals(CloudType.kubernetes)) {
          taskParams.isKubernetesUniverse = true;
          taskType = TaskType.SetKubernetesUniverseKey;
        }
      }
      taskParams.expectedUniverseVersion = universe.version;
      UUID taskUUID = commissioner.submit(taskType, taskParams);
      LOG.info("Submitted set universe key for {}:{}, task uuid = {}.",
              universe.universeUUID, universe.name, taskUUID);

      // Add this task uuid to the user universe.
      CustomerTask.create(customer,
              universe.universeUUID,
              taskUUID,
              CustomerTask.TargetType.Universe,
              CustomerTask.TaskType.SetEncryptionKey,
              universe.name);
      LOG.info("Saved task uuid " + taskUUID + " in customer tasks table for universe " +
              universe.universeUUID + ":" + universe.name);

      ObjectNode resultNode = (ObjectNode)universe.toJson();
      resultNode.put("taskUUID", taskUUID.toString());
      return Results.status(OK, resultNode);
    } catch (Exception e) {
      String errMsg = "Error occurred attempting to set the universe encryption key";
      LOG.error(errMsg, e);
      return ApiResponse.error(BAD_REQUEST, errMsg);
    }
  }

  /**
   * API that queues a task to update/edit a universe of a given customer.
   * This does not wait for the completion.
   *
   * @return result of the universe update operation.
   */
  public Result update(UUID customerUUID, UUID universeUUID) {
    UniverseDefinitionTaskParams taskParams;
    try {
      LOG.info("Update {} for {}.", customerUUID, universeUUID);
      // Get the user submitted form data.

      ObjectNode formData = (ObjectNode) request().body().asJson();
      taskParams = bindFormDataToTaskParams(formData);
    } catch (Throwable t) {
      return ApiResponse.error(BAD_REQUEST, t.getMessage());
    }
    // Verify the customer with this universe is present.
    Customer customer = Customer.get(customerUUID);
    if (customer == null) {
      return ApiResponse.error(BAD_REQUEST, "Invalid Customer UUID: " + customerUUID);
    }

    // Get the universe. This makes sure that a universe of this name does exist
    // for this customer id.
    Universe universe = null;
    try {
      universe = Universe.get(universeUUID);
    } catch (RuntimeException re) {
      String errMsg= "Invalid universe UUID: " + universeUUID;
      LOG.error(errMsg);
      return ApiResponse.error(BAD_REQUEST, errMsg);
    }

    if (!universe.getUniverseDetails().isUniverseEditable()) {
      String errMsg= "Universe UUID " + universeUUID + " cannot be edited.";
      LOG.error(errMsg);
      return ApiResponse.error(BAD_REQUEST, errMsg);
    }

    if (universe.nodesInTransit()) {
      return ApiResponse.error(BAD_REQUEST, "Cannot perform an edit operation on universe " +
                               universeUUID + " as it has nodes in one of " +
                               NodeDetails.IN_TRANSIT_STATES + " states.");
    }

    try {
      Cluster primaryCluster = taskParams.getPrimaryCluster();
      UUID uuid = null;
      PlacementInfo placementInfo = null;
      TaskType taskType = TaskType.EditUniverse;
      if (primaryCluster == null) {
        // Update of a read only cluster.
        List<Cluster> readReplicaClusters = taskParams.getReadOnlyClusters();
        if (readReplicaClusters.size() != 1) {
          String errMsg = "Can only have one read-only cluster per edit/update for now, found " +
                          readReplicaClusters.size();
          LOG.error(errMsg);
          return ApiResponse.error(BAD_REQUEST, errMsg);
        }
        Cluster cluster = readReplicaClusters.get(0);
        uuid = cluster.uuid;
        placementInfo = cluster.placementInfo;
      } else {
        uuid = primaryCluster.uuid;
        placementInfo = primaryCluster.placementInfo;

        if (primaryCluster.userIntent.providerType.equals(CloudType.kubernetes)) {
          taskType = TaskType.EditKubernetesUniverse;
          taskParams.isKubernetesUniverse = true;
        }
        Map<String, String> encryptionAtRestConfig = universe.getEncryptionAtRestConfig();
        int numRotations = keyManager.getNumKeyRotations(
                customerUUID,
                universeUUID,
                encryptionAtRestConfig
        );
        if (universe.isEncryptedAtRest()) {
          taskParams.encryptionKeyFilePath = CertificateHelper.getEncryptionFile(
                  customerUUID,
                  universe.universeUUID,
                  appConfig.getString("yb.storage.path"),
                  numRotations
          );
        }
      }

      updatePlacementInfo(taskParams.getNodesInCluster(uuid), placementInfo);

      taskParams.rootCA = universe.getUniverseDetails().rootCA;
      LOG.info("Found universe {} : name={} at version={}.",
               universe.universeUUID, universe.name, universe.version);

      UUID taskUUID = commissioner.submit(taskType, taskParams);
      LOG.info("Submitted edit universe for {} : {}, task uuid = {}.",
               universe.universeUUID, universe.name, taskUUID);

      // Add this task uuid to the user universe.
      CustomerTask.create(customer,
                          universe.universeUUID,
                          taskUUID,
                          primaryCluster == null 
                            ? CustomerTask.TargetType.Cluster 
                            : CustomerTask.TargetType.Universe,
                          CustomerTask.TaskType.Update,
                          universe.name);
      LOG.info("Saved task uuid {} in customer tasks table for universe {} : {}.", taskUUID,
               universe.universeUUID, universe.name);
      ObjectNode resultNode = (ObjectNode)universe.toJson();
      resultNode.put("taskUUID", taskUUID.toString());
      return Results.status(OK, resultNode);
    } catch (Throwable t) {
      LOG.error("Error updating universe", t);
      return ApiResponse.error(INTERNAL_SERVER_ERROR, t.getMessage());
    }
  }

  /**
   * List the universes for a given customer.
   *
   * @return
   */
  public Result list(UUID customerUUID) {
    // Verify the customer is present.
    Customer customer = Customer.get(customerUUID);
    if (customer == null) {
      return ApiResponse.error(BAD_REQUEST, "Invalid Customer UUID: " + customerUUID);
    }
    ArrayNode universes = Json.newArray();
    // TODO: Restrict the list api json payload, possibly to only include UUID, Name etc
    for (Universe universe: customer.getUniverses()) {
      ObjectNode universePayload = (ObjectNode) universe.toJson();
      try {
        UniverseResourceDetails details = UniverseResourceDetails.create(universe.getNodes(),
            universe.getUniverseDetails());
        universePayload.put("pricePerHour", details.pricePerHour);
      } catch (Exception e) {
        LOG.error("Unable to fetch cost for universe {}.", universe.universeUUID);
      }
      universes.add(universePayload);
    }
    return ApiResponse.success(universes);
  }

  /**
   * Mark whether the universe needs to be backed up or not.
   *
   * @return Result
   */
  public Result setBackupFlag(UUID customerUUID, UUID universeUUID) {
    Customer customer = Customer.get(customerUUID);
    if (customer == null) {
      return ApiResponse.error(BAD_REQUEST, "Invalid Customer UUID: " + customerUUID);
    }
    Universe universe = Universe.get(universeUUID);
    if (universe == null) {
      return ApiResponse.error(BAD_REQUEST, "Invalid Universe UUID: " + universeUUID);
    }
    String active = "false";
    Map<String, String> config = new HashMap<>();
    try {
      if (request().getQueryString("markActive") != null) {
        active = request().getQueryString("markActive");
        config.put("takeBackups", active);
      } else {
        return ApiResponse.error(BAD_REQUEST, "Invalid Query: Need to specify markActive value");
      }
      universe.setConfig(config);
      return ApiResponse.success();
    } catch (Exception e) {
      return ApiResponse.error(INTERNAL_SERVER_ERROR, e);
    }
  }

  public Result index(UUID customerUUID, UUID universeUUID) {
    Customer customer = Customer.get(customerUUID);
    if (customer == null) {
      return ApiResponse.error(BAD_REQUEST, "Invalid Customer UUID: " + customerUUID);
    }
    try {
      Universe universe = Universe.get(universeUUID);
      return Results.status(OK, universe.toJson());
    } catch (RuntimeException e) {
      return ApiResponse.error(BAD_REQUEST, "Invalid Universe UUID: " + universeUUID);
    }
  }

  public Result destroy(UUID customerUUID, UUID universeUUID) {
    LOG.info("Destroy universe, customer uuid: {}, universeUUID: {} ", customerUUID, universeUUID);

    Boolean isForceDelete = false;
    if (request().getQueryString("isForceDelete") != null) {
      isForceDelete = Boolean.valueOf(request().getQueryString("isForceDelete"));
    }

    // Verify the customer with this universe is present.
    Customer customer = Customer.get(customerUUID);
    if (customer == null) {
      return ApiResponse.error(BAD_REQUEST, "Invalid Customer UUID: " + customerUUID);
    }

    Universe universe;
    // Make sure the universe exists, this method will throw an exception if it does not.
    try {
      universe = Universe.get(universeUUID);
    } catch (RuntimeException e) {
      return ApiResponse.error(BAD_REQUEST, "No universe found with UUID: " + universeUUID);
    }

    // Create the Commissioner task to destroy the universe.
    DestroyUniverse.Params taskParams = new DestroyUniverse.Params();
    taskParams.universeUUID = universeUUID;
    // There is no staleness of a delete request. Perform it even if the universe has changed.
    taskParams.expectedUniverseVersion = -1;
    taskParams.customerUUID = customerUUID;
    taskParams.isForceDelete = isForceDelete;
    // Submit the task to destroy the universe.
    TaskType taskType = TaskType.DestroyUniverse;
    UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
    Cluster primaryCluster = universeDetails.getPrimaryCluster();
    if (primaryCluster.userIntent.providerType.equals(CloudType.kubernetes)) {
      taskType = TaskType.DestroyKubernetesUniverse;
    }

    // Update all current tasks for this universe to be marked as done if it is a force delete.
    if (isForceDelete) {
      markAllUniverseTasksAsCompleted(universe.universeUUID);
    }

    UUID taskUUID = commissioner.submit(taskType, taskParams);
    LOG.info("Submitted destroy universe for " + universeUUID + ", task uuid = " + taskUUID);

    // Add this task uuid to the user universe.
    CustomerTask.create(customer,
      universe.universeUUID,
      taskUUID,
      CustomerTask.TargetType.Universe,
      CustomerTask.TaskType.Delete,
      universe.name);

    // If the universe was created with universe encryption at rest key created through an
    // integration, then we should remove the key history from local storage to enable safe
    // deletion of the KMS configuration only when all associated universes have been deleted
    if (universe.isEncryptedAtRest() || keyManager.getNumKeyRotations(
            customerUUID,
            universeUUID,
            universe.getEncryptionAtRestConfig()) > 0) {
      keyManager.clearUniverseKeyHistory(customerUUID, universeUUID);
    }

    LOG.info("Destroyed universe " + universeUUID + " for customer [" + customer.name + "]");

    ObjectNode response = Json.newObject();
    response.put("taskUUID", taskUUID.toString());
    return ApiResponse.success(response);
  }

  /**
   * API that queues a task to create a read-only cluster in an existing universe.
   * @return result of the cluster create operation.
   */
  public Result clusterCreate(UUID customerUUID, UUID universeUUID) {
    UniverseDefinitionTaskParams taskParams;
    try {
      LOG.info("Create cluster for {} in {}.", customerUUID, universeUUID);
      // Get the user submitted form data.
      ObjectNode formData = (ObjectNode) request().body().asJson();
      taskParams = bindFormDataToTaskParams(formData);
    } catch (Throwable t) {
      return ApiResponse.error(BAD_REQUEST, t.getMessage());
    }
    // Verify the customer with this universe is present.
    Customer customer = Customer.get(customerUUID);
    if (customer == null) {
      return ApiResponse.error(BAD_REQUEST, "Invalid Customer UUID: " + customerUUID);
    }

    Universe universe = null;
    try {
      universe = Universe.get(universeUUID);
    } catch (RuntimeException e) {
      return ApiResponse.error(BAD_REQUEST, "No universe found with UUID: " + universeUUID);
    }

    try {
      if (taskParams.clusters == null || taskParams.clusters.size() != 1) {
        return ApiResponse.error(BAD_REQUEST, "Invalid 'clusters' field/size: " +
                                              taskParams.clusters + " for " + universeUUID);
      }

      List<Cluster> newReadOnlyClusters = taskParams.clusters;
      List<Cluster> existingReadOnlyClusters = universe.getUniverseDetails().getReadOnlyClusters();
      LOG.info("newReadOnly={}, existingRO={}.",
               newReadOnlyClusters.size(), existingReadOnlyClusters.size());

      if (existingReadOnlyClusters.size() > 0 && newReadOnlyClusters.size() > 0) {
        String errMsg = "Can only have one read-only cluster per universe for now.";
        LOG.error(errMsg);
        return ApiResponse.error(BAD_REQUEST, errMsg);
      }

      if (newReadOnlyClusters.size() != 1) {
        String errMsg = "Only one read-only cluster expected, but we got " +
                        newReadOnlyClusters.size();
        LOG.error(errMsg);
        return ApiResponse.error(BAD_REQUEST, errMsg);
      }

      Cluster cluster = newReadOnlyClusters.get(0);
      if (cluster.uuid == null) {
        String errMsg = "UUID of read-only cluster should be non-null.";
        LOG.error(errMsg);
        return ApiResponse.error(BAD_REQUEST, errMsg);
      }

      if (cluster.clusterType != ClusterType.ASYNC) {
        String errMsg = "Read-only cluster type should be " + ClusterType.ASYNC + " but is " +
                        cluster.clusterType;
        LOG.error(errMsg);
        return ApiResponse.error(BAD_REQUEST, errMsg);
      }

      // Set the provider code.
      Cluster c = taskParams.clusters.get(0);
      Provider provider = Provider.find.byId(UUID.fromString(c.userIntent.provider));
      c.userIntent.providerType = CloudType.valueOf(provider.code);
      updatePlacementInfo(taskParams.getNodesInCluster(c.uuid), c.placementInfo);

      // Submit the task to create the cluster.
      UUID taskUUID = commissioner.submit(TaskType.ReadOnlyClusterCreate, taskParams);
      LOG.info("Submitted create cluster for {}:{}, task uuid = {}.",
               universe.universeUUID, universe.name, taskUUID);

      // Add this task uuid to the user universe.
      CustomerTask.create(customer,
                          universe.universeUUID,
                          taskUUID,
                          CustomerTask.TargetType.Cluster,
                          CustomerTask.TaskType.Create,
                          universe.name);
      LOG.info("Saved task uuid {} in customer tasks table for universe {}:{}",
               taskUUID, universe.universeUUID, universe.name);

      ObjectNode resultNode = (ObjectNode)universe.toJson();
      resultNode.put("taskUUID", taskUUID.toString());
      return Results.status(OK, resultNode);
    } catch (Throwable t) {
      LOG.error("Error creating cluster", t);
      return ApiResponse.error(INTERNAL_SERVER_ERROR, t.getMessage());
    }
  }

  /**
   * API that queues a task to delete a read-only cluster in an existing universe.
   * @return result of the cluster delete operation.
   */
  public Result clusterDelete(UUID customerUUID, UUID universeUUID, UUID clusterUUID) {
    // Verify the customer with this universe is present.
    Customer customer = Customer.get(customerUUID);
    if (customer == null) {
      return ApiResponse.error(BAD_REQUEST, "Invalid Customer UUID: " + customerUUID);
    }

    Universe universe;
    try {
      universe = Universe.get(universeUUID);
    } catch (RuntimeException e) {
      return ApiResponse.error(BAD_REQUEST, "No universe found with UUID: " + universeUUID);
    }

    List<Cluster> existingReadOnlyClusters = universe.getUniverseDetails().getReadOnlyClusters();
    if (existingReadOnlyClusters.size() != 1) {
      String errMsg = "Expected just one read only cluster, but found " +
                      existingReadOnlyClusters.size();
      LOG.error(errMsg);
      return ApiResponse.error(BAD_REQUEST, errMsg);
    }

    Cluster cluster = existingReadOnlyClusters.get(0);
    UUID uuid = cluster.uuid;
    if (!uuid.equals(clusterUUID)) {
      String errMsg = "Uuid " + clusterUUID + " to delete cluster not found, only " +
                      uuid + " found.";
      LOG.error(errMsg);
      return ApiResponse.error(BAD_REQUEST, errMsg);
    }

    Boolean isForceDelete = false;
    if (request().getQueryString("isForceDelete") != null) {
      isForceDelete = Boolean.valueOf(request().getQueryString("isForceDelete"));
    }

    try {
      // Create the Commissioner task to destroy the universe.
      ReadOnlyClusterDelete.Params taskParams = new ReadOnlyClusterDelete.Params();
      taskParams.universeUUID = universeUUID;
      taskParams.clusterUUID = clusterUUID;
      taskParams.isForceDelete = isForceDelete;
      taskParams.expectedUniverseVersion = universe.version;

      // Submit the task to delete the cluster.
      UUID taskUUID = commissioner.submit(TaskType.ReadOnlyClusterDelete, taskParams);
      LOG.info("Submitted delete cluster for {} in {}, task uuid = {}.",
               clusterUUID, universe.name, taskUUID);

      // Add this task uuid to the user universe.
      CustomerTask.create(customer,
                          universe.universeUUID,
                          taskUUID,
                          CustomerTask.TargetType.Cluster,
                          CustomerTask.TaskType.Delete,
                          universe.name);
      LOG.info("Saved task uuid {} in customer tasks table for universe {}:{}",
               taskUUID, universe.universeUUID, universe.name);

      ObjectNode resultNode = (ObjectNode)universe.toJson();
      resultNode.put("taskUUID", taskUUID.toString());
      return Results.status(OK, resultNode);
    } catch (Throwable t) {
      LOG.error("Error deleting cluster ", t);
      return ApiResponse.error(INTERNAL_SERVER_ERROR, t.getMessage());
    }
  }

  public Result universeCost(UUID customerUUID, UUID universeUUID) {
    // Verify the customer with this universe is present.
    Customer customer = Customer.get(customerUUID);
    if (customer == null) {
      return ApiResponse.error(BAD_REQUEST, "Invalid Customer UUID: " + customerUUID);
    }

    Universe universe;
    // Make sure the universe exists, this method will throw an exception if it does not.
    try {
      universe = Universe.get(universeUUID);
    } catch (RuntimeException e) {
      return ApiResponse.error(BAD_REQUEST, "No universe found with UUID: " + universeUUID);
    }
    try {
      return ApiResponse.success(Json.toJson(UniverseResourceDetails.create(universe.getNodes(),
          universe.getUniverseDetails())));
    } catch (Exception e) {
      return ApiResponse.error(INTERNAL_SERVER_ERROR,
        "Error getting cost for customer " + customerUUID);
    }
  }

  public Result universeListCost(UUID customerUUID) {
    Customer customer = Customer.get(customerUUID);
    if (customer == null) {
      return ApiResponse.error(BAD_REQUEST, "Invalid Customer UUID: " + customerUUID);
    }
    ArrayNode response = Json.newArray();
    Set<Universe> universeSet = null;
    try {
      universeSet = customer.getUniverses();
    } catch (RuntimeException e) {
      return ApiResponse.error(BAD_REQUEST, "No universe found for customer with ID: " + customerUUID);
    }
    for (Universe universe : universeSet) {
      try {
        response.add(Json.toJson(UniverseResourceDetails.create(universe.getNodes(),
            universe.getUniverseDetails())));
      } catch (Exception e) {
        LOG.error("Could not add cost details for Universe with UUID: " + universe.universeUUID);
      }
    }
    return ApiResponse.success(response);
  }

  /**
   * API that queues a task to perform an upgrade and a subsequent rolling restart of a universe.
   *
   * @return result of the universe update operation.
   */
  public Result upgrade(UUID customerUUID, UUID universeUUID) {
    LOG.info("Upgrade {} for {}.", customerUUID, universeUUID);

    // Verify the customer with this universe is present.
    Customer customer = Customer.get(customerUUID);
    if (customer == null) {
      return ApiResponse.error(BAD_REQUEST, "Invalid Customer UUID: " + customerUUID);
    }

    // Get the universe. This makes sure that a universe of this name does exist
    // for this customer id.
    Universe universe = null;
    try {
      universe = Universe.get(universeUUID);
    } catch (RuntimeException e) {
      return ApiResponse.error(BAD_REQUEST, "No universe found with UUID: " + universeUUID);
    }

    // Bind rolling restart params
    RollingRestartParams taskParams;
    try {
      ObjectNode formData = (ObjectNode) request().body().asJson();
      taskParams = (RollingRestartParams) bindFormDataToTaskParams(formData, true);

      if (taskParams.taskType == null) {
        return ApiResponse.error(BAD_REQUEST, "task type is required");
      }

      if (taskParams.rollingUpgrade && universe.nodesInTransit()) {
        return ApiResponse.error(BAD_REQUEST, "Cannot perform rolling upgrade of universe " +
                                 universeUUID + " as it has nodes in one of " +
                                 NodeDetails.IN_TRANSIT_STATES + " states.");
      }

      // TODO: we need to refactor this to read from cluster
      // instead of top level task param, for now just copy the master flag and tserver flag
      // from primary cluster.
      UserIntent primaryIntent = taskParams.getPrimaryCluster().userIntent;
      taskParams.masterGFlags = primaryIntent.masterGFlags;
      taskParams.tserverGFlags = primaryIntent.tserverGFlags;
    } catch (Throwable t) {
      return ApiResponse.error(BAD_REQUEST, t.getMessage());
    }

    try {
      CustomerTask.TaskType customerTaskType = null;
      // Validate if any required params are missed based on the taskType
      switch(taskParams.taskType) {
        case Software:
          customerTaskType = CustomerTask.TaskType.UpgradeSoftware;
          if (taskParams.ybSoftwareVersion == null || taskParams.ybSoftwareVersion.isEmpty()) {
            return ApiResponse.error(
                BAD_REQUEST,
                "ybSoftwareVersion param is required for taskType: " + taskParams.taskType);
          }
          break;
        case GFlags:
          customerTaskType = CustomerTask.TaskType.UpgradeGflags;
          if ((taskParams.masterGFlags == null || taskParams.masterGFlags.isEmpty()) &&
              (taskParams.tserverGFlags == null || taskParams.tserverGFlags.isEmpty())) {
            return ApiResponse.error(
                BAD_REQUEST,
                "gflags param is required for taskType: " + taskParams.taskType);
          }
          UserIntent univIntent = universe.getUniverseDetails().getPrimaryCluster().userIntent;
          if (taskParams.masterGFlags != null &&
              taskParams.masterGFlags.equals(univIntent.masterGFlags) &&
              taskParams.tserverGFlags != null &&
              taskParams.tserverGFlags.equals(univIntent.tserverGFlags)) {
            return ApiResponse.error(
                BAD_REQUEST, "Neither master nor tserver gflags changed.");
          }
          break;
      }

      LOG.info("Got task type {}", customerTaskType.toString());

      taskParams.universeUUID = universe.universeUUID;
      taskParams.expectedUniverseVersion = universe.version;
      LOG.info("Found universe {} : name={} at version={}.",
        universe.universeUUID, universe.name, universe.version);

      TaskType taskType = TaskType.UpgradeUniverse;
      if (taskParams.getPrimaryCluster().userIntent.providerType.equals(CloudType.kubernetes)) {
        taskType = TaskType.UpgradeKubernetesUniverse;
      }

      UUID taskUUID = commissioner.submit(taskType, taskParams);
      LOG.info("Submitted upgrade universe for {} : {}, task uuid = {}.",
        universe.universeUUID, universe.name, taskUUID);

      // Add this task uuid to the user universe.
      CustomerTask.create(customer,
        universe.universeUUID,
        taskUUID,
        CustomerTask.TargetType.Universe,
        customerTaskType,
        universe.name);
      LOG.info("Saved task uuid {} in customer tasks table for universe {} : {}.", taskUUID,
        universe.universeUUID, universe.name);
      ObjectNode resultNode = Json.newObject();
      resultNode.put("taskUUID", taskUUID.toString());
      return Results.status(OK, resultNode);
    } catch (Throwable t) {
      LOG.error("Error updating universe", t);
      return ApiResponse.error(INTERNAL_SERVER_ERROR, t.getMessage());
    }
  }

  /**
   * API that checks the status of the the tservers and masters in the universe.
   *
   * @return result of the universe status operation.
   */
  public Result status(UUID customerUUID, UUID universeUUID) {
    // Verify the customer with this universe is present.
    Customer customer = Customer.get(customerUUID);
    if (customer == null) {
      return ApiResponse.error(BAD_REQUEST, "Invalid Customer UUID: " + customerUUID);
    }

    // Make sure the universe exists, this method will throw an exception if it does not.
    Universe universe;
    try {
      universe = Universe.get(universeUUID);
    } catch (RuntimeException e) {
      return ApiResponse.error(BAD_REQUEST, "No universe found with UUID: " + universeUUID);
    }

    // Get alive status
    try {
      JsonNode result = PlacementInfoUtil.getUniverseAliveStatus(universe, metricQueryHelper);
      return result.has("error") ? ApiResponse.error(BAD_REQUEST, result.get("error")) : ApiResponse.success(result);
    } catch (RuntimeException e) {
      return ApiResponse.error(BAD_REQUEST, e.getMessage());
    }
  }

  /**
   * API that checks the health of all the tservers and masters in the universe, as well as certain
   * conditions on the machines themselves, such as disk utilization, presence of FATAL or core
   * files, etc.
   *
   * @return result of the checker script
   */
  public Result healthCheck(UUID customerUUID, UUID universeUUID) {
    // Verify the customer with this universe is present.
    Customer customer = Customer.get(customerUUID);
    if (customer == null) {
      return ApiResponse.error(BAD_REQUEST, "Invalid Customer UUID: " + customerUUID);
    }

    // Make sure the universe exists, this method will throw an exception if it does not.
    Universe universe;
    try {
      universe = Universe.get(universeUUID);
    } catch (RuntimeException e) {
      return ApiResponse.error(BAD_REQUEST, "No universe found with UUID: " + universeUUID);
    }

    // Get alive status
    try {
      List<HealthCheck> checks = HealthCheck.getAll(universeUUID);
      if (checks == null) {
        return ApiResponse.error(BAD_REQUEST, "No health check record for universe UUID: "
            + universeUUID);
      }
      ArrayNode detailsList = Json.newArray();
      for (HealthCheck check : checks) {
        detailsList.add(Json.stringify(Json.parse(check.detailsJson)));
      }
      return ApiResponse.success(detailsList);
    } catch (RuntimeException e) {
      return ApiResponse.error(BAD_REQUEST, e.getMessage());
    }
  }

  /**
   * Endpoint to retrieve the IP of the master leader for a given universe.
   *
   * @param customerUUID UUID of Customer the target Universe belongs to.
   * @param universeUUID UUID of Universe to retrieve the master leader private IP of.
   * @return The private IP of the master leader.
   */
  public Result getMasterLeaderIP(UUID customerUUID, UUID universeUUID) {
    // Verify the customer with this universe is present.
    Customer customer = Customer.get(customerUUID);
    if (customer == null) {
      return ApiResponse.error(BAD_REQUEST, "Invalid Customer UUID: " + customerUUID);
    }

    // Make sure the universe exists, this method will throw an exception if it does not.
    Universe universe;
    try {
      universe = Universe.get(universeUUID);
    } catch (RuntimeException e) {
      return ApiResponse.error(BAD_REQUEST, "No universe found with UUID: " + universeUUID);
    }
    final String hostPorts = universe.getMasterAddresses();
    String certificate = universe.getCertificate();
    YBClient client = null;
    // Get and return Leader IP
    try {
      client = ybService.getClient(hostPorts, certificate);
      ObjectNode result = Json.newObject().put("privateIP", client.getLeaderMasterHostAndPort().getHostText());
      ybService.closeClient(client, hostPorts);
      return ApiResponse.success(result);
    } catch (RuntimeException e) {
      return ApiResponse.error(BAD_REQUEST, e.getMessage());
    } finally {
      ybService.closeClient(client, hostPorts);
    }
  }

  private void markAllUniverseTasksAsCompleted(UUID universeUUID) {
    List<CustomerTask> existingTasks = CustomerTask.findIncompleteByTargetUUID(universeUUID);
    if (existingTasks == null) {
      return;
    }
    for (CustomerTask task : existingTasks) {
      task.markAsCompleted();
      TaskInfo taskInfo = TaskInfo.get(task.getTaskUUID());
      if (taskInfo != null) {
        taskInfo.setTaskState(TaskInfo.State.Failure);
      }
    }
  }

  private UniverseDefinitionTaskParams bindFormDataToTaskParams(ObjectNode formData) throws Exception {
    return bindFormDataToTaskParams(formData, false);
  }

  private UniverseDefinitionTaskParams bindFormDataToTaskParams(ObjectNode formData, boolean isRolling) throws Exception {
    ObjectMapper mapper = new ObjectMapper();
    ArrayNode nodeSetArray = null;
    Map<String, String> configMap = null;
    int expectedUniverseVersion = -1;
    if (formData.get("nodeDetailsSet") != null && formData.get("nodeDetailsSet").size() > 0) {
      nodeSetArray = (ArrayNode)formData.get("nodeDetailsSet");
      formData.remove("nodeDetailsSet");
    }
    if (formData.get("expectedUniverseVersion") != null) {
      expectedUniverseVersion = formData.get("expectedUniverseVersion").asInt();
    }
    JsonNode config = formData.get("encryptionAtRestConfig");
    if (config != null) {
      formData.remove("encryptionAtRestConfig");
      configMap = mapper.treeToValue(config, Map.class);
    }
    UniverseDefinitionTaskParams taskParams = null;
    List<Cluster> clusters = mapClustersInParams(formData);
    if (isRolling) {
      taskParams = mapper.treeToValue(formData, RollingRestartParams.class);
    } else {
      taskParams = mapper.treeToValue(formData, UniverseDefinitionTaskParams.class);
    }
    taskParams.clusters = clusters;
    if (nodeSetArray != null) {
      taskParams.nodeDetailsSet = new HashSet<>();
      for (JsonNode nodeItem : nodeSetArray) {
        taskParams.nodeDetailsSet.add(mapper.treeToValue(nodeItem, NodeDetails.class));
      }
    }
    taskParams.expectedUniverseVersion = expectedUniverseVersion;
    taskParams.encryptionAtRestConfig = configMap;
    return taskParams;
  }

  /**
   * The ObjectMapper fails to properly map the array of clusters. Given form data, this method
   * does the translation. Each cluster in the array of clusters is expected to conform to the
   * Cluster definitions, especially including the UserIntent.
   *
   * @param formData Parent FormObject for the clusters array.
   * @return A list of deserialized clusters.
   */
  private List<Cluster> mapClustersInParams(ObjectNode formData) throws Exception {
    ArrayNode clustersJsonArray = (ArrayNode) formData.get("clusters");
    if (clustersJsonArray == null) {
      throw new Exception("clusters: This field is required");
    }
    ArrayNode newClustersJsonArray = Json.newArray();
    List<Cluster> clusters = new ArrayList<>();
    for (int i = 0; i < clustersJsonArray.size(); ++i) {
      ObjectNode clusterJson = (ObjectNode) clustersJsonArray.get(i);
      if (clusterJson.has("regions")) {
        clusterJson.remove("regions");
      }
      ObjectNode userIntent = (ObjectNode) clusterJson.get("userIntent");
      if (userIntent == null ) {
        throw new Exception("userIntent: This field is required");
      }
      // TODO: (ram) add tests for all these.
      Map<String, String> masterGFlagsMap = serializeGFlagListToMap(userIntent, "masterGFlags");
      Map<String, String> tserverGFlagsMap = serializeGFlagListToMap(userIntent, "tserverGFlags");
      Map<String, String> instanceTags = serializeGFlagListToMap(userIntent, "instanceTags");
      clusterJson.set("userIntent", userIntent);
      newClustersJsonArray.add(clusterJson);
      Cluster cluster = (new ObjectMapper()).treeToValue(clusterJson, Cluster.class);
      cluster.userIntent.masterGFlags = masterGFlagsMap;
      cluster.userIntent.tserverGFlags = tserverGFlagsMap;
      cluster.userIntent.instanceTags = instanceTags;
      clusters.add(cluster);
    }
    formData.set("clusters", newClustersJsonArray);
    return clusters;
  }

  /**
   * Method serializes the GFlag ObjectNode into a Map and then deletes it from its parent node.
   * @param formNode Parent FormObject for the GFlag Node.
   * @param listType Type of GFlag object
   * @return Serialized JSON array into Map
   */
  private Map<String, String>  serializeGFlagListToMap(ObjectNode formNode, String listType) {
    Map<String, String> gflagMap = new HashMap<>();
    JsonNode formNodeList = formNode.get(listType);
    if (formNodeList != null && formNodeList.isArray()) {
      ArrayNode flagNodeArray = (ArrayNode) formNodeList;
      for (JsonNode gflagNode : flagNodeArray) {
        if (gflagNode.has("name")) {
          gflagMap.put(gflagNode.get("name").asText(), gflagNode.get("value").asText());
        }
      }
    }
    formNode.remove(listType);
    return gflagMap;
  }
}

