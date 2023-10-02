// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.controllers;

import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.rbac.PermissionInfo.Action;
import com.yugabyte.yw.common.rbac.PermissionInfo.ResourceType;
import com.yugabyte.yw.controllers.handlers.NodeAgentHandler;
import com.yugabyte.yw.controllers.handlers.NodeAgentHandler.NodeAgentDownloadFile;
import com.yugabyte.yw.forms.NodeAgentForm;
import com.yugabyte.yw.forms.NodeAgentResp;
import com.yugabyte.yw.forms.PlatformResults;
import com.yugabyte.yw.forms.PlatformResults.YBPSuccess;
import com.yugabyte.yw.forms.PlatformResults.YBPTask;
import com.yugabyte.yw.forms.ReinstallNodeAgentForm;
import com.yugabyte.yw.forms.paging.NodeAgentPagedApiQuery;
import com.yugabyte.yw.forms.paging.NodeAgentPagedApiResponse;
import com.yugabyte.yw.models.Audit;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.NodeAgent;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.paging.NodeAgentPagedQuery;
import com.yugabyte.yw.rbac.annotations.AuthzPath;
import com.yugabyte.yw.rbac.annotations.PermissionAttribute;
import com.yugabyte.yw.rbac.annotations.RequiredPermissionOnResource;
import com.yugabyte.yw.rbac.annotations.Resource;
import com.yugabyte.yw.rbac.enums.SourceType;
import io.swagger.annotations.Api;
import io.swagger.annotations.ApiImplicitParam;
import io.swagger.annotations.ApiImplicitParams;
import io.swagger.annotations.ApiOperation;
import io.swagger.annotations.Authorization;
import java.util.UUID;
import javax.inject.Inject;
import play.mvc.Http;
import play.mvc.Result;

@Api(
    value = "Node Agents",
    authorizations = @Authorization(AbstractPlatformController.API_KEY_AUTH))
public class NodeAgentController extends AuthenticatedController {

  @Inject NodeAgentHandler nodeAgentHandler;

  @ApiOperation(
      value = "Register Node Agent",
      response = NodeAgent.class,
      hidden = true,
      nickname = "RegisterNodeAgent")
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "NodeAgentForm",
          paramType = "body",
          dataType = "com.yugabyte.yw.forms.NodeAgentForm",
          required = true))
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.CREATE),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result register(UUID customerUuid, Http.Request request) {
    Customer.getOrBadRequest(customerUuid);
    NodeAgentForm payload = parseJsonAndValidate(request, NodeAgentForm.class);
    NodeAgent nodeAgent = nodeAgentHandler.register(customerUuid, payload);
    auditService()
        .createAuditEntry(
            request,
            Audit.TargetType.NodeAgent,
            nodeAgent.getUuid().toString(),
            Audit.ActionType.AddNodeAgent);
    return PlatformResults.withData(nodeAgent);
  }

  @ApiOperation(
      value = "List Node Agents",
      response = NodeAgentResp.class,
      responseContainer = "List",
      nickname = "ListNodeAgents")
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.READ),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result list(UUID customerUuid, String nodeIp) {
    return PlatformResults.withData(nodeAgentHandler.list(customerUuid, nodeIp));
  }

  @ApiOperation(
      value = "List Node Agents (paginated)",
      response = NodeAgentPagedApiResponse.class,
      nickname = "PageListNodeAgents")
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "PageNodeAgentRequest",
          paramType = "body",
          dataType = "com.yugabyte.yw.forms.paging.NodeAgentPagedApiQuery",
          required = true))
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.READ),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result page(UUID customerUuid, Http.Request request) {
    Customer.getOrBadRequest(customerUuid);
    NodeAgentPagedApiQuery apiQuery = parseJsonAndValidate(request, NodeAgentPagedApiQuery.class);
    NodeAgentPagedQuery query =
        apiQuery.copyWithFilter(apiQuery.getFilter().toFilter(), NodeAgentPagedQuery.class);
    NodeAgentPagedApiResponse response = nodeAgentHandler.pagedList(customerUuid, query);
    return PlatformResults.withData(response);
  }

  @ApiOperation(value = "Get Node Agent", response = NodeAgentResp.class, nickname = "GetNodeAgent")
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.READ),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result get(UUID customerUuid, UUID nodeUuid) {
    return PlatformResults.withData(nodeAgentHandler.get(customerUuid, nodeUuid));
  }

  @ApiOperation(
      value = "Update Node Agent State",
      response = NodeAgent.class,
      hidden = true,
      nickname = "UpdateNodeAgentState")
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "NodeAgentForm",
          paramType = "body",
          dataType = "com.yugabyte.yw.forms.NodeAgentForm",
          required = true))
  public Result updateState(UUID customerUuid, UUID nodeUuid, Http.Request request) {
    NodeAgentForm payload = parseJsonAndValidate(request, NodeAgentForm.class);
    NodeAgent nodeAgent = nodeAgentHandler.updateState(customerUuid, nodeUuid, payload);
    auditService()
        .createAuditEntry(
            request,
            Audit.TargetType.NodeAgent,
            nodeUuid.toString(),
            Audit.ActionType.UpdateNodeAgent);
    return PlatformResults.withData(nodeAgent);
  }

  @ApiOperation(
      value = "Unregister Node Agent",
      response = YBPSuccess.class,
      hidden = true,
      nickname = "UnregisterNodeAgent")
  public Result unregister(UUID customerUuid, UUID nodeUuid, Http.Request request) {
    NodeAgent.getOrBadRequest(customerUuid, nodeUuid);
    nodeAgentHandler.unregister(nodeUuid);
    auditService()
        .createAuditEntry(
            request,
            Audit.TargetType.NodeAgent,
            nodeUuid.toString(),
            Audit.ActionType.DeleteNodeAgent);
    return YBPSuccess.empty();
  }

  @ApiOperation(
      value = "Download Node Agent Installer or Package",
      response = String.class,
      produces = "application/gzip, application/x-sh",
      nickname = "DownloadNodeAgentInstaller")
  public Result download(String downloadType, String os, String arch) {
    NodeAgentDownloadFile fileToDownload =
        nodeAgentHandler.validateAndGetDownloadFile(downloadType, os, arch);
    return ok(fileToDownload.getContent())
        .withHeader(
            "Content-Disposition", "attachment; filename=" + fileToDownload.getContentType())
        .as(fileToDownload.getContentType());
  }

  @ApiOperation(
      value = "Reinstall Node Agent",
      response = NodeAgent.class,
      nickname = "ReinstallNodeAgent")
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "ReinstallNodeAgentForm",
          paramType = "body",
          dataType = "com.yugabyte.yw.forms.ReinstallNodeAgentForm",
          required = true))
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.CREATE),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result reinstall(UUID customerUuid, UUID universeUuid, Http.Request request) {
    Customer customer = Customer.getOrBadRequest(customerUuid);
    Universe universe = Universe.getOrBadRequest(universeUuid, customer);
    ReinstallNodeAgentForm payload = parseJsonAndValidate(request, ReinstallNodeAgentForm.class);
    UUID taskUuid = nodeAgentHandler.reinstall(customerUuid, universeUuid, payload);
    CustomerTask.create(
        customer,
        universeUuid,
        taskUuid,
        CustomerTask.TargetType.NodeAgent,
        CustomerTask.TaskType.Install,
        universe.getName());
    auditService()
        .createAuditEntry(
            request,
            Audit.TargetType.Universe,
            universeUuid.toString(),
            Audit.ActionType.Delete,
            taskUuid);
    return new YBPTask(taskUuid, universeUuid).asResult();
  }
}
