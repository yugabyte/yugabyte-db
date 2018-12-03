// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;

import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.util.concurrent.ThreadFactoryBuilder;
import com.google.inject.Inject;

import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.ITask;
import com.yugabyte.yw.commissioner.SubTaskGroup;
import com.yugabyte.yw.commissioner.SubTaskGroupQueue;
import com.yugabyte.yw.commissioner.tasks.UniverseDefinitionTaskBase;
import com.yugabyte.yw.common.ApiResponse;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.forms.ImportUniverseFormData;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Universe.UniverseUpdater;
import com.yugabyte.yw.models.helpers.CloudSpecificInfo;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.PlacementInfo;

import java.util.ArrayList;
import java.util.Collection;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Future;
import java.util.concurrent.LinkedBlockingQueue;
import java.util.concurrent.ThreadPoolExecutor;
import java.util.concurrent.ThreadFactory;
import java.util.concurrent.TimeUnit;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.client.ListMastersResponse;
import org.yb.client.ListTabletServersResponse;
import org.yb.client.YBClient;
import org.yb.util.ServerInfo;


import play.api.Play;
import play.Configuration;
import play.data.Form;
import play.data.FormFactory;
import play.libs.Json;
import play.mvc.*;

import javax.persistence.PersistenceException;

public class ImportController extends Controller {
  public static final Logger LOG = LoggerFactory.getLogger(ImportController.class);

  // Threadpool to run user submitted tasks.
  static ExecutorService executor;

  // Minimum number of concurrent tasks to execute at a time.
  private static final int TASK_THREADS = 200;

  // The maximum time that excess idle threads will wait for new tasks before terminating.
  // The unit is specified in the API (and is seconds).
  private static final long THREAD_ALIVE_TIME = 60L;

  // The RPC timeouts.
  private static final long RPC_TIMEOUT_MS = 5000L;

  @Inject
  FormFactory formFactory;

  @Inject
  YBClientService ybService;

  public Result importUniverse(UUID customerUUID) {
    // Get the submitted form data.
    Form<ImportUniverseFormData> formData =
        formFactory.form(ImportUniverseFormData.class).bindFromRequest();
    if (formData.hasErrors()) {
      return ApiResponse.error(BAD_REQUEST, formData.errorsAsJson());
    }
    ObjectNode results = Json.newObject();
    ObjectNode checks = Json.newObject();
    results.put("checks", checks);
    ImportUniverseFormData importForm = formData.get();

    Customer customer = Customer.get(customerUUID);
    if (customer == null) {
      return ApiResponse.error(BAD_REQUEST, "Invalid customer uuid : " + customerUUID.toString());
    }

    switch (importForm.currentState) {
      case BEGIN:
        return importUniverseMasters(importForm, customer, results);
      case IMPORTED_MASTERS:
        return importUniverseTservers(importForm, customer, results);
      case IMPORTED_TSERVERS:
        return finishUniverseImport(importForm, customer, results);
      case FINISHED:
        return ApiResponse.success(results);
    }
    return ApiResponse.error(BAD_REQUEST,
                             "Unknown current state: " + importForm.currentState.toString());
  }

  // Helper function to convert comma seperated list of host:port into a list of host ips.
  // Returns null if there are parsing or invalid port errors.
  private List<String> getMastersList(String masterAddresses) {
    List<String> userMasterIps = new ArrayList<String>();
    String nodesList[] = masterAddresses.split(",");
    for (String hostPort : nodesList) {
      String parts[] = hostPort.split(":");
      if (parts.length != 2) {
        LOG.error("Incorrect host:port format: " + hostPort);
        return null;
      }
      userMasterIps.add(parts[0]);

      if (Integer.parseInt(parts[1]) != MetaMasterController.MASTER_RPC_PORT) {
        LOG.error("Incorrect master rpc port : " + parts[1] + " expected " +
                  MetaMasterController.MASTER_RPC_PORT);
        return null;
      }
    }
    return userMasterIps;
  }

