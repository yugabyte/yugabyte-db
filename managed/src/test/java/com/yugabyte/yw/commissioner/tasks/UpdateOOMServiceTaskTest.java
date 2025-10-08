// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks;

import static com.yugabyte.yw.common.ModelFactory.createUniverse;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyBoolean;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;
import static play.test.Helpers.contentAsString;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.collect.ImmutableList;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.FakeApiHelper;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.forms.AdditionalServicesStateData;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.NodeAgent;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.RuntimeConfigEntry;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.helpers.CommonUtils;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import com.yugabyte.yw.nodeagent.ConfigureServiceInput;
import com.yugabyte.yw.nodeagent.ConfigureServiceOutput;
import com.yugabyte.yw.nodeagent.EarlyoomConfig;
import com.yugabyte.yw.nodeagent.Service;
import com.yugabyte.yw.nodeagent.ServiceConfig;
import java.util.HashMap;
import java.util.Map;
import java.util.Optional;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mockito;
import org.mockito.junit.MockitoJUnitRunner;
import play.libs.Json;
import play.mvc.Result;

@RunWith(MockitoJUnitRunner.class)
public class UpdateOOMServiceTaskTest extends CommissionerBaseTest {

  private Users user;
  private Universe defaultUniverse;
  private Map<String, NodeAgent> agentMap = new HashMap<>();

  private Result runOperationInUniverse(UUID universeUUID, AdditionalServicesStateData formData) {
    return FakeApiHelper.doRequestWithAuthTokenAndBody(
        app,
        "PUT",
        "/api/customers/"
            + defaultCustomer.getUuid()
            + "/universes/"
            + universeUUID
            + "/update_additional_services",
        user.createAuthToken(),
        Json.toJson(formData));
  }

  @Before
  public void setUp() {
    user = ModelFactory.testUser(defaultCustomer);

    Region region = Region.create(defaultProvider, "region-1", "Region 1", "yb-image-1");
    AvailabilityZone az = AvailabilityZone.createOrThrow(region, "az-1", "AZ 1", "subnet-1");
    // Create default universe.
    UniverseDefinitionTaskParams.UserIntent userIntent =
        new UniverseDefinitionTaskParams.UserIntent();
    userIntent.provider = defaultProvider.getUuid().toString();
    userIntent.numNodes = 3;
    userIntent.ybSoftwareVersion = "yb-version";
    userIntent.accessKeyCode = "demo-access";
    userIntent.regionList = ImmutableList.of(region.getUuid());
    defaultUniverse = createUniverse(defaultCustomer.getId());
    PlacementInfo placementInfo = new PlacementInfo();
    PlacementInfoUtil.addPlacementZone(az.getUuid(), placementInfo, 3, 3, false);
    defaultUniverse =
        Universe.saveDetails(
            defaultUniverse.getUniverseUUID(),
            ApiUtils.mockUniverseUpdater(userIntent, placementInfo, true));
    for (NodeDetails nodeDetails : defaultUniverse.getUniverseDetails().nodeDetailsSet) {
      NodeAgent nodeAgent = new NodeAgent();
      nodeAgent.setIp(nodeDetails.cloudInfo.private_ip);
      nodeAgent.setName("nodeAgent");
      nodeAgent.setHome("/home/yugabyte");
      nodeAgent.setUuid(UUID.randomUUID());
      agentMap.put(nodeDetails.cloudInfo.private_ip, nodeAgent);
    }

    when(mockNodeUniverseManager.maybeGetNodeAgent(any(), any(), anyBoolean()))
        .then(
            invocation -> {
              NodeDetails node = invocation.getArgument(1);
              return Optional.of(agentMap.get(node.cloudInfo.private_ip));
            });

    when(mockNodeAgentClient.runConfigureEarlyoom(any(), any(), any()))
        .thenReturn(ConfigureServiceOutput.getDefaultInstance());
  }

  @Test
  public void testTurnOnForDisabled() throws InterruptedException {
    AdditionalServicesStateData data = new AdditionalServicesStateData();
    data.setEarlyoomEnabled(true);
    PlatformServiceException thrown =
        assertThrows(
            PlatformServiceException.class,
            () -> runOperationInUniverse(defaultUniverse.getUniverseUUID(), data));
  }

  @Test
  public void testTurnOnNoConfig() throws InterruptedException {
    RuntimeConfigEntry.upsertGlobal("yb.ui.feature_flags.enable_earlyoom", "true");
    String earlyoomArgs = "-M 1024 -m 50";
    RuntimeConfigEntry.upsertGlobal("yb.node_agent.earlyoom_default_args", earlyoomArgs);
    AdditionalServicesStateData data = new AdditionalServicesStateData();
    data.setEarlyoomEnabled(true);
    Result result = runOperationInUniverse(defaultUniverse.getUniverseUUID(), data);
    JsonNode json = Json.parse(contentAsString(result));
    TaskInfo taskInfo = waitForTask(UUID.fromString(json.get("taskUUID").asText()));
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    ConfigureServiceInput input =
        ConfigureServiceInput.newBuilder()
            .setService(Service.EARLYOOM)
            .setEnabled(true)
            .setConfig(
                ServiceConfig.newBuilder()
                    .setYbHomeDir(CommonUtils.DEFAULT_YB_HOME_DIR)
                    .setEarlyoomConfig(EarlyoomConfig.newBuilder().setStartArgs(earlyoomArgs))
                    .build())
            .build();
    verifyRpcs(input);

    defaultUniverse = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    assertTrue(
        defaultUniverse.getUniverseDetails().additionalServicesStateData.isEarlyoomEnabled());
    assertEquals(
        AdditionalServicesStateData.fromArgs(earlyoomArgs, true),
        defaultUniverse.getUniverseDetails().additionalServicesStateData.getEarlyoomConfig());
  }

