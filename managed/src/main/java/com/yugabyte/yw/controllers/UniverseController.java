// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;

import java.util.Calendar;
import java.util.Collection;
import java.util.Set;
import java.util.UUID;

import com.yugabyte.yw.forms.RollingRestartParams;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.inject.Inject;
import com.yugabyte.yw.cloud.AWSConstants;
import com.yugabyte.yw.cloud.AWSCostUtil;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.commissioner.tasks.DestroyUniverse;
import com.yugabyte.yw.common.ApiResponse;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;

import play.data.Form;
import play.data.FormFactory;
import play.libs.Json;
import play.mvc.Result;
import play.mvc.Results;

public class UniverseController extends AuthenticatedController {
  public static final Logger LOG = LoggerFactory.getLogger(UniverseController.class);

  @Inject
  FormFactory formFactory;

  @Inject
  Commissioner commissioner;

  /**
   * API that queues a task to create a new universe. This does not wait for the creation.
   * @return result of the universe create operation.
   */
  public Result create(UUID customerUUID) {
    try {
      LOG.info("Create for {}.", customerUUID);
      // Get the user submitted form data.
      Form<UniverseDefinitionTaskParams> formData =
        formFactory.form(UniverseDefinitionTaskParams.class).bindFromRequest();

      // Check for any form errors.
      if (formData.hasErrors()) {
        return ApiResponse.error(BAD_REQUEST, formData.errorsAsJson());
      }

      UniverseDefinitionTaskParams taskParams = formData.get();

      // Verify the customer with this universe is present.
      Customer customer = Customer.get(customerUUID);
      if (customer == null) {
        return ApiResponse.error(BAD_REQUEST, "Invalid Customer UUID: " + customerUUID);
      }

      // Create a new universe. This makes sure that a universe of this name does not already exist
      // for this customer id.
      Universe universe = Universe.create(taskParams.userIntent.universeName,
                                          customer.getCustomerId());
      LOG.info("Created universe {} : {}.", universe.universeUUID, universe.name);

      // Add an entry for the universe into the customer table.
      customer.addUniverseUUID(universe.universeUUID);
      customer.save();

      LOG.info("Added universe {} : {} for customer [{}].",
               universe.universeUUID, universe.name, customer.getCustomerId());

      PlacementInfoUtil.updateTaskParams(taskParams, universe, customer.getCustomerId());

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
    try {
      LOG.info("Update {} for {}.", customerUUID, universeUUID);
      // Get the user submitted form data.
      Form<UniverseDefinitionTaskParams> formData =
        formFactory.form(UniverseDefinitionTaskParams.class).bindFromRequest();

      // Check for any form errors.
      if (formData.hasErrors()) {
        return ApiResponse.error(BAD_REQUEST, formData.errorsAsJson());
      }

      UniverseDefinitionTaskParams taskParams = formData.get();

      // Verify the customer with this universe is present.
      Customer customer = Customer.get(customerUUID);
      if (customer == null) {
        return ApiResponse.error(BAD_REQUEST, "Invalid Customer UUID: " + customerUUID);
      }

      // Get the universe. This makes sure that a universe of this name does exist
      // for this customer id.
      Universe universe = Universe.get(universeUUID);
      LOG.info("Found universe {} : name={} at version={}.",
               universe.universeUUID, universe.name, universe.version);

      PlacementInfoUtil.updateTaskParams(taskParams, universe, customer.getCustomerId());

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
      universes.add(universe.toJson());
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
      ObjectNode universeCost = getUniverseCostUtil(universe);
      return ApiResponse.success(universeCost);
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
        response.add(getUniverseCostUtil(universe));
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
          if (taskParams.ybServerPkg == null || taskParams.ybServerPkg.isEmpty()) {
            return ApiResponse.error(
              BAD_REQUEST,
              "ybServerPkg param is required for taskType: " + taskParams.taskType);
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

  /**
   * Helper Method to fetch API Responses for Instance costs
   */
  private ObjectNode getUniverseCostUtil(Universe universe) throws Exception {
    Collection<NodeDetails> nodes = universe.getNodes();
    // TODO: only pick the newly configured nodes in case of the universe being edited.
    double instanceCostPerDay = 0;
    double universeCostPerDay = 0;
    for (NodeDetails node : nodes) {
      String regionCode = AvailabilityZone.find.byId(node.azUuid).region.code;
      // TODO: we do not currently store tenancy for the node.
      instanceCostPerDay = AWSCostUtil.getCostPerHour(node.cloudInfo.instance_type,
                           regionCode,
                           AWSConstants.Tenancy.Shared) * 24;
      universeCostPerDay += instanceCostPerDay;
    }
    Calendar currentCalender = Calendar.getInstance();
    int monthDays = currentCalender.getActualMaximum(Calendar.DAY_OF_MONTH);
    double costPerMonth = monthDays * universeCostPerDay;
    ObjectNode universeCostItem = Json.newObject();
    universeCostItem.put("costPerDay", universeCostPerDay);
    universeCostItem.put("costPerMonth", costPerMonth);
    universeCostItem.put("name", universe.name);
    universeCostItem.put("uuid", universe.universeUUID.toString());
    return universeCostItem;
  }
}
