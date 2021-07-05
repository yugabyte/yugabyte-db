// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.AlertDefinition;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import org.apache.commons.exec.OS;
import org.apache.commons.io.FileUtils;
import org.apache.commons.io.IOUtils;
import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.runners.MockitoJUnitRunner;
import play.Configuration;
import play.Environment;
import play.Mode;
import play.libs.Json;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileReader;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.UUID;

import static com.yugabyte.yw.common.ModelFactory.createAlertDefinition;
import static com.yugabyte.yw.common.ModelFactory.createUniverse;
import static org.hamcrest.MatcherAssert.assertThat;
import static org.hamcrest.Matchers.*;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;
import static org.mockito.Mockito.when;

@RunWith(MockitoJUnitRunner.class)
public class SwamperHelperTest extends FakeDBApplication {
  private Customer defaultCustomer;

  @Mock Configuration appConfig;

  SwamperHelper swamperHelper;

  static String SWAMPER_TMP_PATH = "/tmp/swamper/";

  @Before
  public void setUp() {
    new File(SWAMPER_TMP_PATH).mkdir();
    defaultCustomer = ModelFactory.testCustomer();

    ClassLoader classLoader = Thread.currentThread().getContextClassLoader();
    Environment env = new Environment(new File("."), classLoader, Mode.TEST);
    swamperHelper = new SwamperHelper(appConfig, env);
  }

  @After
  public void tearDown() throws IOException {
    File file = new File(SWAMPER_TMP_PATH);
    FileUtils.deleteDirectory(file);
  }

  @Test
  public void testWriteUniverseTargetJson() {
    when(appConfig.getString("yb.swamper.targetPath")).thenReturn(SWAMPER_TMP_PATH);
    Universe u = createUniverse(defaultCustomer.getCustomerId());
    u = Universe.saveDetails(u.universeUUID, ApiUtils.mockUniverseUpdaterWithInactiveNodes());
    UserIntent ui = u.getUniverseDetails().getPrimaryCluster().userIntent;
    ui.provider = Provider.get(defaultCustomer.uuid, Common.CloudType.aws).get(0).uuid.toString();
    u.getUniverseDetails().upsertPrimaryCluster(ui, null);

    try {
      swamperHelper.writeUniverseTargetJson(u.universeUUID);
      BufferedReader br =
          new BufferedReader(
              new FileReader(SWAMPER_TMP_PATH + "yugabyte." + u.universeUUID + ".json"));
      String line;
      StringBuilder sb = new StringBuilder();
      while ((line = br.readLine()) != null) {
        sb.append(line);
      }

      JsonNode targetsJson = Json.parse(sb.toString());
      assertThat(targetsJson.size(), is(equalTo(15)));
      List<String> targetTypes = new ArrayList<>();
      for (SwamperHelper.TargetType t : Arrays.asList(SwamperHelper.TargetType.values())) {
        targetTypes.add(t.toString());
      }
      for (int i = 0; i < targetsJson.size(); ++i) {
        JsonNode target = targetsJson.get(i);
        assertTrue(target.get("targets").isArray());
        assertThat(target.get("targets").size(), equalTo(1));
        assertThat(target.get("targets").toString(), RegexMatcher.matchesRegex("(.*)(|:)([0-9]*)"));
        JsonNode labels = target.get("labels");
        assertThat(labels.get("node_prefix").asText(), equalTo("host"));
        assertTrue(targetTypes.contains(labels.get("export_type").asText().toUpperCase()));
      }
    } catch (Exception e) {
      assertTrue(false);
    }
  }

