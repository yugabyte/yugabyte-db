// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.annotations.VisibleForTesting;
import com.google.inject.Inject;
import com.typesafe.config.Config;
import com.yugabyte.yw.common.ApiHelper;
import com.yugabyte.yw.common.CustomWsClientFactory;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.rbac.PermissionInfo.Action;
import com.yugabyte.yw.common.rbac.PermissionInfo.ResourceType;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.forms.PlatformResults;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.common.YbaApi;
import com.yugabyte.yw.rbac.annotations.AuthzPath;
import com.yugabyte.yw.rbac.annotations.PermissionAttribute;
import com.yugabyte.yw.rbac.annotations.RequiredPermissionOnResource;
import com.yugabyte.yw.rbac.annotations.Resource;
import com.yugabyte.yw.rbac.enums.SourceType;
import io.swagger.annotations.Api;
import io.swagger.annotations.ApiOperation;
import io.swagger.annotations.Authorization;
import java.util.Iterator;
import java.util.Map.Entry;
import java.util.UUID;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.client.ListTabletServersResponse;
import org.yb.client.YBClient;
import org.yb.util.ServerInfo;
import play.libs.ws.WSClient;
import play.mvc.Result;

@Api(
    value = "Tablet server management",
    authorizations = @Authorization(AbstractPlatformController.API_KEY_AUTH))
public class TabletServerController extends AuthenticatedController {
  private static final Logger LOG = LoggerFactory.getLogger(TabletServerController.class);
  private final ApiHelper apiHelper;
  private static final String SEPARATOR = ":";

  @Inject private YBClientService ybService;

  @Inject
  public TabletServerController(CustomWsClientFactory wsClientFactory, Config config) {
    WSClient wsClient = wsClientFactory.forCustomConfig(config.getValue(Util.YB_NODE_UI_WS_KEY));
    this.apiHelper = new ApiHelper(wsClient);
  }

  @VisibleForTesting
  public TabletServerController(ApiHelper apiHelper) {
    this.apiHelper = apiHelper;
  }

  /**
   * This API directly queries the leader master server for a list of tablet servers in a universe
   *
   * @param customerUUID UUID of the customer
   * @param universeUUID UUID of the universe
   * @return Result tablet server information
   */
  @ApiOperation(
      notes = "YbaApi Internal.",
      value = "List all tablet servers information",
      nickname = "listTabletServers",
      response = Object.class,
      responseContainer = "Map")
  @YbaApi(visibility = YbaApi.YbaApiVisibility.INTERNAL, sinceYBAVersion = "2.2.0.0")
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.UNIVERSE, action = Action.READ),
        resourceLocation = @Resource(path = Util.UNIVERSES, sourceType = SourceType.ENDPOINT))
  })
  public Result listTabletServers(UUID customerUUID, UUID universeUUID) {
    // Validate customer UUID
    Customer customer = Customer.getOrBadRequest(customerUUID);
    // Validate universe UUID
    Universe universe = Universe.getOrBadRequest(universeUUID, customer);
    JsonNode tabletServersAPIResp;
    YBClient client = null;
    String masterAddresses = universe.getMasterAddresses();
    String certificate = universe.getCertificateNodetoNode();

    final String masterLeaderIPAddr = universe.getMasterLeaderHostText();
    if (masterLeaderIPAddr.isEmpty() || masterAddresses.isEmpty()) {
      final String errMsg = "Could not find the master leader address in universe " + universeUUID;
      LOG.error(errMsg);
      throw new PlatformServiceException(INTERNAL_SERVER_ERROR, errMsg);
    }

    JsonNode response;
    // Query master leader tablet servers endpoint
    try {
      final int tserverPort = universe.getUniverseDetails().communicationPorts.tserverHttpPort;
      final int masterHttpPort = universe.getUniverseDetails().communicationPorts.masterHttpPort;
      final String masterLeaderUrl =
          String.format("http://%s:%s/api/v1/tablet-servers", masterLeaderIPAddr, masterHttpPort);
      tabletServersAPIResp = apiHelper.getRequest(masterLeaderUrl);

      if (tabletServersAPIResp != null && tabletServersAPIResp.size() > 0) {
        client = ybService.getClient(masterAddresses, certificate);
        Iterator<Entry<String, JsonNode>> iter = tabletServersAPIResp.fields();
        while (iter.hasNext()) {
          Entry<String, JsonNode> entry = iter.next();
          ListTabletServersResponse listTServerResp = client.listTabletServers();
          JsonNode tserverObject = entry.getValue();
          for (ServerInfo tserver : listTServerResp.getTabletServersList()) {
            String objectKey = tserver.getHost() + SEPARATOR + tserverPort;
            JsonNode tserverNode = tserverObject.get(objectKey);
            if (tserverNode != null) {
              ((ObjectNode) tserverNode).put("uuid", tserver.getUuid());
            }
          }
        }
        ybService.closeClient(client, masterAddresses);
      }
      return PlatformResults.withRawData(tabletServersAPIResp);
    } catch (Exception e) {
      LOG.error("Failed to get list of tablet servers in universe " + universeUUID, e);
      throw new PlatformServiceException(INTERNAL_SERVER_ERROR, e.getMessage());
    }
  }
}
