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
import com.yugabyte.yw.models.helpers.BundleDetails.YSQLComponentSpec;
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
import java.util.UUID;
import java.util.concurrent.CancellationException;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class YSQLComponentTest extends FakeDBApplication {
  @Mock public SupportBundleCgroupCommandExecutor mockCgroupCommandExecutor;
  @Mock public SupportBundleV2ManifestLoader mockManifestLoader;

  private Universe universe;
  private Customer customer;
  private final NodeDetails node = new NodeDetails();
  private final String fakeBundlePath = "/tmp/yugaware_tests/support_bundle-ysql_component/bundle";

  private SupportBundleClusterScope clusterScope;

  @Before
  public void setUp() throws Exception {
    // Bundles here are never registered, so every member runs and the loader is not read.
    clusterScope = new SupportBundleClusterScope(mockManifestLoader);
    customer = ModelFactory.testCustomer();
    universe = ModelFactory.createUniverse(customer.getId());

    node.nodeName = "u-n1";
    node.cloudInfo = new CloudSpecificInfo();
    node.cloudInfo.private_ip = "fake_ip";
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

  /** A dedicated master VM has no tserver, so no YSQL endpoint to query. */
  @Test
  public void testMasterOnlyNodeRunsNothing() throws Exception {
    node.isTserver = false;
    YSQLComponentSpec spec = new YSQLComponentSpec();
    spec.setComponentName("ysql_yugabyte_queries");
    spec.setDbName("yugabyte");
    spec.setQueries(Collections.singletonList("SELECT 1;"));
    spec.setOutputFileName("ysql_master_only.txt");
    BundleDetails bundleDetails = new BundleDetails();
    bundleDetails.setYsqlComponentSpecs(Collections.singletonList(spec));
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
    new YSQLComponent(mockCgroupCommandExecutor, clusterScope)
        .downloadComponentBetweenDates(
            taskParams, customer, universe, bundlePath, new Date(), new Date(), node);

    verify(mockCgroupCommandExecutor, never())
        .runYsqlQueries(any(), any(), any(), any(), anyList(), anyLong(), any());
    assertFalse(Files.exists(bundlePath.resolve("ysql_master_only.txt")));
  }

  @Test
  public void testListDatabasesCollectsQueryOutput() throws Exception {
    YSQLComponentSpec spec = new YSQLComponentSpec();
    spec.setComponentName("list_databases");
    spec.setDbName("yugabyte");
    spec.setQueries(Collections.singletonList("SELECT datname FROM pg_database;"));
    spec.setOutputFileName("pg_databases.txt");

    SupportBundleFormData bundleData = new SupportBundleFormData();
    BundleDetails bundleDetails = new BundleDetails();
    bundleDetails.setYsqlComponentSpecs(Collections.singletonList(spec));

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
        "-- query: SELECT datname FROM pg_database;\n"
            + " datname \n----------\n yugabyte\n template1\n(2 rows)\n\n";
    when(mockCgroupCommandExecutor.runYsqlQueries(
            eq(taskParams.supportBundle.getBundleUUID()),
            eq(node),
            eq(universe),
            eq("yugabyte"),
            eq(Collections.singletonList("SELECT datname FROM pg_database;")),
            anyLong(),
            eq("list_databases")))
        .thenReturn(response);

    YSQLComponent component = new YSQLComponent(mockCgroupCommandExecutor, clusterScope);
    Path bundlePath = Paths.get(fakeBundlePath);
    component.downloadComponentBetweenDates(
        taskParams, customer, universe, bundlePath, new Date(), new Date(), node);

    Path outputPath = bundlePath.resolve("pg_databases.txt");
    assertTrue(Files.exists(outputPath));
    String output = new String(Files.readAllBytes(outputPath), StandardCharsets.UTF_8);
    assertTrue(output.contains("node=u-n1"));
    assertTrue(output.contains("db=yugabyte"));
    assertTrue(output.contains("SELECT datname FROM pg_database;"));
    assertTrue(output.contains("yugabyte"));
    assertEquals(0, output.indexOf("node=u-n1"));
    // The spec gives no timeout, so the query default applies.
    verify(mockCgroupCommandExecutor)
        .runYsqlQueries(any(), any(), any(), any(), any(), eq(300L), any());
  }

  /** The pooling flag is what routes the queries past Connection Manager to Postgres. */
  @Test
  public void testPassesConnectionPoolingFlag() throws Exception {
    universe =
        Universe.saveDetails(
            universe.getUniverseUUID(),
            u -> {
              UniverseDefinitionTaskParams details = u.getUniverseDetails();
              details.getPrimaryCluster().userIntent.enableConnectionPooling = true;
              u.setUniverseDetails(details);
            });
    YSQLComponentSpec spec = new YSQLComponentSpec();
    spec.setComponentName("list_databases");
    spec.setQueries(Collections.singletonList("SELECT 1;"));

    BundleDetails bundleDetails = new BundleDetails();
    bundleDetails.setYsqlComponentSpecs(Collections.singletonList(spec));
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

    ShellResponse response = new ShellResponse();
    response.message = "";
    when(mockCgroupCommandExecutor.runYsqlQueries(
            any(), any(), any(), any(), anyList(), anyLong(), any()))
        .thenReturn(response);

    new YSQLComponent(mockCgroupCommandExecutor, clusterScope)
        .downloadComponentBetweenDates(
            taskParams,
            customer,
            universe,
            Paths.get(fakeBundlePath),
            new Date(),
            new Date(),
            node);

    verify(mockCgroupCommandExecutor)
        .runYsqlQueries(any(), any(), any(), any(), anyList(), anyLong(), any());
  }

  @Test
  public void testMultipleQueriesContinueOnFailure() throws Exception {
    YSQLComponentSpec spec = new YSQLComponentSpec();
    spec.setComponentName("multi_query");
    spec.setDbName("yugabyte");
    spec.setQueries(Arrays.asList("SELECT 1;", "SELECT invalid_syntax;", "SELECT version();"));
    spec.setOutputFileName("multi_query.txt");

    SupportBundleFormData bundleData = new SupportBundleFormData();
    BundleDetails bundleDetails = new BundleDetails();
    bundleDetails.setYsqlComponentSpecs(Collections.singletonList(spec));

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
        "-- query: SELECT 1;\n"
            + " ?column? \n----------\n          1\n(1 row)\n\n"
            + "-- query: SELECT invalid_syntax;\n"
            + "ERROR:  syntax error\n"
            + "(exit code 1)\n\n"
            + "-- query: SELECT version();\n"
            + " version \n---------\n v1\n(1 row)\n\n";
    when(mockCgroupCommandExecutor.runYsqlQueries(
            any(),
            eq(node),
            eq(universe),
            eq("yugabyte"),
            eq(spec.getQueries()),
            anyLong(),
            eq("multi_query")))
        .thenReturn(response);

    YSQLComponent component = new YSQLComponent(mockCgroupCommandExecutor, clusterScope);
    Path bundlePath = Paths.get(fakeBundlePath);
    component.downloadComponentBetweenDates(
        taskParams, customer, universe, bundlePath, new Date(), new Date(), node);

    Path outputPath = bundlePath.resolve("multi_query.txt");
    assertTrue(Files.exists(outputPath));
    String output = new String(Files.readAllBytes(outputPath), StandardCharsets.UTF_8);
    assertTrue(output.contains("-- query: SELECT 1;"));
    assertTrue(output.contains("-- query: SELECT invalid_syntax;"));
    assertTrue(output.contains("ERROR:  syntax error"));
    assertTrue(output.contains("(exit code 1)"));
    assertTrue(output.contains("-- query: SELECT version();"));
    assertTrue(output.contains("v1"));
  }

  /** A task abort stops the loop at the running spec instead of moving on to the next one. */
  @Test
  public void testAbortStopsAtTheRunningSpec() throws Exception {
    List<YSQLComponentSpec> specs = new java.util.ArrayList<>();
    for (String name : List.of("first", "second")) {
      YSQLComponentSpec spec = new YSQLComponentSpec();
      spec.setComponentName(name);
      spec.setDbName("yugabyte");
      spec.setQueries(Collections.singletonList("SELECT 1;"));
      spec.setOutputFileName(name + ".txt");
      specs.add(spec);
    }
    BundleDetails bundleDetails = new BundleDetails();
    bundleDetails.setYsqlComponentSpecs(specs);
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
    when(mockCgroupCommandExecutor.runYsqlQueries(
            any(), any(), any(), any(), anyList(), anyLong(), eq("first")))
        .thenThrow(new CancellationException("aborted"));

    YSQLComponent component = new YSQLComponent(mockCgroupCommandExecutor, clusterScope);
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
        .runYsqlQueries(any(), any(), any(), any(), anyList(), anyLong(), eq("second"));
    assertTrue(Thread.interrupted());
  }
}
