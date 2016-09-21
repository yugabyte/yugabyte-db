// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;

import org.yb.client.ListTabletServersResponse;

import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.inject.Inject;
import com.yugabyte.yw.common.services.YBClientService;

import play.libs.Json;
import play.mvc.Result;

public class TabletServerController extends AuthenticatedController {
  private final YBClientService ybService;

  @Inject
  public TabletServerController(YBClientService service) { this.ybService = service; }

  /**
   * This API would query for all the tabletServers using YB Client and return a JSON
   * with tablet server UUIDs
   *
   * @return Result tablet server uuids
   */
  public Result list() {
    ObjectNode result = Json.newObject();

    try {
        ListTabletServersResponse response = ybService.getClient(null).listTabletServers();
        result.put("count", response.getTabletServersCount());
        ArrayNode tabletServers = result.putArray("servers");
        response.getTabletServersList().forEach(tabletServer->{
            tabletServers.add(tabletServer.getHost());
        });
    } catch (Exception e) {
        return internalServerError("Error: " + e.getMessage());
    }

    return ok(result);
  }
}