  // Helper function to verify masters are up and running on the saved set of nodes.
  // Returns true if there are no errors.
  private boolean verifyMastersRunning(UniverseDefinitionTaskParams taskParams,
                                       ObjectNode results) {
    UniverseDefinitionTaskBase checkMasters = new UniverseDefinitionTaskBase() {
      @Override
      public void run() {
        try {
          // Create the task list sequence.
          subTaskGroupQueue = new SubTaskGroupQueue(userTaskUUID);
          // Get the list of masters.
          // Note that at this point, we have only added the masters into the cluster.
          Set<NodeDetails> masterNodes =
              taskParams().getNodesInCluster(taskParams().getPrimaryCluster().uuid);
          // Wait for new masters to be responsive.
          createWaitForServersTasks(masterNodes, ServerType.MASTER, RPC_TIMEOUT_MS);
          // Run the task.
          subTaskGroupQueue.run();
        } catch (Throwable t) {
          LOG.error("Error executing task {}, error='{}'", getName(), t.getMessage(), t);
          throw t;
        }
      }
    };
    checkMasters.initialize(taskParams);
    // Execute the task. If it fails, sets the error in the results.
    if (!executeITask(checkMasters, "check_masters_are_running", results)) {
      return false;
    }
    return true;
  }

  // Helper function to check that a master leader exists.
  // Returns true if there are no errors.
  private boolean verifyMasterLeaderExists(UniverseDefinitionTaskParams taskParams,
                                           ObjectNode results) {
    UniverseDefinitionTaskBase checkMasterLeader = new UniverseDefinitionTaskBase() {
      @Override
      public void run() {
        try {
          // Create the task list sequence.
          subTaskGroupQueue = new SubTaskGroupQueue(userTaskUUID);
          // Wait for new masters to be responsive.
          createWaitForMasterLeaderTask();
          // Run the task.
          subTaskGroupQueue.run();
        } catch (Throwable t) {
          LOG.error("Error executing task {}, error='{}'", getName(), t.getMessage(), t);
          throw t;
        }
      }
    };
    checkMasterLeader.initialize(taskParams);
    // Execute the task. If it fails, return an error.
    if (!executeITask(checkMasterLeader, "check_master_leader_election", results)) {
      return false;
    }
    return true;
  }

  /**
   * Given the master addresses, create a basic universe object.
   */
  private Result importUniverseMasters(ImportUniverseFormData importForm,
                                       Customer customer,
                                       ObjectNode results) {
    String universeName = importForm.universeName;
    String masterAddresses = importForm.masterAddresses;

    // TODO: Check name pattern/size validity as well.
    if (universeName == null || universeName.isEmpty()) {
      return ApiResponse.error(BAD_REQUEST, "Invalid universe name.");
    }

    if (masterAddresses == null || masterAddresses.isEmpty()) {
      return ApiResponse.error(BAD_REQUEST, "Invalid master addresses list.");
    }

    //---------------------------------------------------------------------------------------------
    // Get the user specified master node ips.
    //---------------------------------------------------------------------------------------------
    List<String> userMasterIps = getMastersList(masterAddresses);
    if (userMasterIps == null) {
      return ApiResponse.error(BAD_REQUEST, "Could not parse host:port from masterAddresseses: " +
                               masterAddresses);
    }

    //---------------------------------------------------------------------------------------------
    // Create a universe object in the DB with the information so far: master info, cloud, etc.
    //---------------------------------------------------------------------------------------------
    Universe universe = null;
    try {
      universe = createNewUniverseForImport(customer, importForm, "yb-" + universeName,
                                            universeName, userMasterIps);
      results.with("checks").put("create_db_entry", "OK");
      results.put("universeUUID", universe.universeUUID.toString());
    } catch (Exception e) {
      results.with("checks").put("create_db_entry", "FAILURE");
      results.put("error", e.getMessage());
      return ApiResponse.error(INTERNAL_SERVER_ERROR, results);
    }
    UniverseDefinitionTaskParams taskParams = universe.getUniverseDetails();

    //---------------------------------------------------------------------------------------------
    // Verify the master processes are running on the master nodes.
    //---------------------------------------------------------------------------------------------
    if (!verifyMastersRunning(taskParams, results)) {
      return ApiResponse.error(INTERNAL_SERVER_ERROR, results);
    }

    //---------------------------------------------------------------------------------------------
    // Wait for the master leader election if needed.
    //---------------------------------------------------------------------------------------------
    if (!verifyMasterLeaderExists(taskParams, results)) {
      return ApiResponse.error(INTERNAL_SERVER_ERROR, results);
    }

    LOG.info("Done importing masters " + masterAddresses);
    // Update the state to IMPORTED_MASTERS.
    results.put("state", ImportUniverseFormData.State.IMPORTED_MASTERS.toString());

    return ApiResponse.success(results);
  }

