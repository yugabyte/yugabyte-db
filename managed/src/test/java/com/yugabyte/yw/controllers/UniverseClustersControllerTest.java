/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.controllers;

import static com.yugabyte.yw.common.AssertHelper.assertBadRequest;
import static com.yugabyte.yw.common.AssertHelper.assertOk;
import static com.yugabyte.yw.common.AssertHelper.assertPlatformException;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.when;
import static play.test.Helpers.contentAsString;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.cloud.PublicCloudConstants;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.DeviceInfo;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.Collections;
import java.util.UUID;
import java.util.function.Consumer;
import junitparams.JUnitParamsRunner;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import play.libs.Json;
import play.mvc.Result;

@RunWith(JUnitParamsRunner.class)
public class UniverseClustersControllerTest extends UniverseCreateControllerTestBase {

  @Before
  public void setUpTest() {
    UUID fakeTaskUUID = FakeDBApplication.buildTaskInfo(null, TaskType.AddOnClusterCreate);
    when(mockCommissioner.submit(any(TaskType.class), any(UniverseDefinitionTaskParams.class)))
        .thenReturn(fakeTaskUUID);
  }

  @Override
  protected JsonNode getUniverseJson(Result universeCreateResponse) {
    String universeUUID =
        Json.parse(contentAsString(universeCreateResponse)).get("resourceUUID").asText();
    String url = "/api/customers/" + customer.getUuid() + "/universes/" + universeUUID;
    Result getResponse = doRequestWithAuthToken("GET", url, authToken);
    assertOk(getResponse);
    return Json.parse(contentAsString(getResponse));
  }

  @Override
  protected JsonNode getUniverseDetailsJson(Result universeCreateResponse) {
    return getUniverseJson(universeCreateResponse).get("universeDetails");
  }

  @Override
  public Result sendCreateRequest(ObjectNode bodyJson) {
    ObjectNode body = bodyJson.deepCopy();
    body.remove("nodeDetailsSet");
    body.remove("nodePrefix");
    return doRequestWithAuthTokenAndBody(
        "POST", "/api/customers/" + customer.getUuid() + "/universes/clusters", authToken, body);
  }

  @Override
  public Result sendPrimaryCreateConfigureRequest(ObjectNode body) {
    body.remove("clusterOperation");
    body.remove("currentClusterType");
    return doRequestWithAuthTokenAndBody(
        "POST", "/api/customers/" + customer.getUuid() + "/universes/clusters", authToken, body);
  }

  @Override
  public Result sendPrimaryEditConfigureRequest(ObjectNode body) {
    body.remove("clusterOperation");
    body.remove("currentClusterType");
    return doRequestWithAuthTokenAndBody(
        "PUT",
        "/api/customers/"
            + customer.getUuid()
            + "/universes/"
            + body.get("universeUUID").asText()
            + "/clusters/primary",
        authToken,
        body);
  }

  @Override
  public Result sendAsyncCreateConfigureRequest(ObjectNode body) {
    body.remove("clusterOperation");
    body.remove("currentClusterType");
    return doRequestWithAuthTokenAndBody(
        "POST",
        "/api/customers/"
            + customer.getUuid()
            + "/universes/"
            + body.get("universeUUID").asText()
            + "/clusters/read_only",
        authToken,
        body);
  }

  @Test
  public void createReadonlyClusterSuccessTest() {
    testCreateReadonlyCluster(null, null, true);
  }

  @Test
  public void createReadonlyClusterFailTest() {
    testCreateReadonlyCluster(null, intent -> intent.assignPublicIP = false, false);
  }

  @Test
  public void updatePrimaryNotConsistentFailTest() {
    Universe universe =
        ModelFactory.createFromConfig(ModelFactory.awsProvider(customer), "ahaha", "r1-az1-1-1");

    universe =
        Universe.saveDetails(
            universe.getUniverseUUID(),
            ApiUtils.mockUniverseUpdaterWithReadReplica(
                universe.getUniverseDetails().getPrimaryCluster().userIntent,
                universe.getUniverseDetails().getPrimaryCluster().placementInfo));

    UniverseDefinitionTaskParams.Cluster primaryCluster =
        universe.getUniverseDetails().getPrimaryCluster();
    primaryCluster.userIntent.enableYSQLAuth = !primaryCluster.userIntent.enableYSQLAuth;
    primaryCluster.userIntent.instanceTags = Collections.singletonMap("qq", "vv");

    ObjectNode bodyJson = Json.newObject();
    ArrayNode clustersJsonArray = Json.newArray().add(Json.toJson(primaryCluster));
    bodyJson.set("clusters", clustersJsonArray);
    bodyJson.put("universeUUID", universe.getUniverseUUID().toString());
    bodyJson.set("nodeDetailsSet", Json.toJson(universe.getUniverseDetails().nodeDetailsSet));
    Result result = assertPlatformException(() -> sendPrimaryEditConfigureRequest(bodyJson));
    assertBadRequest(
        result,
        "Ysql auth setting should be the same for primary and readonly replica true vs false");
  }

  private void testCreateReadonlyCluster(
      Consumer<UniverseDefinitionTaskParams.UserIntent> primaryMutator,
      Consumer<UniverseDefinitionTaskParams.UserIntent> readonlyMutator,
      boolean success) {
    Universe universe = ModelFactory.createUniverse(customer.getId());
    Provider p = ModelFactory.awsProvider(customer);
    Region r = Region.create(p, "region-1", "PlacementRegion 1", "default-image");
    AvailabilityZone.createOrThrow(r, "az-1", "PlacementAZ 1", "subnet-1");
    UniverseDefinitionTaskParams.Cluster primaryCluster =
        universe.getUniverseDetails().getPrimaryCluster();
    UniverseDefinitionTaskParams taskParams = new UniverseDefinitionTaskParams();
    taskParams.setUniverseUUID(universe.getUniverseUUID());
    UniverseDefinitionTaskParams.Cluster newCluster =
        new UniverseDefinitionTaskParams.Cluster(
            UniverseDefinitionTaskParams.ClusterType.ASYNC, primaryCluster.userIntent);
    newCluster.uuid = UUID.randomUUID();
    DeviceInfo deviceInfo = new DeviceInfo();
    deviceInfo.volumeSize = 10;
    deviceInfo.numVolumes = 2;
    deviceInfo.storageType = PublicCloudConstants.StorageType.GP2;
    newCluster.userIntent.deviceInfo = deviceInfo;
    newCluster.userIntent.instanceType = "c3.xlarge";
    newCluster.userIntent.regionList = Collections.singletonList(r.getUuid());

    if (primaryMutator != null) {
      Universe.saveDetails(
          universe.getUniverseUUID(),
          univ -> {
            primaryMutator.accept(univ.getUniverseDetails().getPrimaryCluster().userIntent);
          });
    }
    if (readonlyMutator != null) {
      readonlyMutator.accept(newCluster.userIntent);
    }
    ObjectNode bodyJson = Json.newObject();
    ArrayNode clustersJsonArray = Json.newArray().add(Json.toJson(newCluster));
    bodyJson.set("clusters", clustersJsonArray);
    bodyJson.set("deviceInfo", Json.toJson(deviceInfo));
    bodyJson.put("universeUUID", universe.getUniverseUUID().toString());
    if (success) {
      Result result = sendAsyncCreateConfigureRequest(bodyJson);
      assertOk(result);
    } else {
      assertPlatformException(() -> sendAsyncCreateConfigureRequest(bodyJson));
    }
  }
}
