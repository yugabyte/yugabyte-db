package com.yugabyte.yw.ui.controllers;

import java.util.ArrayList;
import java.util.List;
import java.util.UUID;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.collect.ImmutableMap;
import com.google.inject.Inject;
import com.typesafe.config.Config;
import com.typesafe.config.ConfigFactory;
import com.yugabyte.yw.api.forms.GrafanaPanelData;
import com.yugabyte.yw.api.models.Instance;
import com.yugabyte.yw.common.ApiHelper;
import com.yugabyte.yw.common.controllers.AuthenticatedController;

import play.mvc.Result;
import com.yugabyte.yw.ui.views.html.*;

public class UniversePageController extends AuthenticatedController {

  @Inject
  ApiHelper apiHelper;

  protected static final String GRAFANA_API_ENDPOINT = "/api/dashboards/db/yugabyte-cluster";
  protected static final String GRAFANA_DASHBOARD_URL = "/dashboard-solo/db/yugabyte-cluster";

  public Result index(UUID instanceUUID) {
    long toTime = System.currentTimeMillis();
    long fromTime = toTime - 60 * 60 * 1000;

    Instance instance = Instance.find.where().eq("instance_id", instanceUUID).findUnique();
    Config conf = ConfigFactory.load();
    String grafanaUrl = conf.getString("yb.grafana.url") + GRAFANA_API_ENDPOINT;
    String grafanaAccessKey = conf.getString("yb.grafana.accessKey");
    List<GrafanaPanelData> grafanaPanelDataList = new ArrayList<GrafanaPanelData>();

    if (grafanaUrl != null && grafanaAccessKey != null) {
      JsonNode grafanaData =
        apiHelper.getRequest(grafanaUrl, ImmutableMap.of("Authorization", "Bearer " + grafanaAccessKey));
      for (JsonNode panelEntries : grafanaData.findValues("panels")) {
        for (final JsonNode entry : panelEntries) {
          GrafanaPanelData data = new GrafanaPanelData();
          data.id = entry.get("id").asInt(0);
          data.title = entry.get("title").asText();
          data.url = conf.getString("yb.grafana.url")
            + GRAFANA_DASHBOARD_URL
            + "?panelId=" + data.id
            + "&from=" + fromTime
            + "&to=" + toTime
            + "&var-host=" + instance.name;
          grafanaPanelDataList.add(data);
        }
      }
    }
    return ok(showInstance.render(instance, grafanaPanelDataList));
  }
}
