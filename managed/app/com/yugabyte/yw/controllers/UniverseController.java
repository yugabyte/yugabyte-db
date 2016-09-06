// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;

import java.util.Collections;
import java.util.List;
import java.util.UUID;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.tasks.DestroyUniverse;
import com.yugabyte.yw.commissioner.tasks.params.UniverseDefinitionTaskParams;
import com.yugabyte.yw.common.ApiHelper;
import com.yugabyte.yw.common.ApiResponse;
import com.yugabyte.yw.forms.UniverseFormData;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import com.yugabyte.yw.models.helpers.PlacementInfo.PlacementAZ;
import com.yugabyte.yw.models.helpers.PlacementInfo.PlacementCloud;
import com.yugabyte.yw.models.helpers.PlacementInfo.PlacementRegion;
import com.yugabyte.yw.models.helpers.UserIntent;
import com.yugabyte.yw.ui.controllers.AuthenticatedController;

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
  ApiHelper apiHelper;

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
      Form<UniverseFormData> formData =
        formFactory.form(UniverseFormData.class).bindFromRequest();

      // Check for any form errors.
      if (formData.hasErrors()) {
        return ApiResponse.error(BAD_REQUEST, formData.errorsAsJson());
      }

      // Verify the customer with this universe is present.
      Customer customer = Customer.find.byId(customerUUID);
      if (customer == null) {
        return ApiResponse.error(BAD_REQUEST, "Invalid Customer UUID: " + customerUUID);
      }

      // Create a new universe. This makes sure that a universe of this name does not already exist
      // for this customer id.
      Universe universe = Universe.create(formData.get().universeName, customer.customerId);
      LOG.info("Created universe {} : {}.", universe.universeUUID, universe.name);

      // Add an entry for the universe into the customer table.
      customer.addUniverseUUID(universe.universeUUID);
      customer.save();

      LOG.info("Added universe {} : {} for customer [{}].",
    		  universe.universeUUID, universe.name, customer.customerId);

      UniverseDefinitionTaskParams taskParams =
        getTaskParams(formData, universe, customer.customerId);

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
      Form<UniverseFormData> formData =
        formFactory.form(UniverseFormData.class).bindFromRequest();

      // Check for any form errors.
      if (formData.hasErrors()) {
        return ApiResponse.error(BAD_REQUEST, formData.errorsAsJson());
      }

      // Verify the customer with this universe is present.
      Customer customer = Customer.find.byId(customerUUID);
      if (customer == null) {
        return ApiResponse.error(BAD_REQUEST, "Invalid Customer UUID: " + customerUUID);
      }

      // Get the universe. This makes sure that a universe of this name does exist
      // for this customer id.
      Universe universe = Universe.get(universeUUID);
      LOG.info("Found universe {} : {}.", universe.universeUUID, universe.name);

      UniverseDefinitionTaskParams taskParams =
        getTaskParams(formData, universe, customer.customerId);

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
    Customer customer = Customer.find.byId(customerUUID);
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
    Customer customer = Customer.find.byId(customerUUID);
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
    Customer customer = Customer.find.byId(customerUUID);
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

  /**
   * Helper API to convert the user form into task params.
   *
   * @param formData : Input form data.
   * @param universe : The universe details with which we are working.
   * @param customerId : Current customer's id.
   *
   * @return: The universe task params.
   */
  private UniverseDefinitionTaskParams getTaskParams(
      Form<UniverseFormData> formData,
      Universe universe,
      int customerId) {
    // Setup the create universe task.
    UniverseDefinitionTaskParams taskParams = new UniverseDefinitionTaskParams();
    taskParams.universeUUID = universe.universeUUID;
    taskParams.cloudProvider = CloudType.aws.toString();
    taskParams.numNodes = formData.get().replicationFactor;

    // Compose a unique name for the universe.
    taskParams.nodePrefix = Integer.toString(customerId) + "-" + universe.name;

    // Fill in the user intent.
    taskParams.userIntent = new UserIntent();
    taskParams.userIntent.isMultiAZ = formData.get().isMultiAZ;
    LOG.debug("Setting isMultiAZ = " + taskParams.userIntent.isMultiAZ);
    taskParams.userIntent.preferredRegion = formData.get().preferredRegion;

    taskParams.userIntent.regionList = formData.get().regionList;
    LOG.debug("Added {} regions to placement info.", taskParams.userIntent.regionList.size());

    taskParams.userIntent.instanceType = formData.get().instanceType;

    // Set the replication factor.
    taskParams.userIntent.replicationFactor = formData.get().replicationFactor;

    // Compute and fill in the placement info.
    taskParams.placementInfo = getPlacementInfo(taskParams.userIntent);
    LOG.info("Initialized params for universe {} : {}.", universe.universeUUID, universe.name);

    return taskParams;
  }

  private PlacementInfo getPlacementInfo(UserIntent userIntent) {
    // We only support a replication factor of 3.
    if (userIntent.replicationFactor != 3) {
      throw new RuntimeException("Replication factor must be 3");
    }
    // Make sure the preferred region is in the list of user specified regions.
    if (userIntent.preferredRegion != null &&
        !userIntent.regionList.contains(userIntent.preferredRegion)) {
      throw new RuntimeException("Preferred region " + userIntent.preferredRegion +
                                 " not in user region list");
    }

    // Create the placement info object.
    PlacementInfo placementInfo = new PlacementInfo();

    // Handle the single AZ deployment case.
    if (!userIntent.isMultiAZ) {
      // Select an AZ in the required region.
      List<AvailabilityZone> azList =
          AvailabilityZone.getAZsForRegion(userIntent.regionList.get(0));
      if (azList.isEmpty()) {
        throw new RuntimeException("No AZ found for region: " + userIntent.regionList.get(0));
      }
      Collections.shuffle(azList);
      UUID azUUID = azList.get(0).uuid;
      // Add all replicas into the same AZ.
      for (int idx = 0; idx < userIntent.replicationFactor; idx++) {
        addPlacementZone(azUUID, placementInfo);
      }
      return placementInfo;
    }

    // If one region is specified, pick all three AZs from it. Make sure there are enough regions.
    if (userIntent.regionList.size() == 1) {
      selectAndAddPlacementZones(userIntent.regionList.get(0), placementInfo, 3);
    } else if (userIntent.regionList.size() == 2) {
      // Pick two AZs from one of the regions (preferred region if specified).
      UUID preferredRegionUUID = userIntent.preferredRegion;
      // If preferred region was not specified, then pick the region that has at least 2 zones as
      // the preferred region.
      if (preferredRegionUUID == null) {
        if (AvailabilityZone.getAZsForRegion(userIntent.regionList.get(0)).size() >= 2) {
          preferredRegionUUID = userIntent.regionList.get(0);
        } else {
          preferredRegionUUID = userIntent.regionList.get(1);
        }
      }
      selectAndAddPlacementZones(preferredRegionUUID, placementInfo, 2);

      // Pick one AZ from the other region.
      UUID otherRegionUUID = userIntent.regionList.get(0).equals(preferredRegionUUID) ?
                             userIntent.regionList.get(1) :
                             userIntent.regionList.get(0);
      selectAndAddPlacementZones(otherRegionUUID, placementInfo, 1);

    } else if (userIntent.regionList.size() == 3) {
      // If the user has specified three regions, pick one AZ from each region.
      for (int idx = 0; idx < 3; idx++) {
        selectAndAddPlacementZones(userIntent.regionList.get(idx), placementInfo, 1);
      }
    } else {
      throw new RuntimeException("Unsupported placement, num regions = " +
                                 userIntent.regionList.size() + "more than replication factor");
    }

    return placementInfo;
  }

  private void selectAndAddPlacementZones(UUID regionUUID,
                                          PlacementInfo placementInfo,
                                          int numZones) {
    // Find the region object.
    Region region = Region.get(regionUUID);
    LOG.debug("Selecting and adding " + numZones + " zones in region " + region.name);
    // Find the AZs for the required region.
    List<AvailabilityZone> azList = AvailabilityZone.getAZsForRegion(regionUUID);
    if (azList.size() < numZones) {
      throw new RuntimeException("Need at least " + numZones + " zones but found only " +
                                 azList.size() + " for region " + region.name);
    }
    Collections.shuffle(azList);
    // Pick as many AZs as required.
    for (int idx = 0; idx < numZones; idx++) {
      addPlacementZone(azList.get(idx).uuid, placementInfo);
    }
  }

  private void addPlacementZone(UUID zone, PlacementInfo placementInfo) {
    // Get the zone, region and cloud.
    AvailabilityZone az = AvailabilityZone.find.byId(zone);
    Region region = az.region;
    Provider cloud = region.provider;

    // Find the placement cloud if it already exists, or create a new one if one does not exist.
    PlacementCloud placementCloud = null;
    for (PlacementCloud pCloud : placementInfo.cloudList) {
      if (pCloud.uuid.equals(cloud.uuid)) {
        LOG.debug("Found cloud: " + cloud.name);
        placementCloud = pCloud;
        break;
      }
    }
    if (placementCloud == null) {
      LOG.debug("Adding cloud: " + cloud.name);
      placementCloud = new PlacementCloud();
      placementCloud.uuid = cloud.uuid;
      // TODO: fix this hardcode by creating a 'code' attribute in the cloud object.
      placementCloud.name = "aws";
      placementInfo.cloudList.add(placementCloud);
    }

    // Find the placement region if it already exists, or create a new one.
    PlacementRegion placementRegion = null;
    for (PlacementRegion pRegion : placementCloud.regionList) {
      if (pRegion.uuid.equals(region.uuid)) {
        LOG.debug("Found region: " + region.name);
        placementRegion = pRegion;
        break;
      }
    }
    if (placementRegion == null) {
      LOG.debug("Adding region: " + region.name);
      placementRegion = new PlacementRegion();
      placementRegion.uuid = region.uuid;
      placementRegion.code = region.code;
      placementRegion.name = region.name;
      placementCloud.regionList.add(placementRegion);
    }

    // Find the placement AZ in the region if it already exists, or create a new one.
    PlacementAZ placementAZ = null;
    for (PlacementAZ pAz : placementRegion.azList) {
      if (pAz.uuid.equals(az.uuid)) {
        LOG.debug("Found az: " + az.name);
        placementAZ = pAz;
        break;
      }
    }
    if (placementAZ == null) {
      LOG.debug("Adding region: " + az.name);
      placementAZ = new PlacementAZ();
      placementAZ.uuid = az.uuid;
      placementAZ.name = az.name;
      placementAZ.replicationFactor = 0;
      placementAZ.subnet = az.subnet;
      placementRegion.azList.add(placementAZ);
    }
    placementAZ.replicationFactor++;
    LOG.debug("Setting az " + az.name + " replication factor = " + placementAZ.replicationFactor);
  }
}
