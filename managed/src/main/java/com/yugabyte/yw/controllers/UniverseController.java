// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;


import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.yugabyte.yw.cloud.AWSConstants;
import com.yugabyte.yw.cloud.AWSResourceUtil;
import com.yugabyte.yw.cloud.UniverseResourceDetails;
import com.yugabyte.yw.forms.MetricQueryParams;
import com.yugabyte.yw.metrics.MetricQueryHelper;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.tasks.DestroyUniverse;
import com.yugabyte.yw.common.ApiResponse;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.forms.RollingRestartParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;

import play.data.Form;
import play.data.FormFactory;
import play.libs.Json;
import play.mvc.Result;
import play.mvc.Results;

import java.util.Collection;
import java.util.HashSet;
import java.util.Map;
import java.util.Set;
import java.util.UUID;

import static com.yugabyte.yw.commissioner.Common.CloudType.aws;


public class UniverseController extends AuthenticatedController {
  public static final Logger LOG = LoggerFactory.getLogger(UniverseController.class);

  @Inject
  FormFactory formFactory;

  @Inject
  Commissioner commissioner;

  @Inject
  MetricQueryHelper metricQueryHelper;

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
      UniverseDefinitionTaskParams taskParams = bindFormDataToTaskParams(formData);
      // Verify the customer with this universe is present.
      Customer customer = Customer.get(customerUUID);
      if (customer == null) {
        return ApiResponse.error(BAD_REQUEST, "Invalid Customer UUID: " + customerUUID);
      }
      PlacementInfoUtil.updateUniverseDefinition(taskParams, customer.getCustomerId());
      return ApiResponse.success(taskParams);
    } catch (Exception e) {
      LOG.error("Unable to Configure Universe for Customer with ID {} Failed with message: {}.",
                customerUUID, e.getMessage());
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
      UniverseResourceDetails resourceDetails =
        getUniverseResourcesUtil(taskParams.nodeDetailsSet, taskParams.userIntent.providerType);
      return ApiResponse.success(resourceDetails);
    } catch (Throwable t) {
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
    try {
      // Set the provider code.
      String providerCode =
        Provider.find.byId(UUID.fromString(taskParams.userIntent.provider)).code;
      taskParams.userIntent.providerType = CloudType.valueOf(providerCode);
      // Create a new universe. This makes sure that a universe of this name does not already exist
      // for this customer id.
      Universe universe = Universe.create(taskParams.userIntent.universeName,
        taskParams.universeUUID,
        customer.getCustomerId());
      LOG.info("Created universe {} : {}.", universe.universeUUID, universe.name);

      // Add an entry for the universe into the customer table.
      customer.addUniverseUUID(universe.universeUUID);
      customer.save();

      LOG.info("Added universe {} : {} for customer [{}].",
        universe.universeUUID, universe.name, customer.getCustomerId());

      // Submit the task to create the universe.
      UUID taskUUID = commissioner.submit(TaskInfo.Type.CreateUniverse, taskParams);
      LOG.info("Submitted create universe for {}:{}, task uuid = {}.",
        universe.universeUUID, universe.name, taskUUID);

      // Add this task uuid to the user universe.
      CustomerTask.create(customer,
                          universe,
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
    try {
      // Get the universe. This makes sure that a universe of this name does exist
      // for this customer id.
      Universe universe = Universe.get(universeUUID);
      LOG.info("Found universe {} : name={} at version={}.",
        universe.universeUUID, universe.name, universe.version);
      UUID taskUUID = commissioner.submit(TaskInfo.Type.EditUniverse, taskParams);
      LOG.info("Submitted edit universe for {} : {}, task uuid = {}.",
        universe.universeUUID, universe.name, taskUUID);

      // Add this task uuid to the user universe.
      CustomerTask.create(customer,
        universe,
        taskUUID,
        CustomerTask.TargetType.Universe,
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
        universePayload.put("pricePerHour", getUniverseResourcesUtil(universe.getNodes(), universe.getUniverseDetails().userIntent.providerType).pricePerHour);
      } catch (Exception e) {
        LOG.error("Unable to fetch cost for universe {}.", universe.universeUUID);
      }
      universes.add(universePayload);
    }
    return ApiResponse.success(universes);
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
    taskParams.cloud = universe.getUniverseDetails().userIntent.providerType;

    // Submit the task to destroy the universe.
    UUID taskUUID = commissioner.submit(TaskInfo.Type.DestroyUniverse, taskParams);
    LOG.info("Submitted destroy universe for " + universeUUID + ", task uuid = " + taskUUID);

    // Add this task uuid to the user universe.
    CustomerTask.create(customer,
      universe,
      taskUUID,
      CustomerTask.TargetType.Universe,
      CustomerTask.TaskType.Delete,
      universe.name);

    // Remove the entry for the universe from the customer table.
    customer.removeUniverseUUID(universeUUID);
    customer.save();
    LOG.info("Dropped universe " + universeUUID + " for customer [" + customer.name + "]");

    ObjectNode response = Json.newObject();
    response.put("taskUUID", taskUUID.toString());
    return ApiResponse.success(response);
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
    }
    catch (RuntimeException e) {
      return ApiResponse.error(BAD_REQUEST, "No universe found with UUID: " + universeUUID);
    }
    try {
      return ApiResponse.success(getUniverseResourcesUtil(universe.getNodes(), universe.getUniverseDetails().userIntent.providerType));
    }
    catch (Exception e) {
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
    try {
      for (Universe universe : universeSet) {
        response.add(Json.toJson(getUniverseResourcesUtil(universe.getNodes(), universe.getUniverseDetails().userIntent.providerType)));
      }
    } catch (Exception e) {
      return ApiResponse.error(INTERNAL_SERVER_ERROR,
        "Error getting cost for customer " + customerUUID);
    }
    return ApiResponse.success(response);
  }

  /**
   * API that queues a task to perform an upgrade and a subsequent rolling restart of a universe.
   *
   * @return result of the universe update operation.
   */
  public Result upgrade(UUID customerUUID, UUID universeUUID) {
    try {
      LOG.info("Upgrade {} for {}.", customerUUID, universeUUID);

      // Verify the customer with this universe is present.
      Customer customer = Customer.get(customerUUID);
      if (customer == null) {
        return ApiResponse.error(BAD_REQUEST, "Invalid Customer UUID: " + customerUUID);
      }

      // Get the user submitted form data.
      Form<RollingRestartParams> formData = formFactory.form(RollingRestartParams.class).bindFromRequest();

      // Check for any form errors.
      if (formData.hasErrors()) {
        return ApiResponse.error(BAD_REQUEST, formData.errorsAsJson());
      }

      RollingRestartParams taskParams = formData.get();

      CustomerTask.TaskType customerTaskType = null;
      // Validate if any required params are missed based on the taskType
      switch(taskParams.taskType) {
        case Software:
          customerTaskType = CustomerTask.TaskType.UpgradeSoftware;
          if (taskParams.ybServerPackage == null || taskParams.ybServerPackage.isEmpty()) {
            return ApiResponse.error(
              BAD_REQUEST,
              "ybServerPackage param is required for taskType: " + taskParams.taskType);
          }
          break;
        case GFlags:
          customerTaskType = CustomerTask.TaskType.UpgradeGflags;
          if (taskParams.gflags == null || taskParams.gflags.isEmpty()) {
            return ApiResponse.error(
              BAD_REQUEST,
              "gflags param is required for taskType: " + taskParams.taskType);
          }
          break;
      }

      LOG.info("Got task type {}", customerTaskType.toString());

      // Get the universe. This makes sure that a universe of this name does exist
      // for this customer id.
      Universe universe = Universe.get(universeUUID);
      taskParams.universeUUID = universe.universeUUID;
      taskParams.expectedUniverseVersion = universe.version;
      LOG.info("Found universe {} : name={} at version={}.",
        universe.universeUUID, universe.name, universe.version);

      UUID taskUUID = commissioner.submit(TaskInfo.Type.UpgradeUniverse, taskParams);
      LOG.info("Submitted upgrade universe for {} : {}, task uuid = {}.",
        universe.universeUUID, universe.name, taskUUID);

      // Add this task uuid to the user universe.
      CustomerTask.create(customer,
        universe,
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

  public Result metrics(UUID customerUUID, UUID universeUUID) {
    Customer customer = Customer.get(customerUUID);
    if (customer == null) {
      return ApiResponse.error(BAD_REQUEST, "Invalid Customer UUID: " + customerUUID);
    }

    Universe universe;
    try {
      universe = Universe.get(universeUUID);
    }
    catch (RuntimeException e) {
      return ApiResponse.error(BAD_REQUEST, "Invalid Universe UUID: " + universeUUID);
    }

    Form<MetricQueryParams> formData = formFactory.form(MetricQueryParams.class).bindFromRequest();

    if (formData.hasErrors()) {
      return ApiResponse.error(BAD_REQUEST, formData.errorsAsJson());
    }

    Map<String, String> params = formData.data();
    if (universe.getUniverseDetails().nodePrefix != null) {
      ObjectNode filterJson = Json.newObject();
      filterJson.put("node_prefix", universe.getUniverseDetails().nodePrefix);
      params.put("filters", Json.stringify(filterJson));
    }

    try {
      JsonNode response = metricQueryHelper.query(params);
      if (response.has("error")) {
        return ApiResponse.error(BAD_REQUEST, response.get("error"));
      }
      return ApiResponse.success(response);
    } catch (RuntimeException e) {
      return ApiResponse.error(BAD_REQUEST, e.getMessage());
    }
  }


  private UniverseResourceDetails getUniverseResourcesUtil(Collection<NodeDetails> nodes, CloudType cloudType) throws Exception {
    UniverseResourceDetails universeResourceDetails = new UniverseResourceDetails();
    for (NodeDetails node : nodes) {
      if (node.isActive() && cloudType == aws) {
        AWSResourceUtil.mergeResourceDetails(node.cloudInfo.instance_type, node.cloudInfo.az,
          AvailabilityZone.find.byId(node.azUuid).region.code,
          AWSConstants.Tenancy.Shared, universeResourceDetails);
      }
    }
    return universeResourceDetails;
  }

  private UniverseDefinitionTaskParams bindFormDataToTaskParams(ObjectNode formData) throws Exception {
    ObjectMapper mapper = new ObjectMapper();
    UniverseDefinitionTaskParams taskParams = new UniverseDefinitionTaskParams();
    ArrayNode nodeSetArray = null;
    int expectedUniverseVersion = -1;
    if (formData.get("nodeDetailsSet") != null) {
      nodeSetArray = (ArrayNode)formData.get("nodeDetailsSet");
      formData.remove("nodeDetailsSet");
    }
    if (formData.get("expectedUniverseVersion") != null) {
      expectedUniverseVersion = formData.get("expectedUniverseVersion").asInt();
    }
    taskParams = mapper.treeToValue(formData, UniverseDefinitionTaskParams.class);
    if (taskParams.userIntent == null ) {
      throw new Exception("userIntent: This field is required");
    }
    if (nodeSetArray != null) {
      Set<NodeDetails> nodeDetailSet = new HashSet<NodeDetails>();
      for (JsonNode nodeItem : nodeSetArray) {
        ObjectNode tempObjectNode = ((ObjectNode) nodeItem);
        NodeDetails node = mapper.treeToValue(tempObjectNode, NodeDetails.class);
        nodeDetailSet.add(node);
      }
      taskParams.nodeDetailsSet = nodeDetailSet;
    }
    taskParams.expectedUniverseVersion = expectedUniverseVersion;
    return taskParams;
  }
}

