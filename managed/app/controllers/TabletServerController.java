// Copyright (c) Yugabyte, Inc.

package controllers;

import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import javax.inject.*;
import org.yb.client.ListTabletServersResponse;
import services.YBClientService;
import play.libs.Json;
import play.mvc.*;

public class TabletServerController extends Controller {
    private final YBClientService ybService;

    @Inject
    public TabletServerController(YBClientService service) { this.ybService = service; }

    /**
     *
     * @return Result tablet server uuids
     */
    public Result list() {
        ObjectNode result = Json.newObject();

        try {
            ListTabletServersResponse response = ybService.getClient().listTabletServers();
            result.put("count", response.getTabletServersCount());
            ArrayNode tabletServers = result.putArray("servers");
            response.getTabletServersList().forEach(tabletServer->{
                tabletServers.add(tabletServer);
            });
        } catch (Exception e) {
            return internalServerError("Error: " + e.getMessage());
        }

        return ok(result);
    }
}
