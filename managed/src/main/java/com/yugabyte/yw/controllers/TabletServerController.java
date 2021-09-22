// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;

import java.util.UUID;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.inject.Inject;
import com.yugabyte.yw.common.ApiHelper;
import com.yugabyte.yw.common.ApiResponse;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;

import play.mvc.Result;
import play.mvc.Results;

public class TabletServerController extends AuthenticatedController {
  public static final Logger LOG = LoggerFactory.getLogger(TabletServerController.class);
  @Inject ApiHelper apiHelper;
  private final YBClientService ybService;

  @Inject
  public TabletServerController(YBClientService service) {
    this.ybService = service;
  }

  /**
   * This API would query for all the tabletServers using YB Client and return a JSON with tablet
   * server UUIDs
   *
   * @return Result tablet server uuids
   */
  public Result list() {
    // 501 not implemented
    return Results.TODO;
  }

  /**
   * This API directly queries the leader master server for a list of tablet servers in a universe
   *
   * @param customerUUID UUID of the customer
   * @param universeUUID UUID of the universe
   * @return Result tablet server information
   */
  public Result listTabletServers(UUID customerUUID, UUID universeUUID) {
    // Validate customer UUID
    Customer customer = Customer.getOrBadRequest(customerUUID);
    // Validate universe UUID
    Universe universe = Universe.getOrBadRequest(universeUUID);

    final String masterLeaderIPAddr = universe.getMasterLeaderHostText();
    if (masterLeaderIPAddr.isEmpty()) {
      final String errMsg = "Could not find the master leader address in universe " + universeUUID;
      LOG.error(errMsg);
      return ApiResponse.error(INTERNAL_SERVER_ERROR, errMsg);
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
      return ApiResponse.error(INTERNAL_SERVER_ERROR, e.getMessage());
    }
    return ApiResponse.success(response);
  }
}
