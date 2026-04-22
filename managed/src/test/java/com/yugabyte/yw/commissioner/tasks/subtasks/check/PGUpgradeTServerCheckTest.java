// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks.check;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyList;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.ArgumentMatchers.nullable;
import static org.mockito.Mockito.atLeastOnce;
import static org.mockito.Mockito.lenient;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.collect.ImmutableMap;
import com.google.common.net.HostAndPort;
import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.tasks.CommissionerBaseTest;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.ReleaseContainer;
import com.yugabyte.yw.common.ReleaseManager;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.InstanceType;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Universe;
import java.util.Arrays;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.stream.Collectors;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.ArgumentCaptor;
import org.mockito.junit.MockitoJUnitRunner;
import org.yb.client.YBClient;

@RunWith(MockitoJUnitRunner.class)
public class PGUpgradeTServerCheckTest extends CommissionerBaseTest {

  private static final String CUSTOM_VM_TMP = "/custom/tmp";
  private static final String CUSTOM_K8S_TMP = "/custom/k8s/tmp";
  private static final String PG_UPGRADE_LOG = "pg_upgrade_check.log";
  private static final String SUCCESS_PG_UPGRADE_OUTPUT =
      "Performing Consistency Checks on Old Live Server\n"
          + "------------------------------------------------\n"
          + "Checking cluster versions                                   ok\n"
          + "\n"
          + "\n"
          + "Clusters are compatible";

  private YBClient mockClient;

  @Before
  public void setUp() {
    mockClient = mock(YBClient.class);
    when(mockYBClient.getUniverseClient(any())).thenReturn(mockClient);
  }

  @Test
  public void testParsePGUpgradeOutputSuccess() {
    String input =
        "Performing Consistency Checks on Old Live Server\n"
            + "------------------------------------------------\n"
            + "Checking cluster versions                                   ok\n"
            + "Checking attributes of the 'yugabyte' user                  ok\n"
            + "Checking for all 3 system databases                         ok\n"
            + "Checking database connection settings                       ok\n"
            + "\n"
            + "\n"
            + "Clusters are compatible";

    ObjectNode output = PGUpgradeTServerCheck.parsePGUpgradeOutput(input);
    assertNotNull(output);
    assertEquals("Performing Consistency Checks on Old Live Server", output.get("title").asText());
    assertEquals("Clusters are compatible", output.get("overallStatus").asText());
    assertEquals(4, output.get("checks").size());
    assertEquals("ok", output.get("checks").get(0).get("status").asText());
    assertEquals("Checking cluster versions", output.get("checks").get(0).get("name").asText());
  }

  @Test
  public void testParsePGUpgradeOutputFailure() {
    String input =
        "Performing Consistency Checks on Old Live Server\n"
            + "------------------------------------------------\n"
            + "Checking cluster versions                                   ok\n"
            + "Checking for reg* data types in user tables                 fatal\n"
            + "\n"
            + "In database: yugabyte\n"
            + "  public.tbl2.b\n"
            + "\n"
            + "Your installation contains one of the reg* data types in user tables.\n"
            + "\n"
            + "Checking installed extensions                               ok\n"
            + "\n"
            + "\n"
            + "Failure, exiting";

    ObjectNode output = PGUpgradeTServerCheck.parsePGUpgradeOutput(input);
    assertNotNull(output);
    assertEquals("Failure, exiting", output.get("overallStatus").asText());

    assertEquals(3, output.get("checks").size());

    assertEquals("ok", output.get("checks").get(0).get("status").asText());

    assertEquals("fatal", output.get("checks").get(1).get("status").asText());
    assertEquals(
        "Checking for reg* data types in user tables",
        output.get("checks").get(1).get("name").asText());
    assertNotNull(output.get("checks").get(1).get("details"));
    assertTrue(
        output.get("checks").get(1).get("details").asText().contains("In database: yugabyte"));

    assertEquals("ok", output.get("checks").get(2).get("status").asText());
    assertEquals("Checking installed extensions", output.get("checks").get(2).get("name").asText());
  }

  @Test
  public void testParsePGUpgradeOutputAllOk() {
    String input =
        "Performing Consistency Checks on Old Live Server\n"
            + "------------------------------------------------\n"
            + "Checking cluster versions                                   ok\n"
            + "Checking database connection settings                       ok\n"
            + "\n"
            + "\n"
            + "Clusters are compatible";

    ObjectNode output = PGUpgradeTServerCheck.parsePGUpgradeOutput(input);
    assertNotNull(output);
    assertEquals("Clusters are compatible", output.get("overallStatus").asText());
    assertEquals(2, output.get("checks").size());
    for (int i = 0; i < output.get("checks").size(); i++) {
      assertEquals("ok", output.get("checks").get(i).get("status").asText());
      assertNull(output.get("checks").get(i).get("details").textValue());
    }
  }

