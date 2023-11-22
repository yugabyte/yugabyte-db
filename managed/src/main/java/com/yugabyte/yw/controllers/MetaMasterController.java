// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;

import static com.yugabyte.yw.forms.UniverseDefinitionTaskParams.ExposingServiceState;

import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType;
import com.yugabyte.yw.common.KubernetesManagerFactory;
import com.yugabyte.yw.common.KubernetesUtil;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.rbac.PermissionInfo.Action;
import com.yugabyte.yw.common.rbac.PermissionInfo.ResourceType;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.forms.MasterNodesInfo;
import com.yugabyte.yw.forms.PlatformResults;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.common.YbaApi;
import com.yugabyte.yw.models.common.YbaApi.YbaApiVisibility;
import com.yugabyte.yw.models.helpers.CloudSpecificInfo;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.PlacementInfo;
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
import java.util.Map;
import java.util.Map.Entry;
import java.util.UUID;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.client.ListMastersResponse;
import org.yb.client.YBClient;
import org.yb.util.ServerInfo;
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
      value = "Available since YBA version 2.21.1.0. Lists all master nodes details",
      response = MasterNodesInfo.class,
      responseContainer = "List")
  @YbaApi(visibility = YbaApiVisibility.PUBLIC, sinceYBAVersion = "2.21.1.0")
  public Result getMasterNodesInfo(UUID customerUUID, UUID universeUUID) {
    // Validate customer UUID.
    Customer.getOrBadRequest(customerUUID);
    // Validate universe UUID.
    Universe universe = Universe.getOrBadRequest(universeUUID);
    List<MasterNodesInfo> masters = new ArrayList<>();
    YBClient client = null;
    String masterAddresses = universe.getMasterAddresses();
    String certificate = universe.getCertificateNodetoNode();

    if (masterAddresses == null || masterAddresses.isEmpty()) {
      throw new IllegalStateException("No master host/ports ");
    }

    try {
      client = ybService.getClient(masterAddresses, certificate);
      ListMastersResponse listMastersResp = client.listMasters();
      if (listMastersResp != null) {
        for (ServerInfo master : listMastersResp.getMasters()) {
          MasterNodesInfo masterInfo = new MasterNodesInfo(master);
          // masterInfo.masterUUID = master.getUuid();
          // masterInfo.port = master.getPort();
          // masterInfo.isLeader = master.isLeader();
          // masterInfo.host = master.getHost();
          masters.add(masterInfo);
        }
      }
    } catch (Exception e) {
      LOG.warn("Failed to get list of masters in universe {} - {} ", universeUUID, e.getMessage());
      throw new PlatformServiceException(INTERNAL_SERVER_ERROR, e.getMessage());
    } finally {
      ybService.closeClient(client, masterAddresses);
    }
    return PlatformResults.withData(masters);
  }

  @ApiOperation(
      value = "Available since YBA version 2.2.0.0. List a master node's addresses",
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
      value = "Available since YBA version 2.2.0.0. List a YQL server's addresses",
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
      value = "Available since YBA version 2.2.0.0. List a YSQL server's addresses",
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
      value = "Available since YBA version 2.2.0.0. List a REDIS server's addresses",
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
    String serviceIPPort = getKuberenetesServiceIPPort(type, universe);
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

  private String getKuberenetesServiceIPPort(ServerType type, Universe universe) {
    List<String> allIPs = new ArrayList<>();
    UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
    UniverseDefinitionTaskParams.Cluster primary = universeDetails.getPrimaryCluster();
    // If no service is exposed, fail early.
    if (primary.userIntent.enableExposingService == ExposingServiceState.UNEXPOSED) {
      return null;
    }
    Provider provider = Provider.get(UUID.fromString(primary.userIntent.provider));

    if (!primary.userIntent.providerType.equals(Common.CloudType.kubernetes)) {
      return null;
    } else {
      PlacementInfo pi = universeDetails.getPrimaryCluster().placementInfo;

      boolean isMultiAz = PlacementInfoUtil.isMultiAZ(provider);
      Map<UUID, Map<String, String>> azToConfig = KubernetesUtil.getConfigPerAZ(pi);

      for (Entry<UUID, Map<String, String>> entry : azToConfig.entrySet()) {
        UUID azUUID = entry.getKey();
        String azName = isMultiAz ? AvailabilityZone.get(azUUID).getCode() : null;

        Map<String, String> config = entry.getValue();

        String namespace =
            KubernetesUtil.getKubernetesNamespace(
                isMultiAz,
                universeDetails.nodePrefix,
                azName,
                config,
                universeDetails.useNewHelmNamingStyle,
                false);

        String helmReleaseName =
            KubernetesUtil.getHelmReleaseName(
                isMultiAz,
                universeDetails.nodePrefix,
                universe.getName(),
                azName,
                false,
                universeDetails.useNewHelmNamingStyle);

        String ip =
            kubernetesManagerFactory
                .getManager()
                .getPreferredServiceIP(
                    config,
                    helmReleaseName,
                    namespace,
                    type == ServerType.MASTER,
                    universeDetails.useNewHelmNamingStyle);
        if (ip == null) {
          return null;
        }

        int rpcPort;
        switch (type) {
          case MASTER:
            rpcPort = universeDetails.communicationPorts.masterRpcPort;
            break;
          case YSQLSERVER:
            rpcPort = universeDetails.communicationPorts.ysqlServerRpcPort;
            break;
          case YQLSERVER:
            rpcPort = universeDetails.communicationPorts.yqlServerRpcPort;
            break;
          case REDISSERVER:
            rpcPort = universeDetails.communicationPorts.redisServerRpcPort;
            break;
          default:
            throw new IllegalArgumentException("Unexpected type " + type);
        }
        allIPs.add(String.format("%s:%d", ip, rpcPort));
      }
      return String.join(",", allIPs);
    }
  }
}
