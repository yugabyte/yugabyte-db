// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;

import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType;
import com.yugabyte.yw.common.KubernetesManagerFactory;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.rbac.PermissionInfo.Action;
import com.yugabyte.yw.common.rbac.PermissionInfo.ResourceType;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.forms.PlatformResults;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.common.YbaApi;
import com.yugabyte.yw.models.helpers.CloudSpecificInfo;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.rbac.annotations.AuthzPath;
import com.yugabyte.yw.rbac.annotations.PermissionAttribute;
import com.yugabyte.yw.rbac.annotations.RequiredPermissionOnResource;
import com.yugabyte.yw.rbac.annotations.Resource;
import com.yugabyte.yw.rbac.enums.SourceType;
import io.swagger.annotations.Api;
import io.swagger.annotations.ApiOperation;
import io.swagger.annotations.Authorization;
import java.util.ArrayList;
import java.util.List;
import java.util.UUID;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.mvc.Controller;
import play.mvc.Result;

@Api(
    value = "Universe node metadata (metamaster)",
    authorizations = @Authorization(AbstractPlatformController.API_KEY_AUTH))
public class MetaMasterController extends Controller {

  public static final Logger LOG = LoggerFactory.getLogger(MetaMasterController.class);

  @Inject private YBClientService ybService;

  @Inject KubernetesManagerFactory kubernetesManagerFactory;

  @ApiOperation(
      value = "List a universe's master nodes",
      response = MastersList.class,
      nickname = "getUniverseMasterNodes")
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.UNIVERSE, action = Action.READ),
        resourceLocation = @Resource(path = Util.UNIVERSES, sourceType = SourceType.ENDPOINT))
  })
  public Result get(UUID universeUUID) {
    // Lookup the entry for the instanceUUID.
    Universe universe = Universe.getOrBadRequest(universeUUID);
    // Return the result.
    MastersList masters = new MastersList();
    for (NodeDetails node : universe.getMasters()) {
      masters.add(MasterNode.fromUniverseNode(node));
    }
    return PlatformResults.withData(masters);
  }

  @ApiOperation(
      notes = "Available since YBA version 2.2.0.0.",
      value = "List a master node's addresses",
      response = String.class)
  @YbaApi(visibility = YbaApi.YbaApiVisibility.PUBLIC, sinceYBAVersion = "2.2.0.0")
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.UNIVERSE, action = Action.READ),
        resourceLocation = @Resource(path = Util.UNIVERSES, sourceType = SourceType.ENDPOINT))
  })
  public Result getMasterAddresses(UUID customerUUID, UUID universeUUID) {
    return getServerAddresses(customerUUID, universeUUID, ServerType.MASTER);
  }

  @ApiOperation(
      notes = "Available since YBA version 2.2.0.0.",
      value = "List a YQL server's addresses",
      response = String.class)
  @YbaApi(visibility = YbaApi.YbaApiVisibility.PUBLIC, sinceYBAVersion = "2.2.0.0")
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.UNIVERSE, action = Action.READ),
        resourceLocation = @Resource(path = Util.UNIVERSES, sourceType = SourceType.ENDPOINT))
  })
  public Result getYQLServerAddresses(UUID customerUUID, UUID universeUUID) {
    return getServerAddresses(customerUUID, universeUUID, ServerType.YQLSERVER);
  }

  @ApiOperation(
      notes = "Available since YBA version 2.2.0.0.",
      value = "List a YSQL server's addresses",
      response = String.class)
  @YbaApi(visibility = YbaApi.YbaApiVisibility.PUBLIC, sinceYBAVersion = "2.2.0.0")
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.UNIVERSE, action = Action.READ),
        resourceLocation = @Resource(path = Util.UNIVERSES, sourceType = SourceType.ENDPOINT))
  })
  public Result getYSQLServerAddresses(UUID customerUUID, UUID universeUUID) {
    return getServerAddresses(customerUUID, universeUUID, ServerType.YSQLSERVER);
  }

  @ApiOperation(
      notes = "Available since YBA version 2.2.0.0.",
      value = "List a REDIS server's addresses",
      response = String.class)
  @YbaApi(visibility = YbaApi.YbaApiVisibility.PUBLIC, sinceYBAVersion = "2.2.0.0")
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.UNIVERSE, action = Action.READ),
        resourceLocation = @Resource(path = Util.UNIVERSES, sourceType = SourceType.ENDPOINT))
  })
  public Result getRedisServerAddresses(UUID customerUUID, UUID universeUUID) {
    return getServerAddresses(customerUUID, universeUUID, ServerType.REDISSERVER);
  }

  private Result getServerAddresses(UUID customerUUID, UUID universeUUID, ServerType type) {
    // Verify the customer with this universe is present.
    Customer customer = Customer.getOrBadRequest(customerUUID);

    // Lookup the entry for the instanceUUID.
    Universe universe = Universe.getOrBadRequest(universeUUID, customer);
    // In case of Kubernetes universe we would fetch the service ip
    // instead of the POD ip.
    String serviceIPPort =
        kubernetesManagerFactory.getManager().getKubernetesServiceIPPort(type, universe);
    if (serviceIPPort != null) {
      return PlatformResults.withData(serviceIPPort);
    }

    switch (type) {
      case MASTER:
        return PlatformResults.withData(universe.getMasterAddresses());
      case YQLSERVER:
        return PlatformResults.withData(universe.getYQLServerAddresses());
      case YSQLSERVER:
        return PlatformResults.withData(universe.getYSQLServerAddresses());
      case REDISSERVER:
        return PlatformResults.withData(universe.getRedisServerAddresses());
      default:
        throw new IllegalArgumentException("Unexpected type " + type);
    }
  }

  public static class MastersList {
    public List<MasterNode> masters = new ArrayList<>();

    public void add(MasterNode masterNode) {
      masters.add(masterNode);
    }
  }

  public static class MasterNode {
    // Information about the node that is returned by the cloud provider.
    public CloudSpecificInfo cloudInfo;

    // The master rpc port.
    public int masterRpcPort;

    public static MasterNode fromUniverseNode(NodeDetails uNode) {
      MasterNode mNode = new MasterNode();
      mNode.cloudInfo = uNode.cloudInfo;
      mNode.masterRpcPort = uNode.masterRpcPort;

      return mNode;
    }
  }
}
