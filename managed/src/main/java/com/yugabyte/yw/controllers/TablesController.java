// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;

import java.util.List;
import java.util.UUID;

import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.forms.TableDefinitionTaskParams;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.ColumnDetails;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.client.GetTableSchemaResponse;
import org.yb.client.ListTablesResponse;
import org.yb.client.YBClient;
import org.yb.master.Master.ListTablesResponsePB.TableInfo;

import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.inject.Inject;
import com.yugabyte.yw.common.ApiResponse;
import com.yugabyte.yw.common.services.YBClientService;

import play.data.Form;
import play.data.FormFactory;
import play.libs.Json;
import play.mvc.Result;
import play.mvc.Results;

import static com.yugabyte.yw.common.Util.getUUIDRepresentation;
import static com.yugabyte.yw.forms.TableDefinitionTaskParams.createFromResponse;

public class TablesController extends AuthenticatedController {
  public static final Logger LOG = LoggerFactory.getLogger(TablesController.class);

  @Inject
  FormFactory formFactory;

  @Inject
  Commissioner commissioner;

  // The YB client to use.
  public YBClientService ybService;

  @Inject
  public TablesController(YBClientService service) { this.ybService = service; }

  public Result create(UUID customerUUID, UUID universeUUID) {

    try {

      // Validate customer UUID and universe UUID
      Customer customer = Customer.get(customerUUID);
      if (customer == null) {
        String errMsg = "Invalid Customer UUID: " + customerUUID;
        LOG.error(errMsg);
        return ApiResponse.error(BAD_REQUEST, errMsg);
      }
      Universe universe = Universe.get(universeUUID);

      Form<TableDefinitionTaskParams> formData = formFactory.form(TableDefinitionTaskParams.class)
        .bindFromRequest();
      TableDefinitionTaskParams taskParams = formData.get();

      // Submit the task to create the table.
      UUID taskUUID = commissioner.submit(TaskInfo.Type.CreateCassandraTable, taskParams);
      LOG.info("Submitted create table for {}:{}, task uuid = {}.",
        taskParams.tableUUID, taskParams.tableDetails.tableName, taskUUID);

      // Add this task uuid to the user universe.
      // TODO: check as to why we aren't populating the tableUUID from middleware
      // Which means all the log statements above and below are basically logging null?
      CustomerTask.create(customer,
        universe.universeUUID,
        taskUUID,
        CustomerTask.TargetType.Table,
        CustomerTask.TaskType.Create,
        taskParams.tableDetails.tableName);
      LOG.info("Saved task uuid {} in customer tasks table for table {}:{}",
        taskUUID, taskParams.tableUUID, taskParams.tableDetails.tableName);

      ObjectNode resultNode = Json.newObject();
      resultNode.put("taskUUID", taskUUID.toString());
      return Results.status(OK, resultNode);
    } catch (NullPointerException e) {
      LOG.error("Error creating table", e);
      // This error isn't useful at all, why send a NullPointerException as api response?
      return ApiResponse.error(BAD_REQUEST, "NullPointerException");
    } catch (RuntimeException e) {
      LOG.error("Error creating table", e);
      return ApiResponse.error(BAD_REQUEST, e.getMessage());
    } catch (Exception e) {
      LOG.error("Error creating table", e);
      return ApiResponse.error(INTERNAL_SERVER_ERROR, e.getMessage());
    }
  }

  public Result alter(UUID cUUID, UUID uniUUID, UUID tableUUID) {
    return play.mvc.Results.TODO;
  }

  public Result drop(UUID cUUID, UUID uniUUID, UUID tableUUID) {
    return play.mvc.Results.TODO;
  }

  public Result getColumnTypes() {
    return ok(Json.toJson(ColumnDetails.YQLDataType.values()));
  }

  public Result universeList(UUID customerUUID, UUID universeUUID) {
    try {

      // Validate customer UUID
      if (Customer.get(customerUUID) == null) {
        String errMsg = "Invalid Customer UUID: " + customerUUID;
        LOG.error(errMsg);
        return ApiResponse.error(BAD_REQUEST, errMsg);
      }

      // Validate universe UUID and retrieve master addresses
      final String masterAddresses = Universe.get(universeUUID).getMasterAddresses();
      if (masterAddresses.isEmpty()) {
        String errMsg = "Expected error. Masters are not currently queryable.";
        LOG.warn(errMsg);
        return ok(errMsg);
      }

      YBClient client = ybService.getClient(masterAddresses);
      ListTablesResponse response = client.getTablesList();
      List<TableInfo> tableInfoList = response.getTableInfoList();
      ArrayNode resultNode = Json.newArray();
      for (TableInfo table : tableInfoList) {
        ObjectNode node = Json.newObject();
        node.put("tableType", table.getTableType().toString());
        node.put("tableName", table.getName());
        String tableUUID =  table.getId().toStringUtf8();
        node.put("tableUUID", String.valueOf(getUUIDRepresentation(tableUUID)));
        resultNode.add(node);
      }
      return ok(resultNode);
    } catch (Exception e) {
      LOG.error("Failed to get list of tables in universe " + universeUUID, e);
      return ApiResponse.error(INTERNAL_SERVER_ERROR, e.getMessage());
    }
  }

  /**
   * This API will describe a single table.
   *
   * @param customerUUID UUID of the customer owning the table
   * @param universeUUID UUID of the universe in which the table resides
   * @param tableUUID UUID of the table to describe
   * @return json-serialized description of the table
   */
  public Result describe(UUID customerUUID, UUID universeUUID, UUID tableUUID) {
    try {

      // Validate customer UUID and universe UUID
      if (Customer.get(customerUUID) == null) {
        String errMsg = "Invalid Customer UUID: " + customerUUID;
        LOG.error(errMsg);
        return ApiResponse.error(BAD_REQUEST, errMsg);
      }
      Universe universe = Universe.get(universeUUID);

      final String masterAddresses = Universe.get(universeUUID).getMasterAddresses();
      if (masterAddresses.isEmpty()) {
        LOG.warn("Expected error. Masters are not currently queryable.");
        return ok("Expected error. Masters are not currently queryable.");
      }
      YBClient client = ybService.getClient(masterAddresses);
      GetTableSchemaResponse response = client.getTableSchemaByUUID(
        tableUUID.toString().replace("-", ""));

      return ok(Json.toJson(createFromResponse(universe, tableUUID, response)));
    } catch (IllegalArgumentException e) {
      LOG.error("Failed to get schema of table " + tableUUID + " in universe " + universeUUID, e);
      return ApiResponse.error(BAD_REQUEST, e.getMessage());
    } catch (Exception e) {
      LOG.error("Failed to get schema of table " + tableUUID + " in universe " + universeUUID, e);
      return ApiResponse.error(INTERNAL_SERVER_ERROR, e.getMessage());
    }
  }
}
