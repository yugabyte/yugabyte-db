// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;

import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.inject.Inject;
import com.yugabyte.yw.common.services.YBClientService;

import org.yb.client.ListTablesResponse;
import play.libs.Json;
import play.mvc.*;

import java.util.UUID;

public class TablesController extends AuthenticatedController {
  private final YBClientService ybService;

  @Inject
  public TablesController(YBClientService service) { this.ybService = service; }

  /**
   * This API would query for all the tables using YB Client and return a JSON
   * with table names
   *
   * @return Result table names
   */
  public Result list() {
    ObjectNode result = Json.newObject();
    try {
      ListTablesResponse response = ybService.getClient(null).getTablesList();
      ArrayNode tableNames = result.putArray("table_names");
      response.getTablesList().forEach(table->{
        tableNames.add(table);
      });
    } catch (Exception e) {
      return internalServerError("Error: " + e.getMessage());
    }
    return ok(result);
  }

  public Result universeList(UUID customerUUID, UUID universeUUID) {
    ArrayNode resultNode = Json.newArray();
    ObjectNode node1 = Json.newObject();
    node1.put("tableType", "cassandra");
    node1.put("tableName", "table1");
    node1.put("tableUUID", "1");
    ObjectNode node2 = Json.newObject();
    node2.put("tableType", "redis");
    node2.put("tableName", "table2");
    node2.put("tableUUID", "2");
    resultNode.add(node1);
    resultNode.add(node2);
    return ok(resultNode);
  }

  public Result index(UUID customerUUID, UUID universeUUID, UUID tableUUID) {
    ObjectNode response = Json.newObject();
    response.put("tableType", "cassandra");
    response.put("tableName", "table1");
    response.put("tableUUID", tableUUID.toString());
    return ok(response);
  }
}
