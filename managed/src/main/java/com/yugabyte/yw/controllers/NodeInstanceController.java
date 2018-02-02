// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;

import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;

import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.models.*;
import com.yugabyte.yw.models.helpers.TaskType;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.google.inject.Inject;

import com.yugabyte.yw.common.ApiResponse;
import com.yugabyte.yw.forms.NodeInstanceFormData;
import com.yugabyte.yw.forms.NodeInstanceFormData.NodeInstanceData;

import play.data.Form;
import play.data.FormFactory;
import play.libs.Json;
import play.mvc.Result;
import play.mvc.Results;

public class NodeInstanceController extends AuthenticatedController {
  @Inject
  FormFactory formFactory;

  @Inject
  Commissioner commissioner;

  public static final Logger LOG = LoggerFactory.getLogger(NodeInstanceController.class);

  /**
   * GET endpoint for Node data
   * @param customerUuid the customer UUID
   * @param nodeUuid the node UUID
   * @return JSON response with Node data
   */
  public Result get(UUID customerUuid, UUID nodeUuid) {
    String error = validateParams(customerUuid, null /* zoneUuid */);
    if (error != null) return ApiResponse.error(BAD_REQUEST, error);

    try {
      NodeInstance node = NodeInstance.get(nodeUuid);
      return ApiResponse.success(node);
    } catch (Exception e) {
      return ApiResponse.error(INTERNAL_SERVER_ERROR, e.getMessage());
    }
  }

  /**
   * GET endpoint for getting all unused nodes under a zone
   * @param customerUuid the customer UUID
   * @param zoneUuid the zone UUID
   * @return JSON response with list of nodes
   */
  public Result listByZone(UUID customerUuid, UUID zoneUuid) {
    String error = validateParams(customerUuid, zoneUuid);
    if (error != null) return ApiResponse.error(BAD_REQUEST, error);

    try {
      List<NodeInstance> nodes = NodeInstance.listByZone(zoneUuid, null /* instanceTypeCode */);
      return ApiResponse.success(nodes);
    } catch (Exception e) {
      return ApiResponse.error(INTERNAL_SERVER_ERROR, e.getMessage());
    }
  }

  public Result listByProvider(UUID customerUUID, UUID providerUUID) {
    List<NodeInstance> regionList;
    try {
      regionList = NodeInstance.listByProvider(providerUUID);
    } catch (Exception e) {
      return ApiResponse.error(INTERNAL_SERVER_ERROR, e.getMessage());
    }
    return ApiResponse.success(regionList);
  }
  /**
   * POST endpoint for creating new Node(s)
   * @param customerUuid the customer UUID
   * @param zoneUuid the zone UUID
   * @return JSON response of newly created Nodes
   */
  public Result create(UUID customerUuid, UUID zoneUuid) {
    String error = validateParams(customerUuid, zoneUuid);
    if (error != null) return ApiResponse.error(BAD_REQUEST, error);

    Form<NodeInstanceFormData> formData = formFactory.form(NodeInstanceFormData.class).bindFromRequest();
    List<NodeInstanceData> nodeDataList = formData.get().nodes;
    Map<String, NodeInstance> nodes = new HashMap<>();
    try {
      for (NodeInstanceData nodeData : nodeDataList) {
        NodeInstance node = NodeInstance.create(zoneUuid, nodeData);
        nodes.put(node.getDetails().ip, node);
      }
      return ApiResponse.success(nodes);
    } catch (Exception e) {
      return ApiResponse.error(INTERNAL_SERVER_ERROR, e.getMessage());
    }
  }


