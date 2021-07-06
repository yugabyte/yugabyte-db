// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.inject.Inject;
import com.yugabyte.yw.common.ApiHelper;
import com.yugabyte.yw.common.YWServiceException;
import com.yugabyte.yw.forms.YWResults;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import io.swagger.annotations.*;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.mvc.Result;

import java.util.UUID;

@Api(
    value = "Tablet Server",
    authorizations = @Authorization(AbstractPlatformController.API_KEY_AUTH))
public class TabletServerController extends AuthenticatedController {
  private static final Logger LOG = LoggerFactory.getLogger(TabletServerController.class);
  private final ApiHelper apiHelper;

  @Inject
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
  @ApiOperation(value = "List of tablet server", response = Object.class, responseContainer = "Map")
  public Result listTabletServers(UUID customerUUID, UUID universeUUID) {
    // Validate customer UUID
    Customer.getOrBadRequest(customerUUID);
    // Validate universe UUID
    Universe universe = Universe.getOrBadRequest(universeUUID);

    final String masterLeaderIPAddr = universe.getMasterLeaderHostText();
    if (masterLeaderIPAddr.isEmpty()) {
      final String errMsg = "Could not find the master leader address in universe " + universeUUID;
      LOG.error(errMsg);
      throw new YWServiceException(INTERNAL_SERVER_ERROR, errMsg);
    }

    JsonNode response;
    // Query master leader tablet servers endpoint
    try {
      final int masterHttpPort = universe.getUniverseDetails().communicationPorts.masterHttpPort;
      final String masterLeaderUrl =
          String.format("http://%s:%s/api/v1/tablet-servers", masterLeaderIPAddr, masterHttpPort);
      response = apiHelper.getRequest(masterLeaderUrl);
    } catch (Exception e) {
      LOG.error("Failed to get list of tablet servers in universe " + universeUUID, e);
      throw new YWServiceException(INTERNAL_SERVER_ERROR, e.getMessage());
    }
    return YWResults.withRawData(response);
  }
}