  @Test
  public void testRunCheckOnNodeUsesGflagTmpDir() {
    String version = "2025.1.0.0-b1";
    ReleaseManager.ReleaseMetadata rm =
        ReleaseManager.ReleaseMetadata.create(version)
            .withFilePath("/releases/yugabyte-" + version + "-linux-x86_64.tar.gz");
    when(mockReleaseManager.getReleaseByVersion(anyString()))
        .thenReturn(new ReleaseContainer(rm, mockCloudUtilFactory, mockConfig, mockReleasesUtils));

    when(mockNodeUniverseManager.getRemoteTmpDir(any(), any())).thenReturn(CUSTOM_VM_TMP);
    when(mockNodeUniverseManager.getYbHomeDir(any(), any())).thenReturn("/home/yugabyte");
    when(mockClient.getLeaderMasterHostAndPort()).thenReturn(HostAndPort.fromHost("10.0.0.1"));
    ShellResponse logReadResponse =
        ShellResponse.create(0, "Command output:\n" + SUCCESS_PG_UPGRADE_OUTPUT);
    ShellResponse okResponse = ShellResponse.create(0, "Command output: ok");
    when(mockNodeUniverseManager.runCommand(any(), any(), anyList(), any()))
        .thenAnswer(
            invocation -> {
              @SuppressWarnings("unchecked")
              List<String> cmd = invocation.getArgument(2);
              if (cmd != null && !cmd.isEmpty() && "cat".equals(cmd.get(0))) {
                return logReadResponse;
              }
              return okResponse;
            });

    // AWS by default.
    Universe universe = ModelFactory.createUniverse(defaultCustomer.getId());
    UserIntent userIntent = ApiUtils.getDefaultUserIntent(defaultProvider);
    universe =
        Universe.saveDetails(
            universe.getUniverseUUID(),
            ApiUtils.mockUniverseUpdater(userIntent, true /* setMasters */));

    PGUpgradeTServerCheck.Params params = new PGUpgradeTServerCheck.Params();
    params.setUniverseUUID(universe.getUniverseUUID());
    params.ybSoftwareVersion = version;

    PGUpgradeTServerCheck task = AbstractTaskBase.createTask(PGUpgradeTServerCheck.class);
    task.initialize(params);
    task.run();

    verify(mockNodeUniverseManager, atLeastOnce()).getRemoteTmpDir(any(), any());

    @SuppressWarnings("unchecked")
    ArgumentCaptor<List<String>> cmdCaptor = ArgumentCaptor.forClass(List.class);
    verify(mockNodeUniverseManager, atLeastOnce())
        .runCommand(any(), any(), cmdCaptor.capture(), any());

    String joinedCommands =
        cmdCaptor.getAllValues().stream().flatMap(List::stream).collect(Collectors.joining(" "));
    assertTrue(
        "Expected pg_upgrade log redirect to use gflag-based tmp dir",
        joinedCommands.contains(CUSTOM_VM_TMP + "/" + PG_UPGRADE_LOG));
  }

  @Test
  public void testRunCheckOnPodUsesGflagTmpDir() {
    Map<String, String> kubeConfig = new HashMap<>();
    kubeConfig.put("KUBECONFIG", "test");
    kubernetesProvider.setConfigMap(kubeConfig);
    kubernetesProvider.save();

    Region r = Region.create(kubernetesProvider, "region-1", "PlacementRegion-1", "default-image");
    AvailabilityZone.createOrThrow(r, "az-1", "PlacementAZ-1", "subnet-1");
    InstanceType i =
        InstanceType.upsert(
            kubernetesProvider.getUuid(),
            "c3.xlarge",
            10,
            5.5,
            new InstanceType.InstanceTypeDetails());
    UserIntent userIntent = ApiUtils.getTestUserIntent(r, kubernetesProvider, i, 3);
    userIntent.enableYSQL = true;
    userIntent.enableYSQLAuth = true;

    Universe universe = ModelFactory.createUniverse(defaultCustomer.getId());
    Universe.saveDetails(
        universe.getUniverseUUID(),
        ApiUtils.mockUniverseUpdaterForK8sEdit(userIntent, "demo-universe", true, false));
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    universe.updateConfig(
        ImmutableMap.of(Universe.HELM2_LEGACY, Universe.HelmLegacy.V3.toString()));
    universe.save();

    String version = "2025.1.0.0-b1";
    ReleaseManager.ReleaseMetadata rm =
        ReleaseManager.ReleaseMetadata.create(version)
            .withFilePath("/releases/yugabyte-" + version + "-linux-x86_64.tar.gz");
    when(mockReleaseManager.getReleaseByVersion(anyString()))
        .thenReturn(new ReleaseContainer(rm, mockCloudUtilFactory, mockConfig, mockReleasesUtils));

    when(mockNodeUniverseManager.getRemoteTmpDir(any(), any())).thenReturn(CUSTOM_K8S_TMP);

    lenient()
        .when(
            mockKubernetesManager.executeCommandInPodContainer(
                any(), any(), any(), any(), anyList()))
        .thenAnswer(
            invocation -> {
              @SuppressWarnings("unchecked")
              List<String> cmd = invocation.getArgument(4);
              if (cmd != null && cmd.equals(Arrays.asList("uname", "-m"))) {
                return "x86_64";
              }
              return "";
            });

    PGUpgradeTServerCheck.Params params = new PGUpgradeTServerCheck.Params();
    params.setUniverseUUID(universe.getUniverseUUID());
    params.ybSoftwareVersion = version;

    PGUpgradeTServerCheck task = AbstractTaskBase.createTask(PGUpgradeTServerCheck.class);
    task.initialize(params);
    task.run();

    verify(mockNodeUniverseManager, atLeastOnce()).getRemoteTmpDir(any(), any());

    @SuppressWarnings("unchecked")
    ArgumentCaptor<List<String>> podCmdCaptor = ArgumentCaptor.forClass(List.class);
    verify(mockKubernetesManager, atLeastOnce())
        .executeCommandInPodContainer(
            any(),
            nullable(String.class),
            nullable(String.class),
            eq("yb-tserver"),
            podCmdCaptor.capture());

    boolean foundTmpInPgUpgrade =
        podCmdCaptor.getAllValues().stream()
            .anyMatch(
                args ->
                    args.size() >= 2
                        && "/bin/bash".equals(args.get(0))
                        && args.get(1).equals("-c")
                        && args.size() > 2
                        && args.get(2).contains("pg_upgrade")
                        && args.get(2).contains(CUSTOM_K8S_TMP));
    assertTrue(
        "Expected pg_upgrade --check command to use gflag-based tmp dir", foundTmpInPgUpgrade);
  }
}