  public Result deleteNode(UUID customerUUID, UUID universeUUID, String nodeName) {
    try {
      // Validate customer UUID and universe UUID and AWS provider.
      Customer customer = Customer.get(customerUUID);
      if (customer == null) {
        String errMsg = "Invalid Customer UUID: " + customerUUID;
        LOG.error(errMsg);
        return ApiResponse.error(BAD_REQUEST, errMsg);
      }
      Universe universe = Universe.get(universeUUID);
      if (universe == null) {
        String errMsg = "Invalid Universe UUID: " + universeUUID;
        LOG.error(errMsg);
        return ApiResponse.error(BAD_REQUEST, errMsg);
      }
      if (nodeName == null || nodeName.trim().equals("")) {
        String errMsg = "Node Name Cannot be Empty";
        LOG.error(errMsg);
        return ApiResponse.error(BAD_REQUEST, errMsg);
      }
      NodeTaskParams taskParams = new NodeTaskParams();
      taskParams.universeUUID = universe.universeUUID;
      taskParams.expectedUniverseVersion = universe.version;
      taskParams.nodeName = nodeName;
      LOG.info("Deleting Node {} from  universe {} : name={} at version={}.",
        nodeName, universe.universeUUID, universe.name, universe.version);
      UUID taskUUID = commissioner.submit(TaskType.DeleteNodeFromUniverse, taskParams);
      CustomerTask.create(customer, universe.universeUUID,
        taskUUID, CustomerTask.TargetType.Node,
        CustomerTask.TaskType.Delete, nodeName);
      LOG.info("Saved task uuid {} in customer tasks table for universe {} : {} for node {}",
        taskUUID, universe.universeUUID, universe.name, nodeName);
      ObjectNode resultNode = Json.newObject();
      resultNode.put("taskUUID", taskUUID.toString());
      return Results.status(OK, resultNode);
    } catch (Exception e) {
      return ApiResponse.error(INTERNAL_SERVER_ERROR, e.getMessage());
    }
  }

  /**
   * Endpoint deletes the configured instance for a provider.
   * Since instance name and instance uuid are absent in a pristine (unused) instance
   * We use IP to query for Instance and delete it
   */
  public Result deleteInstance(UUID customerUUID, UUID providerUUID, String instanceIP) {
    try {
      // Validate customer UUID and universe UUID and AWS provider.
      Customer customer = Customer.get(customerUUID);
      if (customer == null) {
        String errMsg = "Invalid Customer UUID: " + customerUUID;
        LOG.error(errMsg);
        return ApiResponse.error(BAD_REQUEST, errMsg);
      }
      Provider provider = Provider.get(providerUUID);
      if (provider == null) {
        String errMsg = "Invalid Provider UUID: " + providerUUID;
        LOG.error(errMsg);
        return ApiResponse.error(BAD_REQUEST, errMsg);
      }
      List<NodeInstance> nodesInProvider = NodeInstance.listByProvider(providerUUID);
      NodeInstance nodeToBeFound = null;
      for (int a = 0; a < nodesInProvider.size(); a++) {
        if (nodesInProvider.get(a).getDetails().ip.equals(instanceIP)) {
          nodeToBeFound = nodesInProvider.get(a);
        }
      }
      if (nodeToBeFound != null) {
        nodeToBeFound.delete();
        return Results.status(OK);
      } else {
        return ApiResponse.error(BAD_REQUEST, "Node Not Found");
      }
    } catch (Exception e) {
      return ApiResponse.error(500, e.getMessage());
    }
  }

