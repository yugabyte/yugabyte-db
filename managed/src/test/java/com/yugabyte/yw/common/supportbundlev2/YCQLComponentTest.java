// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.supportbundlev2;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyList;
import static org.mockito.ArgumentMatchers.anyLong;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

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
import com.yugabyte.yw.models.helpers.BundleDetails.YCQLComponentSpec;
import com.yugabyte.yw.models.helpers.CloudSpecificInfo;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.Arrays;
import java.util.Collections;
import java.util.Date;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.concurrent.CancellationException;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class YCQLComponentTest extends FakeDBApplication {
  @Mock public SupportBundleCgroupCommandExecutor mockCgroupCommandExecutor;
  @Mock public SupportBundleV2ManifestLoader mockManifestLoader;

  private Universe universe;
  private Customer customer;
  private final NodeDetails node = new NodeDetails();
  private final String fakeBundlePath = "/tmp/yugaware_tests/support_bundle-ycql_component/bundle";

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
    node.yqlServerRpcPort = 9042;
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
  public void testListKeyspacesCollectsQueryOutput() throws Exception {
    YCQLComponentSpec spec = new YCQLComponentSpec();
    spec.setComponentName("list_keyspaces");
    spec.setQueries(
        Collections.singletonList("SELECT keyspace_name FROM system_schema.keyspaces;"));
    spec.setOutputFileName("ycql_keyspaces.txt");

    SupportBundleFormData bundleData = new SupportBundleFormData();
    BundleDetails bundleDetails = new BundleDetails();
    bundleDetails.setYcqlComponentSpecs(Collections.singletonList(spec));

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
        "-- query: SELECT keyspace_name FROM system_schema.keyspaces;\n"
            + " system  system_schema  yugabyte\n(3 rows)\n\n";
    when(mockCgroupCommandExecutor.runYcqlQueries(
            eq(taskParams.supportBundle.getBundleUUID()),
            eq(node),
            eq(universe),
            eq(null),
            eq(Collections.singletonList("SELECT keyspace_name FROM system_schema.keyspaces;")),
            anyLong(),
            eq("list_keyspaces")))
        .thenReturn(response);

    YCQLComponent component = new YCQLComponent(mockCgroupCommandExecutor, clusterScope);
    Path bundlePath = Paths.get(fakeBundlePath);
    component.downloadComponentBetweenDates(
        taskParams, customer, universe, bundlePath, new Date(), new Date(), node);

    Path outputPath = bundlePath.resolve("ycql_keyspaces.txt");
    assertTrue(Files.exists(outputPath));
    String output = new String(Files.readAllBytes(outputPath), StandardCharsets.UTF_8);
    assertTrue(output.contains("node=u-n1"));
    assertTrue(output.contains("SELECT keyspace_name FROM system_schema.keyspaces;"));
    assertTrue(output.contains("system_schema"));
    assertEquals(0, output.indexOf("node=u-n1"));
    // The spec gives no timeout, so the query default applies.
    verify(mockCgroupCommandExecutor)
        .runYcqlQueries(any(), any(), any(), any(), anyList(), eq(300L), any());
  }

  /** YBA has no current YCQL password, so with auth on nothing runs and the file says why. */
  @Test
  public void testAuthEnabledRecordsSkipWithoutRunning() throws Exception {
    universe =
        Universe.saveDetails(
            universe.getUniverseUUID(),
            u -> {
              UniverseDefinitionTaskParams details = u.getUniverseDetails();
              details.getPrimaryCluster().userIntent.enableYCQLAuth = true;
              u.setUniverseDetails(details);
            });
    YCQLComponentSpec spec = new YCQLComponentSpec();
    spec.setComponentName("ycql_queries");
    spec.setQueries(Collections.singletonList("SELECT * FROM system.local;"));
    spec.setOutputFileName("ycql_skipped.txt");

    BundleDetails bundleDetails = new BundleDetails();
    bundleDetails.setYcqlComponentSpecs(Collections.singletonList(spec));
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

    Path bundlePath = Paths.get(fakeBundlePath);
    new YCQLComponent(mockCgroupCommandExecutor, clusterScope)
        .downloadComponentBetweenDates(
            taskParams, customer, universe, bundlePath, new Date(), new Date(), node);

    verify(mockCgroupCommandExecutor, never())
        .runYcqlQueries(any(), any(), any(), any(), anyList(), anyLong(), any());
    String output =
        new String(
            Files.readAllBytes(bundlePath.resolve("ycql_skipped.txt")), StandardCharsets.UTF_8);
    assertTrue(output.contains("node=u-n1"));
    assertTrue(output.contains("skipped: " + YCQLComponent.AUTH_SKIP_REASON));
  }

  /** The estimate agrees with collection: with auth on no queries run, so nothing is counted. */
  @Test
  public void testEstimateCountsNothingWhenAuthIsOn() throws Exception {
    YCQLComponentSpec spec = new YCQLComponentSpec();
    spec.setComponentName("ycql_queries");
    spec.setQueries(Collections.singletonList("SELECT * FROM system.local;"));
    BundleDetails bundleDetails = new BundleDetails();
    bundleDetails.setYcqlComponentSpecs(Collections.singletonList(spec));
    YCQLComponent component = new YCQLComponent(mockCgroupCommandExecutor, clusterScope);

    assertEquals(
        Map.of("ycql_queries", SupportBundleV2EstimateRegistry.PLACEHOLDER_5KB),
        component.getFilesListWithSizes(
            customer, null, bundleDetails, universe, new Date(), new Date(), node));

    universe =
        Universe.saveDetails(
            universe.getUniverseUUID(),
            u -> {
              UniverseDefinitionTaskParams details = u.getUniverseDetails();
              details.getPrimaryCluster().userIntent.enableYCQLAuth = true;
              u.setUniverseDetails(details);
            });
    assertEquals(
        Map.of("ycql_queries", 0L),
        component.getFilesListWithSizes(
            customer, null, bundleDetails, universe, new Date(), new Date(), node));
  }

  /** A dedicated master VM has no tserver, so no YCQL endpoint to query. */
  @Test
  public void testMasterOnlyNodeRunsNothing() throws Exception {
    node.isTserver = false;
    YCQLComponentSpec spec = new YCQLComponentSpec();
    spec.setComponentName("ycql_queries");
    spec.setQueries(Collections.singletonList("SELECT * FROM system.local;"));
    spec.setOutputFileName("ycql_master_only.txt");
    BundleDetails bundleDetails = new BundleDetails();
    bundleDetails.setYcqlComponentSpecs(Collections.singletonList(spec));
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

    Path bundlePath = Paths.get(fakeBundlePath);
    new YCQLComponent(mockCgroupCommandExecutor, clusterScope)
        .downloadComponentBetweenDates(
            taskParams, customer, universe, bundlePath, new Date(), new Date(), node);

    verify(mockCgroupCommandExecutor, never())
        .runYcqlQueries(any(), any(), any(), any(), anyList(), anyLong(), any());
    assertFalse(Files.exists(bundlePath.resolve("ycql_master_only.txt")));
  }

  @Test
  public void testMultipleQueriesContinueOnFailure() throws Exception {
    YCQLComponentSpec spec = new YCQLComponentSpec();
    spec.setComponentName("multi_query");
    spec.setQueries(
        Arrays.asList(
            "SELECT * FROM system.local;",
            "SELECT invalid cql;",
            "SELECT keyspace_name FROM system_schema.keyspaces;"));
    spec.setOutputFileName("multi_query.txt");

    SupportBundleFormData bundleData = new SupportBundleFormData();
    BundleDetails bundleDetails = new BundleDetails();
    bundleDetails.setYcqlComponentSpecs(Collections.singletonList(spec));

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
        "-- query: SELECT * FROM system.local;\n"
            + " host\n-------\n 127.0.0.1\n(1 rows)\n\n"
            + "-- query: SELECT invalid cql;\n"
            + "<stdin>:1:SyntaxException: line 1:8 no viable alternative at input 'cql'\n"
            + "(exit code 1)\n\n"
            + "-- query: SELECT keyspace_name FROM system_schema.keyspaces;\n"
            + " system_schema\n yugabyte\n(2 rows)\n\n";
    when(mockCgroupCommandExecutor.runYcqlQueries(
            any(),
            eq(node),
            eq(universe),
            eq(null),
            eq(spec.getQueries()),
            anyLong(),
            eq("multi_query")))
        .thenReturn(response);

    YCQLComponent component = new YCQLComponent(mockCgroupCommandExecutor, clusterScope);
    Path bundlePath = Paths.get(fakeBundlePath);
    component.downloadComponentBetweenDates(
        taskParams, customer, universe, bundlePath, new Date(), new Date(), node);

    Path outputPath = bundlePath.resolve("multi_query.txt");
    assertTrue(Files.exists(outputPath));
    String output = new String(Files.readAllBytes(outputPath), StandardCharsets.UTF_8);
    assertTrue(output.contains("-- query: SELECT * FROM system.local;"));
    assertTrue(output.contains("-- query: SELECT invalid cql;"));
    assertTrue(output.contains("SyntaxException"));
    assertTrue(output.contains("(exit code 1)"));
    assertTrue(output.contains("-- query: SELECT keyspace_name FROM system_schema.keyspaces;"));
    assertTrue(output.contains("system_schema"));
  }

  /** A task abort stops the loop at the running spec instead of moving on to the next one. */
  @Test
  public void testAbortStopsAtTheRunningSpec() throws Exception {
    List<YCQLComponentSpec> specs = new java.util.ArrayList<>();
    for (String name : List.of("first", "second")) {
      YCQLComponentSpec spec = new YCQLComponentSpec();
      spec.setComponentName(name);
      spec.setQueries(Collections.singletonList("SELECT * FROM system.local;"));
      spec.setOutputFileName(name + ".txt");
      specs.add(spec);
    }
    BundleDetails bundleDetails = new BundleDetails();
    bundleDetails.setYcqlComponentSpecs(specs);
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
    when(mockCgroupCommandExecutor.runYcqlQueries(
            any(), any(), any(), any(), anyList(), anyLong(), eq("first")))
        .thenThrow(new CancellationException("aborted"));

    YCQLComponent component = new YCQLComponent(mockCgroupCommandExecutor, clusterScope);
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
        .runYcqlQueries(any(), any(), any(), any(), anyList(), anyLong(), eq("second"));
    // Restored for the task, and cleared so later tests on this thread aren't interrupted.
    assertTrue(Thread.interrupted());
  }
}
