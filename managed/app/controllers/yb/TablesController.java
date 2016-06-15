// Copyright (c) YugaByte, Inc.

package controllers.yb;

import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.inject.Inject;
import controllers.AuthenticatedController;
import org.yb.client.ListTablesResponse;
import play.libs.Json;
import play.mvc.*;
import services.YBClientService;

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
      ListTablesResponse response = ybService.getClient().getTablesList();
      ArrayNode tableNames = result.putArray("table_names");
      response.getTablesList().forEach(table->{
        tableNames.add(table);
      });
    } catch (Exception e) {
      return internalServerError("Error: " + e.getMessage());
    }

    return ok(result);
  }
}
