// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;

import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Random;
import java.util.UUID;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.tasks.params.UniverseTaskParams;
import com.yugabyte.yw.common.ApiHelper;
import com.yugabyte.yw.common.ApiResponse;
import com.yugabyte.yw.forms.CreateUniverseFormData;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.ui.controllers.AuthenticatedController;

import play.data.Form;
import play.data.FormFactory;
import play.mvc.Result;

public class UniverseController extends AuthenticatedController {
  public static final Logger LOG = LoggerFactory.getLogger(UniverseController.class);

  @Inject
  FormFactory formFactory;

  @Inject
  ApiHelper apiHelper;

  Random random = new Random();

  /**
   * API that queues a task to create a new universe. This does not wait for the creation.
   * @return result of the universe create operation.
   */
  public Result create(UUID customerUUID) {
    try {
      // Get the user submitted form data.
      Form<CreateUniverseFormData> formData =
          formFactory.form(CreateUniverseFormData.class).bindFromRequest();

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
      LOG.info("Created universe " + universe.universeUUID + ":" + universe.name);

      // Add an entry for the universe into the customer table.
      customer.addUniverseUUID(universe.universeUUID);
      customer.save();
      LOG.info("Added universe " + universe.universeUUID + ":" + universe.name +
               " for customer [" + customer.name + "]");

      // Setup the create universe task.
      UniverseTaskParams taskParams = new UniverseTaskParams();
      taskParams.universeUUID = universe.universeUUID;
      taskParams.cloudProvider = CloudType.aws.toString();
      taskParams.numNodes = formData.get().replicationFactor;

      // Compose a unique name for the universe.
      taskParams.nodePrefix = Integer.toString(customer.customerId) + "-" + universe.name;

      // Fill in the user intent.
      taskParams.userIntent = new Universe.UserIntent();
      taskParams.userIntent.isMultiAZ = formData.get().isMultiAZ;
      LOG.debug("Setting isMultiAZ = " + taskParams.userIntent.isMultiAZ);
      taskParams.userIntent.preferredRegion = formData.get().preferredRegion;

      // TODO: remove this hack.
      taskParams.userIntent.regionList = new ArrayList<UUID>();
      taskParams.userIntent.regionList.add(formData.get().regionUUID);
      // TODO: enable this.
//      taskParams.userIntent.regionList = formData.get().regionList;
      LOG.debug("Added " + taskParams.userIntent.regionList.size() + " regions to placement info");

      // Set the replication factor.
      taskParams.userIntent.replicationFactor = formData.get().replicationFactor;

      // Compute and fill in the placement info.
      taskParams.placementInfo = getPlacementInfo(taskParams.userIntent);
      LOG.info("Initialized params for creating universe " +
               universe.universeUUID + ":" + universe.name);

      // Submit the task to create the universe.
      UUID taskUUID = Commissioner.submit(TaskInfo.Type.CreateUniverse, taskParams);
      LOG.info("Submitted create universe for " + universe.universeUUID + ":" + universe.name +
               ", task uuid = " + taskUUID);

      // Add this task uuid to the user universe.
      CustomerTask.create(customer,
                          taskUUID,
                          CustomerTask.TargetType.Instance,
                          CustomerTask.TaskType.Create,
                          universe.name);
      LOG.info("Saved task uuid " + taskUUID + " in customer tasks table for universe " +
               universe.universeUUID + ":" + universe.name);
      return ApiResponse.success(universe);
    } catch (Throwable t) {
      LOG.error("Error creating universe", t);
      return ApiResponse.error(INTERNAL_SERVER_ERROR, t.getMessage());
    }
  }

  public Result update(UUID customerUUID, UUID universeUUID) {
    return TODO;
  }

  /**
   * List the universes for a given customer.
   * @return
   */
  public Result list(UUID customerUUID) {
    // Verify the customer is present.
    Customer customer = Customer.find.byId(customerUUID);
    if (customer == null) {
      return ApiResponse.error(BAD_REQUEST, "Invalid Customer UUID: " + customerUUID);
    }
    return ApiResponse.success(customer.getUniverses());
  }

  public Result getDetails(UUID customerUUID, UUID universeUUID) {
    return TODO;
  }

  private Universe.PlacementInfo getPlacementInfo(Universe.UserIntent userIntent) {
    // We currently do not support multi-region placement.
    if (userIntent.regionList.size() > 1) {
      throw new UnsupportedOperationException("Multi-region placement not supported.");
    }
    UUID regionUUID = userIntent.regionList.get(0);

    // Find the cloud object.
    // TODO: standardize on the cloud name.
    Provider cloudProvider = Provider.get("Amazon");
    // Find the region object.
    Region region = Region.get(regionUUID);
    // Find the AZs for the required region.
    List<AvailabilityZone> azList = AvailabilityZone.getAZsForRegion(regionUUID);
    if (azList.isEmpty()) {
      throw new RuntimeException("No AZ found for region: " + regionUUID);
    }

    // Create the placement info object.
    Universe.PlacementInfo placementInfo = new Universe.PlacementInfo();
    placementInfo.cloudList = new ArrayList<Universe.PlacementCloud>();

    Universe.PlacementCloud placementCloud = new Universe.PlacementCloud();
    // TODO: fix this.
    placementCloud.name = "aws";
    placementCloud.uuid = cloudProvider.uuid;
    placementCloud.regionList = new ArrayList<Universe.PlacementRegion>();

    Universe.PlacementRegion placementRegion = new Universe.PlacementRegion();
    placementRegion.uuid = region.uuid;
    placementRegion.name = region.name;
    placementRegion.azList = new ArrayList<Universe.PlacementAZ>();

    // In case of a single PlacementAZ placement request, choose a random PlacementAZ to
    // place the universe into.
    Collections.shuffle(azList);
    if (!userIntent.isMultiAZ) {
      AvailabilityZone az = azList.get(0);
      Universe.PlacementAZ placementAZ = new Universe.PlacementAZ();
      placementAZ.uuid = az.uuid;
      placementAZ.name = az.name;
      placementAZ.replicationFactor = userIntent.replicationFactor;
      placementAZ.subnet = az.subnet;
      placementRegion.azList.add(placementAZ);
    } else {
      Map<UUID, Universe.PlacementAZ> idToAzMap = new HashMap<UUID, Universe.PlacementAZ>();
      for (int idx = 0; idx < userIntent.replicationFactor; idx++) {
        AvailabilityZone az = azList.get(idx % azList.size());
        // Check if we have created an entry for this AZ.
        Universe.PlacementAZ placementAZ = idToAzMap.get(az.uuid);
        // If not, create a new entry.
        if (placementAZ == null) {
          placementAZ = new Universe.PlacementAZ();
          placementAZ.uuid = az.uuid;
          placementAZ.name = az.name;
          placementAZ.replicationFactor = 0;
          placementAZ.subnet = az.subnet;
          // Add this AZ to the list of AZs in this region.
          placementRegion.azList.add(placementAZ);
          // Add this AZ to the map to make sure we do not create it again.
          idToAzMap.put(az.uuid, placementAZ);
        }
        // Increment the number of copies of data to be placed on this AZ.
        placementAZ.replicationFactor++;
      }
    }
    // Add the region to the cloud.
    placementCloud.regionList.add(placementRegion);
    // Add the cloud to the placement info.
    placementInfo.cloudList.add(placementCloud);

    return placementInfo;
  }
}