  /**
   * Import the various tablet servers from the masters.
   */
  private Result importUniverseTservers(ImportUniverseFormData importForm, 
                                        Customer customer,
                                        ObjectNode results) {
    String masterAddresses = importForm.masterAddresses;

    if (importForm.universeUUID == null || importForm.universeUUID.toString().isEmpty()) {
      results.put("error", "Valid universe uuid needs to be set instead of " +
                           importForm.universeUUID);
      return ApiResponse.error(BAD_REQUEST, results);
    }

    Universe universe = Universe.get(importForm.universeUUID);

    if (universe == null) {
      results.put("error", "Invalid universe uuid " + importForm.universeUUID + ", universe not found.");
      return ApiResponse.error(BAD_REQUEST, results);
    }

    UniverseDefinitionTaskParams taskParams = universe.getUniverseDetails();
    // TODO: move this into a common location.
    results.put("universeUUID", universe.universeUUID.toString());

    //---------------------------------------------------------------------------------------------
    // Verify tservers count and list.
    //---------------------------------------------------------------------------------------------
    List<String> tservers_list = getTServers(masterAddresses, results);
    if (tservers_list.isEmpty()) {
      results.put("error", "No tservers known to the master leader in " + masterAddresses);
      ApiResponse.error(INTERNAL_SERVER_ERROR, results);
    }

    // Record the count of the tservers.
    results.put("tservers_count", tservers_list.size());
    ArrayNode arrayNode = results.putArray("tservers_list");
    for (String tserver : tservers_list) {
      arrayNode.add(tserver);
    }

    //---------------------------------------------------------------------------------------------
    // Update the universe object in the DB with new information : complete set of nodes.
    //---------------------------------------------------------------------------------------------
    // Find the provider, region and zone. These should have been created during master info update.
    Provider provider = Provider.get(customer.uuid, importForm.providerType);
    Region region = Region.getByCode(provider, importForm.regionCode);
    AvailabilityZone zone = AvailabilityZone.getByCode(importForm.zoneCode);
    // Update the universe object and refresh it.
    universe = addTServersToUniverse(tservers_list, taskParams, provider, region, zone);
    // Refresh the universe definition object as well.
    taskParams = universe.getUniverseDetails();


    //---------------------------------------------------------------------------------------------
    // Verify that the tservers processes are running.
    //---------------------------------------------------------------------------------------------
    UniverseDefinitionTaskBase checkTservers = new UniverseDefinitionTaskBase() {
      @Override
      public void run() {
        try {
          // Create the task list sequence.
          subTaskGroupQueue = new SubTaskGroupQueue(userTaskUUID);
          // Get the list of tservers.
          Set<NodeDetails> tserverNodes =
              taskParams().getNodesInCluster(taskParams().getPrimaryCluster().uuid);
          // Wait for tservers to be responsive.
          createWaitForServersTasks(tserverNodes, ServerType.TSERVER);
          // Run the task.
          subTaskGroupQueue.run();
        } catch (Throwable t) {
          LOG.error("Error executing task {}, error='{}'", getName(), t.getMessage(), t);
          throw t;
        }
      }
    };
    checkTservers.initialize(taskParams);
    // Execute the task. If it fails, return an error.
    if (!executeITask(checkTservers, "check_tservers_are_running", results)) {
      return ApiResponse.error(INTERNAL_SERVER_ERROR, results);
    }

    //---------------------------------------------------------------------------------------------
    // Wait for all these tservers to heartbeat to the master leader.
    //---------------------------------------------------------------------------------------------
    UniverseDefinitionTaskBase waitForTserverHBs = new UniverseDefinitionTaskBase() {
      @Override
      public void run() {
        try {
          // Create the task list sequence.
          subTaskGroupQueue = new SubTaskGroupQueue(userTaskUUID);
          // Wait for the master leader to hear from all the tservers.
          createWaitForTServerHeartBeatsTask();
          // Run the task.
          subTaskGroupQueue.run();
        } catch (Throwable t) {
          LOG.error("Error executing task {}, error='{}'", getName(), t.getMessage(), t);
          throw t;
        }
      }
    };
    waitForTserverHBs.initialize(taskParams);
    // Execute the task. If it fails, return an error.
    if (!executeITask(waitForTserverHBs, "check_tserver_heartbeats", results)) {
      return ApiResponse.error(INTERNAL_SERVER_ERROR, results);
    }

    LOG.info("Verified " + tservers_list.size() + " tservers present and imported them.");

    // Update the state to IMPORTED_TSERVERS.
    results.put("state", ImportUniverseFormData.State.IMPORTED_TSERVERS.toString());

    return ApiResponse.success(results);
  }


