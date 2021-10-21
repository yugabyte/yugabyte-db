// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;

import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.ApiResponse;
import com.yugabyte.yw.common.NodeActionType;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.forms.NodeActionFormData;
import com.yugabyte.yw.forms.NodeInstanceFormData;
import com.yugabyte.yw.forms.NodeInstanceFormData.NodeInstanceData;
import com.yugabyte.yw.forms.PlatformResults;
import com.yugabyte.yw.forms.PlatformResults.YBPTask;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.CertificateInfo;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.NodeInstance;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.AllowedActionsHelper;
import io.swagger.annotations.Api;
import io.swagger.annotations.ApiImplicitParam;
import io.swagger.annotations.ApiImplicitParams;
import io.swagger.annotations.ApiOperation;
import io.swagger.annotations.Authorization;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.data.Form;
import play.libs.Json;
import play.mvc.Result;
import play.mvc.Results;

@Api(
    value = "Node instances",
    authorizations = @Authorization(AbstractPlatformController.API_KEY_AUTH))
public class NodeInstanceController extends AuthenticatedController {

  @Inject Commissioner commissioner;

  public static final Logger LOG = LoggerFactory.getLogger(NodeInstanceController.class);

  /**
   * GET endpoint for Node data
   *
   * @param customerUuid the customer UUID
   * @param nodeUuid the node UUID
   * @return JSON response with Node data
   */
  @ApiOperation(
      value = "Get a node instance",
      response = NodeInstance.class,
      nickname = "getNodeInstance")
  public Result get(UUID customerUuid, UUID nodeUuid) {
    Customer.getOrBadRequest(customerUuid);
    NodeInstance node = NodeInstance.getOrBadRequest(nodeUuid);
    return PlatformResults.withData(node);
  }

  /**
   * GET endpoint for getting all unused nodes under a zone
   *
   * @param customerUuid the customer UUID
   * @param zoneUuid the zone UUID
   * @return JSON response with list of nodes
   */
  @ApiOperation(
      value = "List all of a zone's node instances",
      response = NodeInstance.class,
      responseContainer = "List")
  public Result listByZone(UUID customerUuid, UUID zoneUuid) {
    Customer.getOrBadRequest(customerUuid);
    AvailabilityZone.getOrBadRequest(zoneUuid);

    try {
      List<NodeInstance> nodes = NodeInstance.listByZone(zoneUuid, null /* instanceTypeCode */);
      return PlatformResults.withData(nodes);
    } catch (Exception e) {
      throw new PlatformServiceException(INTERNAL_SERVER_ERROR, e.getMessage());
    }
  }

  @ApiOperation(
      value = "List all of a provider's node instances",
      response = NodeInstance.class,
      responseContainer = "List")
  public Result listByProvider(UUID customerUUID, UUID providerUUID) {
    List<NodeInstance> regionList;
    try {
      regionList = NodeInstance.listByProvider(providerUUID);
    } catch (Exception e) {
      throw new PlatformServiceException(INTERNAL_SERVER_ERROR, e.getMessage());
    }
    return PlatformResults.withData(regionList);
  }

  /**
   * POST endpoint for creating new Node(s)
   *
   * @param customerUuid the customer UUID
   * @param zoneUuid the zone UUID
   * @return JSON response of newly created Nodes
   */
  @ApiOperation(
      value = "Create a node instance",
      response = NodeInstance.class,
      responseContainer = "Map",
      nickname = "createNodeInstance")
  @ApiImplicitParams({
    @ApiImplicitParam(
        name = "Node instance",
        value = "Node instance data to be created",
        required = true,
        dataType = "com.yugabyte.yw.forms.NodeInstanceFormData",
        paramType = "body")
  })
  public Result create(UUID customerUuid, UUID zoneUuid) {
    Customer.getOrBadRequest(customerUuid);
    AvailabilityZone.getOrBadRequest(zoneUuid);

    Form<NodeInstanceFormData> formData =
        formFactory.getFormDataOrBadRequest(NodeInstanceFormData.class);
    List<NodeInstanceData> nodeDataList = formData.get().nodes;
    Map<String, NodeInstance> nodes = new HashMap<>();
    for (NodeInstanceData nodeData : nodeDataList) {
      if (!NodeInstance.checkIpInUse(nodeData.ip)) {
        NodeInstance node = NodeInstance.create(zoneUuid, nodeData);
        nodes.put(node.getDetails().ip, node);
      }
    }
    if (nodes.size() > 0) {
      auditService().createAuditEntry(ctx(), request(), Json.toJson(formData.data()));
      return PlatformResults.withData(nodes);
    }
    throw new PlatformServiceException(
        BAD_REQUEST, "Invalid nodes in request. Duplicate IP Addresses are not allowed.");
  }

