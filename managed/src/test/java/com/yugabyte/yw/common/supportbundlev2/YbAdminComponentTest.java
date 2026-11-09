// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.supportbundlev2;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyList;
import static org.mockito.ArgumentMatchers.anyLong;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.typesafe.config.ConfigFactory;
import com.yugabyte.yw.commissioner.tasks.params.SupportBundleTaskParams;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.supportbundlev2.cgroup.SupportBundleCgroupCommandExecutor;
import com.yugabyte.yw.common.supportbundlev2.manifest.SupportBundleV2ManifestLoader;
import com.yugabyte.yw.forms.SupportBundleFormData;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.SupportBundle;
import com.yugabyte.yw.models.SupportBundle.SupportBundleStatusType;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.BundleDetails;
import com.yugabyte.yw.models.helpers.BundleDetails.YbAdminComponentSpec;
import com.yugabyte.yw.models.helpers.CloudSpecificInfo;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.modules.CustomObjectMapperModule;
import java.io.File;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.Arrays;
import java.util.Collections;
import java.util.Date;
import java.util.HashSet;
import java.util.List;
import java.util.UUID;
import java.util.concurrent.CancellationException;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class YbAdminComponentTest extends FakeDBApplication {
  @Mock public SupportBundleCgroupCommandExecutor mockCgroupCommandExecutor;
  @Mock public SupportBundleV2ManifestLoader mockManifestLoader;

  private Universe universe;
  private Customer customer;
  private final NodeDetails node = new NodeDetails();
  private final String fakeBundlePath =
      "/tmp/yugaware_tests/support_bundle-ybadmin_component/bundle";

  private SupportBundleClusterScope clusterScope;

  @Before
  public void setUp() throws Exception {
    // Bundles here are never registered, so every member runs and the loader is not read.
    clusterScope = new SupportBundleClusterScope(mockManifestLoader);
    customer = ModelFactory.testCustomer();
    universe = ModelFactory.createUniverse(customer.getId());

    node.nodeName = "u-n1";
    node.cloudInfo = new CloudSpecificInfo();
    node.cloudInfo.private_ip = "10.0.0.1";
    universe =
        Universe.saveDetails(
            universe.getUniverseUUID(),
            (u) -> {
              UniverseDefinitionTaskParams universeDetails = u.getUniverseDetails();
              universeDetails.nodeDetailsSet = new HashSet<>(Arrays.asList(node));
              u.setUniverseDetails(universeDetails);
            });

    Files.createDirectories(Paths.get(fakeBundlePath));
  }

  @Test
  public void testListAllMastersCollectsCommandOutput() throws Exception {
    YbAdminComponentSpec spec = new YbAdminComponentSpec();
    spec.setComponentName("list_all_masters");
    spec.setYbAdminCommands(Collections.singletonList("list_all_masters"));
    spec.setOutputFileName("yb_admin_masters.txt");

    SupportBundleFormData bundleData = new SupportBundleFormData();
    BundleDetails bundleDetails = new BundleDetails();
    bundleDetails.setYbAdminComponentSpecs(Collections.singletonList(spec));

    SupportBundleTaskParams taskParams = new SupportBundleTaskParams();
    taskParams.bundleData = bundleData;
    taskParams.universe = universe;
    taskParams.supportBundle =
        new SupportBundle(
            UUID.randomUUID(),
            universe.getUniverseUUID(),
            null,
            null,
            null,
            bundleDetails,
            SupportBundleStatusType.Running);

    ShellResponse response = new ShellResponse();
    response.code = 0;
    response.message =
        "-- command: list_all_masters\nhost1:7100,host2:7100,host3:7100\n(3 rows)\n\n";
    when(mockCgroupCommandExecutor.runYbAdminCommands(
            eq(taskParams.supportBundle.getBundleUUID()),
            eq(node),
            eq(universe),
            eq(Collections.singletonList("list_all_masters")),
            anyList(),
            anyLong(),
            eq("list_all_masters")))
        .thenReturn(response);

    YbAdminComponent component = new YbAdminComponent(mockCgroupCommandExecutor, clusterScope);
    Path bundlePath = Paths.get(fakeBundlePath);
    component.downloadComponentBetweenDates(
        taskParams, customer, universe, bundlePath, new Date(), new Date(), node);

    Path outputPath = bundlePath.resolve("yb_admin_masters.txt");
    assertTrue(Files.exists(outputPath));
    String output = new String(Files.readAllBytes(outputPath), StandardCharsets.UTF_8);
    assertTrue(output.contains("node=u-n1"));
    assertTrue(output.contains("-- command: list_all_masters"));
    assertTrue(output.contains("host1:7100"));
    assertEquals(0, output.indexOf("node=u-n1"));
    // The spec gives no timeout, so the query default applies.
    verify(mockCgroupCommandExecutor)
        .runYbAdminCommands(any(), any(), any(), anyList(), anyList(), eq(300L), any());
  }

  /** A cluster-scope member runs on the node the bundle chose and is skipped on the others. */
  @Test
  public void testClusterScopeMemberRunsOnlyOnTheChosenNode() throws Exception {
    when(mockManifestLoader.load())
        .thenReturn(
            new SupportBundleV2ManifestLoader(
                    ConfigFactory.parseString(
                        String.format(
                            "yb.devops.home = \"%s\"", new File("devops").getAbsolutePath())),
                    CustomObjectMapperModule.createDefaultMapper())
                .load());
    NodeDetails otherNode = new NodeDetails();
    otherNode.nodeName = "u-n0";
    YbAdminComponentSpec spec = new YbAdminComponentSpec();
    spec.setComponentName("yb_admin_commands");
    spec.setYbAdminCommands(Collections.singletonList("list_all_masters"));
    BundleDetails bundleDetails = new BundleDetails();
    bundleDetails.setYbAdminComponentSpecs(Collections.singletonList(spec));
    SupportBundleTaskParams taskParams = new SupportBundleTaskParams();
    taskParams.bundleData = new SupportBundleFormData();
    taskParams.universe = universe;
    taskParams.supportBundle =
        new SupportBundle(
            UUID.randomUUID(),
            universe.getUniverseUUID(),
            null,
            null,
            null,
            bundleDetails,
            SupportBundleStatusType.Running);
    UUID bundleUuid = taskParams.supportBundle.getBundleUUID();
    clusterScope.register(bundleUuid, List.of(node, otherNode));

    YbAdminComponent component = new YbAdminComponent(mockCgroupCommandExecutor, clusterScope);
    component.downloadComponentBetweenDates(
        taskParams, customer, universe, Paths.get(fakeBundlePath), new Date(), new Date(), node);
    verify(mockCgroupCommandExecutor, never())
        .runYbAdminCommands(any(), any(), any(), anyList(), anyList(), anyLong(), any());

    ShellResponse response = new ShellResponse();
    response.message = "";
    when(mockCgroupCommandExecutor.runYbAdminCommands(
            eq(bundleUuid), eq(otherNode), any(), anyList(), anyList(), anyLong(), any()))
        .thenReturn(response);
    component.downloadComponentBetweenDates(
        taskParams,
        customer,
        universe,
        Paths.get(fakeBundlePath),
        new Date(),
        new Date(),
        otherNode);
    verify(mockCgroupCommandExecutor)
        .runYbAdminCommands(
            eq(bundleUuid), eq(otherNode), any(), anyList(), anyList(), anyLong(), any());
  }

  @Test
  public void testMultipleCommandsContinueOnFailure() throws Exception {
    YbAdminComponentSpec spec = new YbAdminComponentSpec();
    spec.setComponentName("yb_admin_info");
    spec.setYbAdminCommands(
        Arrays.asList("list_all_masters", "invalid_command", "list_all_tablet_servers"));
    spec.setOutputFileName("yb_admin_info.txt");

    SupportBundleFormData bundleData = new SupportBundleFormData();
    BundleDetails bundleDetails = new BundleDetails();
    bundleDetails.setYbAdminComponentSpecs(Collections.singletonList(spec));

    SupportBundleTaskParams taskParams = new SupportBundleTaskParams();
    taskParams.bundleData = bundleData;
    taskParams.universe = universe;
    taskParams.supportBundle =
        new SupportBundle(
            UUID.randomUUID(),
            universe.getUniverseUUID(),
            null,
            null,
            null,
            bundleDetails,
            SupportBundleStatusType.Running);

    List<String> commands = spec.getYbAdminCommands();
    ShellResponse response = new ShellResponse();
    response.code = 0;
    response.message =
        "-- command: list_all_masters\n"
            + "host1:7100,host2:7100\n\n"
            + "-- command: invalid_command\n"
            + "Invalid command: invalid_command\n"
            + "(exit code 1)\n\n"
            + "-- command: list_all_tablet_servers\n"
            + "ts1:9100\n\n";
    when(mockCgroupCommandExecutor.runYbAdminCommands(
            any(), eq(node), eq(universe), eq(commands), anyList(), anyLong(), eq("yb_admin_info")))
        .thenReturn(response);

    YbAdminComponent component = new YbAdminComponent(mockCgroupCommandExecutor, clusterScope);
    Path bundlePath = Paths.get(fakeBundlePath);
    component.downloadComponentBetweenDates(
        taskParams, customer, universe, bundlePath, new Date(), new Date(), node);

    Path outputPath = bundlePath.resolve("yb_admin_info.txt");
    assertTrue(Files.exists(outputPath));
    String output = new String(Files.readAllBytes(outputPath), StandardCharsets.UTF_8);
    assertTrue(output.contains("-- command: list_all_masters"));
    assertTrue(output.contains("-- command: invalid_command"));
    assertTrue(output.contains("Invalid command"));
    assertTrue(output.contains("(exit code 1)"));
    assertTrue(output.contains("-- command: list_all_tablet_servers"));
    assertTrue(output.contains("ts1:9100"));
  }

  /** A task abort stops the loop at the running spec instead of moving on to the next one. */
  @Test
  public void testAbortStopsAtTheRunningSpec() throws Exception {
    List<YbAdminComponentSpec> specs = new java.util.ArrayList<>();
    for (String name : List.of("first", "second")) {
      YbAdminComponentSpec spec = new YbAdminComponentSpec();
      spec.setComponentName(name);
      spec.setYbAdminCommands(Collections.singletonList("list_all_masters"));
      spec.setOutputFileName(name + ".txt");
      specs.add(spec);
    }
    BundleDetails bundleDetails = new BundleDetails();
    bundleDetails.setYbAdminComponentSpecs(specs);
    SupportBundleTaskParams taskParams = new SupportBundleTaskParams();
    taskParams.bundleData = new SupportBundleFormData();
    taskParams.universe = universe;
    taskParams.supportBundle =
        new SupportBundle(
            UUID.randomUUID(),
            universe.getUniverseUUID(),
            null,
            null,
            null,
            bundleDetails,
            SupportBundleStatusType.Running);
    when(mockCgroupCommandExecutor.runYbAdminCommands(
            any(), any(), any(), anyList(), anyList(), anyLong(), eq("first")))
        .thenThrow(new CancellationException("aborted"));

    YbAdminComponent component = new YbAdminComponent(mockCgroupCommandExecutor, clusterScope);
    assertThrows(
        CancellationException.class,
        () ->
            component.downloadComponentBetweenDates(
                taskParams,
                customer,
                universe,
                Paths.get(fakeBundlePath),
                new Date(),
                new Date(),
                node));
    verify(mockCgroupCommandExecutor, never())
        .runYbAdminCommands(any(), any(), any(), anyList(), anyList(), anyLong(), eq("second"));
    assertTrue(Thread.interrupted());
  }
}