  /**
   * Finalizes the universe in the database by:
   *   - setting up Prometheus config for metrics.
   *   - adding the universe to the active list of universes for this customer.
   */
  private Result finishUniverseImport(ImportUniverseFormData importForm, 
                                      Customer customer,
                                      ObjectNode results) {
    if (importForm.universeUUID == null || importForm.universeUUID.toString().isEmpty()) {
      results.put("error", "Valid universe uuid needs to be set.");
      return ApiResponse.error(BAD_REQUEST, results);
    }

    Universe universe = Universe.get(importForm.universeUUID);
    if (universe == null) {
      results.put("error", "Universe uuid was not created.");
      return ApiResponse.error(BAD_REQUEST, results);
    }

    UniverseDefinitionTaskParams taskParams = universe.getUniverseDetails();
    // TODO: move to common location.
    results.put("universeUUID", universe.universeUUID.toString());

    //---------------------------------------------------------------------------------------------
    // Configure metrics.
    //---------------------------------------------------------------------------------------------

    // TODO: verify we can reach the various YB ports.
    // TODO: verify node exporter is running on the various nodes.

    UniverseDefinitionTaskBase createPrometheusConfig = new UniverseDefinitionTaskBase() {
      @Override
      public void run() {
        try {
          // Create the task list sequence.
          subTaskGroupQueue = new SubTaskGroupQueue(userTaskUUID);
          // Create a Prometheus config to pull from targets.
          createSwamperTargetUpdateTask(false /* removeFile */);
          // Run the task.
          subTaskGroupQueue.run();
        } catch (Throwable t) {
          LOG.error("Error executing task {}, error='{}'", getName(), t.getMessage(), t);
          throw t;
        }
      }
    };
    createPrometheusConfig.initialize(taskParams);
    // Execute the task. If it fails, return an error.
    if (!executeITask(createPrometheusConfig, "create_prometheus_config", results)) {
      return ApiResponse.error(INTERNAL_SERVER_ERROR, results);
    }


    //---------------------------------------------------------------------------------------------
    // Create a simple redis table if needed.
    //---------------------------------------------------------------------------------------------


    //---------------------------------------------------------------------------------------------
    // Update the DNS entry for all the clusters in this universe.
    //---------------------------------------------------------------------------------------------


    //---------------------------------------------------------------------------------------------
    // Finalize universe entry in DB
    //---------------------------------------------------------------------------------------------
    // Add the universe to the current user account
    customer.addUniverseUUID(universe.universeUUID);
    customer.save();

    // Update the state to DONE.
    results.put("state", ImportUniverseFormData.State.FINISHED.toString());

    LOG.info("Completed " + universe.universeUUID + " import.");

    return ApiResponse.success(results);
  }


