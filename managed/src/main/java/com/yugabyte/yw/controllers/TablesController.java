// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;

import java.util.List;
import java.util.UUID;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.client.ListTablesResponse;
import org.yb.client.YBClient;
import org.yb.master.Master.ListTablesResponsePB.TableInfo;

import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.inject.Inject;
import com.yugabyte.yw.common.ApiResponse;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;

import play.libs.Json;
import play.mvc.Result;

public class TablesController extends AuthenticatedController {
  public static final Logger LOG = LoggerFactory.getLogger(TablesController.class);

  // The YB client to use.
  public YBClientService ybService;

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
    // Verify the customer with this universe is present.
    Customer customer = Customer.get(customerUUID);
    if (customer == null) {
      return ApiResponse.error(BAD_REQUEST, "Invalid Customer UUID: " + customerUUID);
    }
    try {
      final String masterAddresses = Universe.get(universeUUID).getMasterAddresses();
      if (masterAddresses.isEmpty()) {
        throw new Exception("Masters are not currently queryable.");
      }
      YBClient client = ybService.getClient(masterAddresses);
      ListTablesResponse response = client.getTablesList();
      List<TableInfo> tableInfoList = response.getTableInfoList();
      ArrayNode resultNode = Json.newArray();
      for (TableInfo table : tableInfoList) {
        ObjectNode node = Json.newObject();
        node.put("tableType", table.getTableType().toString());
        node.put("tableName", table.getName());
        node.put("tableUUID", table.getId().toStringUtf8());
        resultNode.add(node);
      }
      return ok(resultNode);
    } catch (Exception e) {
      LOG.error("Failed to get list of tables in universe " + universeUUID, e);
      return ApiResponse.error(INTERNAL_SERVER_ERROR, e.getMessage());
    }
  }

  public Result index(UUID customerUUID, UUID universeUUID, UUID tableUUID) {
    ObjectNode response = Json.newObject();
    response.put("tableType", "cassandra");
    response.put("tableName", "table1");
    response.put("tableUUID", tableUUID.toString());
    return ok(response);
  }
}