  /**
   * Endpoint stops processes on a node with a given nodeName.
   * The stopped node Master and TServer are terminated and no longer part of quorum.
   * @param customerUUID UUID of the current customer
   * @param universeUUID UUID of the universe containing the node
   * @param nodeName name of the node to be stoppped
   * @return Result object containing taskUUID
   */
  public Result stopNode(UUID customerUUID, UUID universeUUID, String nodeName) {
    String apiParamsErrorMessage = validateParams(customerUUID, universeUUID, nodeName);
    if (apiParamsErrorMessage != null) {
      return ApiResponse.error(BAD_REQUEST, apiParamsErrorMessage);
    }
    try {
      Universe universe = Universe.get(universeUUID);
      Customer customer =  Customer.get(customerUUID);
      NodeTaskParams taskParams = new NodeTaskParams();
      taskParams.universeUUID = universe.universeUUID;
      taskParams.expectedUniverseVersion = universe.version;
      taskParams.nodeName = nodeName;
      LOG.info("Stopping Node {} in  universe {} : name={} at version={}.",
        nodeName, universe.universeUUID, universe.name, universe.version);
      // TODO Implement the Tasks and uncomment these lines
/*      UUID taskUUID = commissioner.submit(TaskType.StopNodeInUniverse, taskParams);
        CustomerTask.create(customer, universe.universeUUID,
        taskUUID, CustomerTask.TargetType.Node,
        CustomerTask.TaskType.Stop, nodeName);
        LOG.info("Saved task uuid {} in customer tasks table for universe {} : {} for node {}",
        taskUUID, universe.universeUUID, universe.name, nodeName);
        ObjectNode resultNode = Json.newObject();
        resultNode.put("taskUUID", taskUUID.toString());*/
      return Results.status(OK);
    } catch (Exception e) {
      return ApiResponse.error(INTERNAL_SERVER_ERROR, e.getMessage());
    }
  }

  /**
   * Endpoint starts processes on a node with a given nodeName.
   * The Node Tserver is started and Master is started(if adding master completes quorum)
   * @param customerUUID UUID of the current customer
   * @param universeUUID UUID of the universe containing the node
   * @param nodeName name of the node to be stoppped
   * @return Result object containing taskUUID
   */
  public Result startNode(UUID customerUUID, UUID universeUUID, String nodeName) {
    String apiParamsErrorMessage = validateParams(customerUUID, universeUUID, nodeName);
    if (apiParamsErrorMessage != null) {
      return ApiResponse.error(BAD_REQUEST, apiParamsErrorMessage);
    }
    try {
      Universe universe = Universe.get(universeUUID);
      Customer customer =  Customer.get(customerUUID);
      NodeTaskParams taskParams = new NodeTaskParams();
      taskParams.universeUUID = universe.universeUUID;
      taskParams.expectedUniverseVersion = universe.version;
      taskParams.nodeName = nodeName;
      LOG.info("Starting Node {} in  universe {} : name={} at version={}.",
        nodeName, universe.universeUUID, universe.name, universe.version);
      // TODO Implement the Tasks and uncomment these lines
      /* UUID taskUUID = commissioner.submit(TaskType.StartNodeInUniverse, taskParams);
         CustomerTask.create(customer, universe.universeUUID,
         taskUUID, CustomerTask.TargetType.Node,
         CustomerTask.TaskType.Start, nodeName);
         LOG.info("Saved task uuid {} in customer tasks table for universe {} : {} for node {}",
         taskUUID, universe.universeUUID, universe.name, nodeName);
         ObjectNode resultNode = Json.newObject();
         resultNode.put("taskUUID", taskUUID.toString()); */
      return Results.status(OK);
    } catch (Exception e) {
      return ApiResponse.error(INTERNAL_SERVER_ERROR, e.getMessage());
    }
  }

  private String validateParams(UUID customerUuid, UUID zoneUuid) {
    String e = null;
    e = validateClassByUuid(Customer.class, customerUuid);
    if (e != null) return e;
    e = validateClassByUuid(AvailabilityZone.class, zoneUuid);
    if (e != null) return e;
    return e;
  }

  private String validateParams(UUID customerUUID, UUID universeUUID, String nodeName) {
    String e = null;
    e = validateClassByUuid(Customer.class, customerUUID);
    if (e != null) return e;
    e = validateClassByUuid(Universe.class, universeUUID);
    if (e != null) return e;
    if (nodeName == null || nodeName.trim().equals("")) {
      e = "Node Name Cannot be Empty";
    }
    return e;
  }

  private String validateClassByUuid(Class c, UUID uuid) {
    try {
      if (uuid != null && c.getMethod("get", UUID.class).invoke(null, uuid) == null) {
        return "Invalid " + c.getName() + "UUID: " + uuid;
      }
    } catch (Throwable t) {
      return t.getMessage();
    }
    return null;
  }
}