  /**
   * Helper function to add a list of tservers into an existing universe. It sets the tserver flag
   * if the node already exists, and creates a new node entry if the node does not exist.
   */
  private Universe addTServersToUniverse(List<String> tserverList,
                                         UniverseDefinitionTaskParams taskParams,
                                         Provider provider, Region region, AvailabilityZone zone) {
    // Update the node details and persist into the DB.
    UniverseUpdater updater = new UniverseUpdater() {
      @Override
      public void run(Universe universe) {
        // Get the details of the universe to be updated.
        UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
        Cluster cluster = universeDetails.getPrimaryCluster();
        int index = cluster.index;

        for (String nodeIP : tserverList) {
          // Check if this node is already present.
          NodeDetails node = universe.getNodeByPrivateIP(nodeIP);
          // If the node is already present, set the tserver flag.
          if (node != null) {
            node.isTserver = true;
            continue;
          }

          // If the node is not present, create the node details and add it.
          NodeDetails newNode = createAndAddNode(universeDetails, nodeIP, provider, region, zone, index,
                                                 cluster.userIntent.instanceType);
          // This node is only a tserver and not a master.
          newNode.isMaster = false;
          newNode.isTserver = true;
          index++;
        }

        // Update the number of nodes in the user intent. TODO: update the correct cluster.
        cluster.userIntent.numNodes = tserverList.size();
        cluster.index = index;

        // Update the node details.
        universe.setUniverseDetails(universeDetails);
      }
    };

    // Save the updated universe object and return the updated universe.
    return Universe.saveDetails(taskParams.universeUUID, updater);
  }

  /**
   * This method queries the master leader and returns a list of tserver ip addresses.
   * TODO: We need to get the number of nodes information also from the end user and check that
   *       count matches what master leader provides, to ensure no unreachable/failed tservers.
   */
  private List<String> getTServers(String masterAddresses, ObjectNode results) {
    List<String> tservers_list = new ArrayList<String>();
    YBClient client = null;
    try {
      // Get the client to the YB master service.
      client = ybService.getClient(masterAddresses);

      // Fetch the tablet servers.
      ListTabletServersResponse listTServerResp = client.listTabletServers();

      for (ServerInfo tserver : listTServerResp.getTabletServersList()) {
        tservers_list.add(tserver.getHost());
      }
      results.with("checks").put("find_tservers_list", "OK");
    } catch (Exception e) {
      LOG.error("Hit error: ", e);
      results.with("checks").put("find_tservers_list", "FAILURE");
    } finally {
      ybService.closeClient(client, masterAddresses);
    }

    return tservers_list;
  }

  /**
   * This method executes the passed in task on a threadpool and waits for it to complete. Upon a
   * successful completion of the task, it returns true and adds the stage that completed to the
   * results object. Upon a failure, it returns false and adds the appropriate error message into
   * the results object.
   */
  private boolean executeITask(ITask task, String taskName, ObjectNode results) {
    // Initialize the threadpool if needed.
    initializeThreadpool();
    // Submit the task, and get a future object.
    Future<?> future = executor.submit(task);
    try {
      // Wait for the task to complete.
      future.get();
      // Indicate that this task executed successfully.
      results.with("checks").put(taskName, "OK");
    } catch (Exception e) {
      // If this task failed, return the failure and the reason.
      results.with("checks").put(taskName, "FAILURE");
      results.put("error", e.getMessage());
      LOG.error("Failed to execute " + taskName, e);
      return false;
    }
    return true;
  }