  @Test
  public void testRemoveUniverseTargetJson() {
    when(appConfig.getString("yb.swamper.targetPath")).thenReturn(SWAMPER_TMP_PATH);
    UUID universeUUID = UUID.randomUUID();
    String yugabyteFilePath = SWAMPER_TMP_PATH + "yugabyte." + universeUUID + ".json";
    String nodeFilePath = SWAMPER_TMP_PATH + "node." + universeUUID + ".json";
    try {
      new File(yugabyteFilePath).createNewFile();
      new File(nodeFilePath).createNewFile();
    } catch (IOException e) {
      assertTrue(false);
    }
    swamperHelper.removeUniverseTargetJson(universeUUID);
    assertFalse(new File(yugabyteFilePath).exists());
    assertFalse(new File(nodeFilePath).exists());
  }

  @Test(expected = RuntimeException.class)
  public void testUniverseTargetWriteFailure() {
    String swamperFilePath = "/sys";
    if (OS.isFamilyMac()) {
      swamperFilePath = "/System";
    }
    when(appConfig.getString("yb.swamper.targetPath")).thenReturn(swamperFilePath);
    Universe u = createUniverse();
    u = Universe.saveDetails(u.universeUUID, ApiUtils.mockUniverseUpdater());
    swamperHelper.writeUniverseTargetJson(u.universeUUID);
  }

  public void testUniverseTargetWithoutTargetPath() {
    when(appConfig.getString("yb.swamper.targetPath")).thenReturn("");
    Universe u = createUniverse();
    swamperHelper.writeUniverseTargetJson(u.universeUUID);
  }

  @Test
  public void testWriteAlertDefinition() throws IOException {
    when(appConfig.getString("yb.swamper.rulesPath")).thenReturn(SWAMPER_TMP_PATH);
    Universe universe = createUniverse(defaultCustomer.getCustomerId());
    AlertDefinition definition = createAlertDefinition(defaultCustomer, universe);

    swamperHelper.writeAlertDefinition(definition);
    BufferedReader br =
        new BufferedReader(new FileReader(generateRulesFileName(definition.getUuid().toString())));

    String fileContent = IOUtils.toString(br);

    String expectedContent =
        IOUtils.toString(
            getClass().getClassLoader().getResourceAsStream("alert/test_alert_definition.yml"),
            StandardCharsets.UTF_8);
    expectedContent = expectedContent.replace("<definition_uuid>", definition.getUuid().toString());
    expectedContent =
        expectedContent.replace("<customer_uuid>", defaultCustomer.getUuid().toString());
    expectedContent = expectedContent.replace("<universe_uuid>", universe.universeUUID.toString());

    assertThat(fileContent, equalTo(expectedContent));
  }

  @Test
  public void testRemoveAlertDefinition() throws IOException {
    when(appConfig.getString("yb.swamper.rulesPath")).thenReturn(SWAMPER_TMP_PATH);
    UUID definitionUuid = UUID.randomUUID();
    String configFilePath = generateRulesFileName(definitionUuid.toString());

    new File(configFilePath).createNewFile();

    swamperHelper.removeAlertDefinition(definitionUuid);
    assertFalse(new File(configFilePath).exists());
  }

  @Test
  public void testGetAlertDefinitionConfigUuids() throws IOException {
    when(appConfig.getString("yb.swamper.rulesPath")).thenReturn(SWAMPER_TMP_PATH);
    UUID definitionUuid = UUID.randomUUID();
    UUID definition2Uuid = UUID.randomUUID();
    String configFilePath = generateRulesFileName(definitionUuid.toString());
    String configFilePath2 = generateRulesFileName(definition2Uuid.toString());
    String wrongFilePath = generateRulesFileName("blablabla");

    new File(configFilePath).createNewFile();
    new File(configFilePath2).createNewFile();
    new File(wrongFilePath).createNewFile();

    List<UUID> configUuids = swamperHelper.getAlertDefinitionConfigUuids();
    assertThat(configUuids, containsInAnyOrder(definitionUuid, definition2Uuid));
  }

  private String generateRulesFileName(String definitionUuid) {
    return SWAMPER_TMP_PATH + SwamperHelper.ALERT_CONFIG_FILE_PREFIX + definitionUuid + ".yml";
  }
}