  @Test
  public void testTurnOnWithConfig() throws InterruptedException {
    RuntimeConfigEntry.upsertGlobal("yb.ui.feature_flags.enable_earlyoom", "true");
    AdditionalServicesStateData.EarlyoomConfig earlyoomConfig =
        new AdditionalServicesStateData.EarlyoomConfig();
    earlyoomConfig.setAvailMemoryTermKb(2024);
    earlyoomConfig.setAvailMemoryTermPercent(15);
    AdditionalServicesStateData data = new AdditionalServicesStateData();
    data.setEarlyoomEnabled(true);
    data.setEarlyoomConfig(earlyoomConfig);
    Result result = runOperationInUniverse(defaultUniverse.getUniverseUUID(), data);
    JsonNode json = Json.parse(contentAsString(result));
    TaskInfo taskInfo = waitForTask(UUID.fromString(json.get("taskUUID").asText()));
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    String args = AdditionalServicesStateData.toArgs(earlyoomConfig);
    ConfigureServiceInput input =
        ConfigureServiceInput.newBuilder()
            .setService(Service.EARLYOOM)
            .setEnabled(true)
            .setConfig(
                ServiceConfig.newBuilder()
                    .setYbHomeDir(CommonUtils.DEFAULT_YB_HOME_DIR)
                    .setEarlyoomConfig(EarlyoomConfig.newBuilder().setStartArgs(args))
                    .build())
            .build();
    verifyRpcs(input);

    defaultUniverse = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    assertTrue(
        defaultUniverse.getUniverseDetails().additionalServicesStateData.isEarlyoomEnabled());
    assertEquals(
        earlyoomConfig,
        defaultUniverse.getUniverseDetails().additionalServicesStateData.getEarlyoomConfig());
  }

  @Test
  public void testTurnOffNoConfig() throws InterruptedException {
    RuntimeConfigEntry.upsertGlobal("yb.ui.feature_flags.enable_earlyoom", "true");
    AdditionalServicesStateData.EarlyoomConfig earlyoomConfig =
        new AdditionalServicesStateData.EarlyoomConfig();
    earlyoomConfig.setAvailMemoryTermKb(8024);
    earlyoomConfig.setAvailMemoryTermPercent(18);
    AdditionalServicesStateData data = new AdditionalServicesStateData();
    data.setEarlyoomEnabled(true);
    data.setEarlyoomConfig(earlyoomConfig);
    defaultUniverse =
        Universe.saveDetails(
            defaultUniverse.getUniverseUUID(),
            u -> {
              u.getUniverseDetails().additionalServicesStateData = data;
            });
    AdditionalServicesStateData request = new AdditionalServicesStateData();
    request.setEarlyoomEnabled(false);
    Result result = runOperationInUniverse(defaultUniverse.getUniverseUUID(), request);
    JsonNode json = Json.parse(contentAsString(result));
    TaskInfo taskInfo = waitForTask(UUID.fromString(json.get("taskUUID").asText()));
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    String args = AdditionalServicesStateData.toArgs(earlyoomConfig);
    ConfigureServiceInput input =
        ConfigureServiceInput.newBuilder()
            .setService(Service.EARLYOOM)
            .setEnabled(false)
            .setConfig(
                ServiceConfig.newBuilder()
                    .setYbHomeDir(CommonUtils.DEFAULT_YB_HOME_DIR)
                    .setEarlyoomConfig(EarlyoomConfig.newBuilder().setStartArgs(args))
                    .build())
            .build();
    verifyRpcs(input);

    defaultUniverse = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    assertFalse(
        defaultUniverse.getUniverseDetails().additionalServicesStateData.isEarlyoomEnabled());
    assertEquals(
        earlyoomConfig,
        defaultUniverse.getUniverseDetails().additionalServicesStateData.getEarlyoomConfig());
  }

  @Test
  public void testArgsConversion() {
    AdditionalServicesStateData.EarlyoomConfig config =
        new AdditionalServicesStateData.EarlyoomConfig();
    config.setAvailMemoryTermPercent(10);
    config.setAvailMemoryTermKb(1024);
    config.setPreferPattern("postgres");
    config.setReportInterval(1000);
    String args = AdditionalServicesStateData.toArgs(config);
    assertEquals("-M 1024 -m 10 --prefer 'postgres' -r 1000", args);
    assertEquals(config, AdditionalServicesStateData.fromArgs(args, true));

    config.setAvailMemoryKillPercent(15);
    config.setAvailMemoryKillKb(2048);
    config.setPreferPattern("postgres");
    config.setReportInterval(1000);
    args = AdditionalServicesStateData.toArgs(config);
    assertEquals("-M 1024,2048 -m 10,15 --prefer 'postgres' -r 1000", args);
    assertEquals(config, AdditionalServicesStateData.fromArgs(args, true));

    String incorrect = "-X 10 -M -m 10 -r 10a";
    AdditionalServicesStateData.EarlyoomConfig config2 =
        AdditionalServicesStateData.fromArgs(incorrect, true);
    assertNull(config2.getAvailMemoryTermKb());
    assertNull(config2.getAvailMemoryKillKb());
    assertEquals(10, (int) config2.getAvailMemoryTermPercent());
    assertNull(config2.getAvailMemoryKillPercent());
    assertNull(config2.getReportInterval());
    assertThrows(
        IllegalArgumentException.class,
        () -> AdditionalServicesStateData.fromArgs(incorrect, false));
  }

  private void verifyRpcs(ConfigureServiceInput input) {
    for (NodeAgent agent : agentMap.values()) {
      verify(mockNodeAgentClient)
          .runConfigureEarlyoom(Mockito.eq(agent), Mockito.eq(input), Mockito.eq("yugabyte"));
    }
  }
}