  /**
   * Creates a new universe object for the purpose of importing it. Note that we would not have all
   * the information about the universe at this point, so we are just creating the master ip parts.
   */
  private Universe createNewUniverseForImport(Customer customer, ImportUniverseFormData importForm,
                                              String nodePrefix, String universeName,
                                              List<String> userMasterIps) {
    // Find the provider by the code given, or create a new provider if one does not exist.
    Provider provider = Provider.get(customer.uuid, importForm.providerType);
    if (provider == null) {
      provider = Provider.create(customer.uuid, importForm.providerType, importForm.cloudName);
    }
    // Find the region by the code given, or create a new provider if one does not exist.
    Region region = Region.getByCode(provider, importForm.regionCode);
    if (region == null) {
      region = Region.create(provider, importForm.regionCode, importForm.regionName, null);
    }
    // Find the zone by the code given, or create a new provider if one does not exist.
    AvailabilityZone zone = AvailabilityZone.getByCode(importForm.zoneCode);
    if (zone == null) {
      zone = AvailabilityZone.create(region, importForm.zoneCode, importForm.zoneName, null);
    }

    // Create the universe definition task params.
    UniverseDefinitionTaskParams taskParams = new UniverseDefinitionTaskParams();
    // We will assign this universe a random UUID. This will get fixed later on once we find the
    // universe uuid.
    taskParams.universeUUID = UUID.randomUUID();
    taskParams.nodePrefix = nodePrefix;

    // Set the various details in the user intent.
    UniverseDefinitionTaskParams.UserIntent userIntent =
        new UniverseDefinitionTaskParams.UserIntent();
    userIntent.universeName = universeName;
    userIntent.provider = provider.uuid.toString();
    userIntent.regionList = new ArrayList<UUID>();
    userIntent.regionList.add(region.uuid);
    userIntent.providerType = importForm.providerType;
    userIntent.instanceType = importForm.instanceType;

    // TODO: This needs to be added for things to show up right in the UI.
    PlacementInfo placementInfo = null;

    // Add the placement info and user intent.
    Cluster cluster = taskParams.upsertPrimaryCluster(userIntent, placementInfo);
    // Create the node details set. This is a partial set that contains only the master nodes.
    taskParams.nodeDetailsSet = new HashSet<>();
    int index = 1;
    for (String nodeIP : userMasterIps) {
      NodeDetails nodeDetails = createAndAddNode(taskParams, nodeIP, provider, region, zone, index,
                                                 userIntent.instanceType);

      // At this point, we know only that this node is a master. We do not know if it is a tserver.
      nodeDetails.isMaster = true;
      nodeDetails.isTserver = false;
      index++;
    }
    cluster.index = index;

    // Create the universe object in the database.
    Universe universe = Universe.create(taskParams, customer.getCustomerId());

    // Return the universe we just created.
    return universe;
  }

  // Create a new node with the given placement information.
  private NodeDetails createAndAddNode(UniverseDefinitionTaskParams taskParams, String nodeIP,
                                       Provider provider, Region region, AvailabilityZone zone,
                                       int index, String instanceType) {
    NodeDetails nodeDetails = new NodeDetails();
    // Set the node index and node details.
    nodeDetails.nodeIdx = index;
    nodeDetails.nodeName = taskParams.nodePrefix + Universe.NODEIDX_PREFIX + nodeDetails.nodeIdx;
    nodeDetails.azUuid = zone.uuid;

    // Set this node as a part of the primary cluster.
    nodeDetails.placementUuid = taskParams.getPrimaryCluster().uuid;
    
    // Set this node as live and running.
    nodeDetails.state = NodeDetails.NodeState.Live;

    // Set the node name and the private ip address. TODO: Set public ip.
    nodeDetails.cloudInfo = new CloudSpecificInfo();
    nodeDetails.cloudInfo.private_ip = nodeIP;
    nodeDetails.cloudInfo.cloud = provider.code;
    nodeDetails.cloudInfo.region = region.code;
    nodeDetails.cloudInfo.az = zone.code;
    nodeDetails.cloudInfo.instance_type = instanceType;

    // Add the node and increment the next node index.
    taskParams.nodeDetailsSet.add(nodeDetails);

    return nodeDetails;
  }

  private void initializeThreadpool() {
    if (executor != null) {
      return;
    }

    // Initialize the tasks threadpool.
    ThreadFactory namedThreadFactory =
        new ThreadFactoryBuilder().setNameFormat("Import-Pool-%d").build();
    // Create an task pool which can handle an unbounded number of tasks, while using an initial set
    // of threads that get spawned upto TASK_THREADS limit.
    executor =
        new ThreadPoolExecutor(TASK_THREADS, TASK_THREADS, THREAD_ALIVE_TIME,
                               TimeUnit.SECONDS, new LinkedBlockingQueue<Runnable>(),
                               namedThreadFactory);
    LOG.info("Started Import Thread Pool.");
  }
}