  /**
   * Endpoint deletes the configured instance for a provider. Since instance name and instance uuid
   * are absent in a pristine (unused) instance We use IP to query for Instance and delete it
   */
  @ApiOperation(value = "Delete a node instance")
  public Result deleteInstance(UUID customerUUID, UUID providerUUID, String instanceIP) {
    // Validate customer UUID and universe UUID and AWS provider.
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Provider provider = Provider.getOrBadRequest(providerUUID);
    List<NodeInstance> nodesInProvider = NodeInstance.listByProvider(providerUUID);
    NodeInstance nodeToBeFound = null;
    for (NodeInstance node : nodesInProvider) {
      // TODO: Need to convert routes to use UUID instead of instances' IP address
      // See: https://github.com/yugabyte/yugabyte-db/issues/7936
      if (node.getDetails().ip.equals(instanceIP) && !node.inUse) {
        nodeToBeFound = node;
      }
    }
    if (nodeToBeFound != null) {
      nodeToBeFound.delete();
      auditService().createAuditEntry(ctx(), request());
      return Results.status(OK);
    } else {
      throw new PlatformServiceException(BAD_REQUEST, "Node Not Found");
    }
  }

  @ApiOperation(value = "Update a node", response = YBPTask.class)
  @ApiImplicitParams({
    @ApiImplicitParam(
        name = "Node action",
        value = "Node action data to be updated",
        required = true,
        dataType = "com.yugabyte.yw.forms.NodeActionFormData",
        paramType = "body")
  })
  public Result nodeAction(UUID customerUUID, UUID universeUUID, String nodeName) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getOrBadRequest(universeUUID);
    universe.getNodeOrBadRequest(nodeName);
    Form<NodeActionFormData> formData =
        formFactory.getFormDataOrBadRequest(NodeActionFormData.class);

    if (!universe.getUniverseDetails().isUniverseEditable()) {
      String errMsg = "Node actions cannot be performed on universe UUID " + universeUUID;
      LOG.error(errMsg);
      return ApiResponse.error(BAD_REQUEST, errMsg);
    }

    NodeTaskParams taskParams = new NodeTaskParams();
    taskParams.universeUUID = universe.universeUUID;
    taskParams.expectedUniverseVersion = universe.version;
    taskParams.nodeName = nodeName;
    taskParams.useSystemd = universe.getUniverseDetails().getPrimaryCluster().userIntent.useSystemd;
    NodeActionType nodeAction = formData.get().nodeAction;

    // Check deleting/removing a node will not go below the RF
    // TODO: Always check this for all actions?? For now leaving it as is since it breaks many tests
    if (nodeAction == NodeActionType.STOP || nodeAction == NodeActionType.REMOVE) {
      // Always check this?? For now leaving it as is since it breaks many tests
      new AllowedActionsHelper(universe, universe.getNode(nodeName))
          .allowedOrBadRequest(nodeAction);
    }
    if (nodeAction == NodeActionType.ADD
        || nodeAction == NodeActionType.START
        || nodeAction == NodeActionType.START_MASTER) {
      taskParams.clusters = universe.getUniverseDetails().clusters;
      taskParams.rootCA = universe.getUniverseDetails().rootCA;
      taskParams.clientRootCA = universe.getUniverseDetails().clientRootCA;
      taskParams.rootAndClientRootCASame = universe.getUniverseDetails().rootAndClientRootCASame;
      if (!CertificateInfo.isCertificateValid(taskParams.rootCA)) {
        String errMsg =
            String.format(
                "The certificate %s needs info. Update the cert" + " and retry.",
                CertificateInfo.get(taskParams.rootCA).label);
        LOG.error(errMsg);
        throw new PlatformServiceException(BAD_REQUEST, errMsg);
      }
    }

    if (nodeAction == NodeActionType.QUERY) {
      String errMsg = "Node action not allowed for this action type.";
      LOG.error(errMsg);
      throw new PlatformServiceException(BAD_REQUEST, errMsg);
    }

    LOG.info(
        "{} Node {} in universe={}: name={} at version={}.",
        nodeAction.toString(false),
        nodeName,
        universe.universeUUID,
        universe.name,
        universe.version);

    UUID taskUUID = commissioner.submit(nodeAction.getCommissionerTask(), taskParams);
    CustomerTask.create(
        customer,
        universe.universeUUID,
        taskUUID,
        CustomerTask.TargetType.Node,
        nodeAction.getCustomerTask(),
        nodeName);
    LOG.info(
        "Saved task uuid {} in customer tasks table for universe {} : {} for node {}",
        taskUUID,
        universe.universeUUID,
        universe.name,
        nodeName);
    auditService().createAuditEntry(ctx(), request(), Json.toJson(formData.data()), taskUUID);
    return new YBPTask(taskUUID).asResult();
  }
}
