// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.controllers.handlers;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;
import static play.mvc.Http.Status.BAD_REQUEST;

import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.config.impl.SettableRuntimeConfigFactory;
import com.yugabyte.yw.common.supportbundlev2.manifest.SupportBundleV2Manifest;
import com.yugabyte.yw.common.supportbundlev2.manifest.SupportBundleV2ManifestLoader;
import com.yugabyte.yw.forms.SupportBundleFormDataV2;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.BundleDetails.BashComponentSpec;
import com.yugabyte.yw.models.helpers.BundleDetails.ComponentType;
import com.yugabyte.yw.models.helpers.BundleDetails.FilesComponentSpec;
import com.yugabyte.yw.models.helpers.BundleDetails.PrometheusMetricsType;
import com.yugabyte.yw.models.helpers.BundleDetails.YCQLComponentSpec;
import com.yugabyte.yw.models.helpers.BundleDetails.YSQLComponentSpec;
import com.yugabyte.yw.models.helpers.BundleDetails.YbAdminComponentSpec;
import com.yugabyte.yw.models.helpers.BundleDetails.YbaComponentSpec;
import com.yugabyte.yw.models.helpers.ScriptTarComponentSpec;
import java.io.File;
import java.util.ArrayList;
import java.util.EnumSet;
import java.util.List;
import java.util.Map;
import java.util.stream.Collectors;
import org.junit.Before;
import org.junit.Test;
import play.Application;

/**
 * Request validation in SupportBundleHandlerV2: YBA-only bundles, the cores switch, and Kubernetes
 * universes.
 *
 * <p>yb.support_bundle.allow_cores_collection is a refusal, so it has to be enforced against
 * whatever actually drives collection. In v1 that was the component list itself. In v2 the list
 * carries group names: the manifest merge records CoreFiles as a FilesComponent and moves the work
 * item into filesComponentSpecs, and a caller sending those specs directly skips the merge
 * entirely. Either way the list no longer names CoreFiles, so a check on the list alone passes
 * while the cores are still collected.
 */
public class SupportBundleHandlerV2Test extends FakeDBApplication {

  private SupportBundleHandlerV2 handler;
  private SettableRuntimeConfigFactory runtimeConfigFactory;
  private Customer customer;
  private Universe universe;
  private Universe k8sUniverse;
  private SupportBundleV2Manifest.Spec manifest;

  /** Validation reads the shipped manifest from the devops directory, which tests leave unset. */
  @Override
  protected Application provideApplication() {
    return provideApplication(Map.of("yb.devops.home", new File("devops").getAbsolutePath()));
  }

  @Before
  public void setUp() {
    handler = app.injector().instanceOf(SupportBundleHandlerV2.class);
    manifest = app.injector().instanceOf(SupportBundleV2ManifestLoader.class).load().getSpec();
    runtimeConfigFactory = app.injector().instanceOf(SettableRuntimeConfigFactory.class);
    customer = ModelFactory.testCustomer();
    universe = ModelFactory.createUniverse("cores-test", customer.getId());
    k8sUniverse =
        ModelFactory.createUniverse("k8s-universe", customer.getId(), CloudType.kubernetes);
  }

  @Test
  public void testIsYbaComponentOnly() {
    SupportBundleFormDataV2 bundleData = new SupportBundleFormDataV2();
    bundleData.components = EnumSet.of(ComponentType.YBAComponent);
    assertTrue(bundleData.isYbaComponentOnly());

    bundleData.components = EnumSet.of(ComponentType.YBAComponent, ComponentType.FilesComponent);
    assertFalse(bundleData.isYbaComponentOnly());
  }

  private static SupportBundleFormDataV2 ybaOnlyBundleData(String entrypoint) {
    SupportBundleFormDataV2 bundleData = new SupportBundleFormDataV2();
    bundleData.components = EnumSet.of(ComponentType.YBAComponent);
    YbaComponentSpec spec = new YbaComponentSpec();
    spec.setComponentName("ApplicationLogs");
    spec.setScriptPath("bin/yba_utils.sh");
    spec.setParams(List.of(entrypoint, "--remote_tar_path", "/tmp/custom-yba-script.tar.gz"));
    spec.setRemoteTarPath("/tmp/custom-yba-script.tar.gz");
    bundleData.ybaComponentSpecs = List.of(spec);
    return bundleData;
  }

  @Test
  public void testBundleDataValidationYbaOnlyAcceptsValidRequest() {
    handler.bundleDataValidationYbaOnly(ybaOnlyBundleData("create_application_logs_bundle"));
  }

  @Test
  public void testBundleDataValidationYbaOnlyRejectsForeignEntrypoint() {
    PlatformServiceException exception =
        assertThrows(
            PlatformServiceException.class,
            () -> handler.bundleDataValidationYbaOnly(ybaOnlyBundleData("bash")));
    assertEquals(BAD_REQUEST, exception.getHttpStatus());
  }

  /** Universe bundles accept YBA-host specs too, so the same pin has to hold on that endpoint. */
  @Test
  public void testUniverseBundleRejectsYbaSpecWithForeignEntrypoint() {
    SupportBundleFormDataV2 bundleData = ybaOnlyBundleData("bash");

    PlatformServiceException exception =
        assertThrows(
            PlatformServiceException.class,
            () -> handler.bundleDataValidation(bundleData, universe));
    assertEquals(BAD_REQUEST, exception.getHttpStatus());
  }

  @Test
  public void testFilesSpecWithForeignEntrypointRejected() {
    FilesComponentSpec spec = filesSpec("UniverseLogs");
    spec.setParams(List.of("bash", "-c", "id"));
    SupportBundleFormDataV2 bundleData = new SupportBundleFormDataV2();
    bundleData.components = EnumSet.of(ComponentType.FilesComponent);
    bundleData.filesComponentSpecs = new ArrayList<>(List.of(spec));

    PlatformServiceException exception =
        assertThrows(
            PlatformServiceException.class,
            () -> handler.bundleDataValidation(bundleData, universe));
    assertEquals(BAD_REQUEST, exception.getHttpStatus());
  }

  @Test
  public void testYsqlSpecWithShellInDbNameRejected() {
    YSQLComponentSpec spec = new YSQLComponentSpec();
    spec.setComponentName("ysql_yugabyte_queries");
    spec.setDbName("yugabyte;id");
    spec.setQueries(List.of("SELECT 1;"));
    SupportBundleFormDataV2 bundleData = new SupportBundleFormDataV2();
    bundleData.components = EnumSet.of(ComponentType.YSQLComponent);
    bundleData.ysqlComponentSpecs = new ArrayList<>(List.of(spec));

    PlatformServiceException exception =
        assertThrows(
            PlatformServiceException.class,
            () -> handler.bundleDataValidation(bundleData, universe));
    assertEquals(BAD_REQUEST, exception.getHttpStatus());
  }

  @Test
  public void testYcqlSpecWithShellInKeyspaceRejected() {
    YCQLComponentSpec spec = new YCQLComponentSpec();
    spec.setComponentName("ycql_queries");
    spec.setKeyspace("system;id");
    spec.setQueries(List.of("SELECT * FROM system.local;"));
    SupportBundleFormDataV2 bundleData = new SupportBundleFormDataV2();
    bundleData.components = EnumSet.of(ComponentType.YCQLComponent);
    bundleData.ycqlComponentSpecs = new ArrayList<>(List.of(spec));

    PlatformServiceException exception =
        assertThrows(
            PlatformServiceException.class,
            () -> handler.bundleDataValidation(bundleData, universe));
    assertEquals(BAD_REQUEST, exception.getHttpStatus());
  }

  /** The collection and estimate paths no longer check for a blank script; this is the gate. */
  @Test
  public void testBlankScriptPathRejected() {
    FilesComponentSpec spec = filesSpec("UniverseLogs");
    spec.setScriptPath(" ");
    SupportBundleFormDataV2 bundleData = new SupportBundleFormDataV2();
    bundleData.components = EnumSet.of(ComponentType.FilesComponent);
    bundleData.filesComponentSpecs = new ArrayList<>(List.of(spec));
    PlatformServiceException universeBundle =
        assertThrows(
            PlatformServiceException.class,
            () -> handler.bundleDataValidation(bundleData, universe));

    SupportBundleFormDataV2 ybaOnly = ybaOnlyBundleData("create_application_logs_bundle");
    ybaOnly.ybaComponentSpecs.get(0).setScriptPath("");
    PlatformServiceException ybaBundle =
        assertThrows(
            PlatformServiceException.class, () -> handler.bundleDataValidationYbaOnly(ybaOnly));

    for (PlatformServiceException e : List.of(universeBundle, ybaBundle)) {
      assertEquals(BAD_REQUEST, e.getHttpStatus());
      assertTrue(e.getMessage(), e.getMessage().contains("requires 'scriptPath'"));
    }
  }

  @Test
  public void testBundleDataValidationYbaOnlyRejectsScriptOutsideDevopsHome() {
    SupportBundleFormDataV2 bundleData = ybaOnlyBundleData("create_application_logs_bundle");
    bundleData.ybaComponentSpecs.get(0).setScriptPath("/tmp/evil.sh");

    PlatformServiceException exception =
        assertThrows(
            PlatformServiceException.class, () -> handler.bundleDataValidationYbaOnly(bundleData));
    assertEquals(BAD_REQUEST, exception.getHttpStatus());
  }

  private void allowCores(boolean allowed) {
    runtimeConfigFactory
        .globalRuntimeConf()
        .setValue("yb.support_bundle.allow_cores_collection", String.valueOf(allowed));
  }

  /** The entrypoint is the manifest's, as the CLI sends it; validation rejects any other. */
  private FilesComponentSpec filesSpec(String componentName) {
    FilesComponentSpec spec = new FilesComponentSpec();
    spec.setComponentName(componentName);
    spec.setScriptPath("bin/support_bundle_utils.sh");
    spec.setParams(
        List.of(manifest.getComponentConfigs().get(componentName).getScript().getEntrypoint()));
    spec.setRemoteTarPath("/tmp/" + componentName + "-${nodeName}.tar.gz");
    return spec;
  }

  /**
   * What both the merged request and a direct spec request look like by the time they are
   * validated.
   */
  private SupportBundleFormDataV2 coresViaFilesSpec() {
    SupportBundleFormDataV2 bundleData = new SupportBundleFormDataV2();
    bundleData.components = EnumSet.of(ComponentType.FilesComponent);
    bundleData.filesComponentSpecs =
        new ArrayList<>(List.of(filesSpec("UniverseLogs"), filesSpec("CoreFiles")));
    return bundleData;
  }

  @Test
  public void testCoreFilesSpecRejectedWhenCollectionDisabled() {
    allowCores(false);
    SupportBundleFormDataV2 bundleData = coresViaFilesSpec();

    PlatformServiceException thrown =
        assertThrows(
            PlatformServiceException.class,
            () -> handler.bundleDataValidation(bundleData, universe));

    assertTrue(
        "unexpected message: " + thrown.getMessage(),
        thrown.getMessage().contains("Core file collection is disabled globally"));
  }

  @Test
  public void testCoreFilesSpecAllowedWhenCollectionEnabled() {
    allowCores(true);
    SupportBundleFormDataV2 bundleData = coresViaFilesSpec();

    handler.bundleDataValidation(bundleData, universe);

    assertTrue(
        bundleData.filesComponentSpecs.stream()
            .anyMatch(spec -> "CoreFiles".equals(spec.getComponentName())));
  }

  /**
   * The v1-shaped request, where CoreFiles is named in the component list, must keep being refused.
   */
  @Test
  public void testCoreFilesComponentTypeStillRejected() {
    allowCores(false);
    SupportBundleFormDataV2 bundleData = new SupportBundleFormDataV2();
    bundleData.components = EnumSet.of(ComponentType.CoreFiles);

    assertThrows(
        PlatformServiceException.class, () -> handler.bundleDataValidation(bundleData, universe));
  }

  /** The cores switch must not block anything else in the Files group. */
  @Test
  public void testOtherFilesSpecsUnaffectedWhenCollectionDisabled() {
    allowCores(false);
    SupportBundleFormDataV2 bundleData = new SupportBundleFormDataV2();
    bundleData.components = EnumSet.of(ComponentType.FilesComponent);
    bundleData.filesComponentSpecs =
        new ArrayList<>(List.of(filesSpec("UniverseLogs"), filesSpec("GFlags")));

    handler.bundleDataValidation(bundleData, universe);
  }

  private static BashComponentSpec bashSpec(String componentName) {
    BashComponentSpec spec = new BashComponentSpec();
    spec.setComponentName(componentName);
    spec.setScriptPath("bin/support_bundle_utils.sh");
    spec.setParams(List.of(componentName));
    spec.setRemoteTarPath("/tmp/" + componentName + "-${nodeName}.tar.gz");
    return spec;
  }

  private SupportBundleFormDataV2 allNodeComponents() {
    SupportBundleFormDataV2 bundleData = new SupportBundleFormDataV2();
    bundleData.components =
        EnumSet.of(
            ComponentType.FilesComponent,
            ComponentType.BashComponent,
            ComponentType.YSQLComponent,
            ComponentType.YCQLComponent,
            ComponentType.YbAdminComponent);
    bundleData.filesComponentSpecs =
        new ArrayList<>(
            List.of(
                filesSpec("SystemLogs"),
                filesSpec("UniverseLogs"),
                filesSpec("GFlags"),
                filesSpec("NodeAgent"),
                filesSpec("YbcLogs")));
    bundleData.bashComponentSpecs =
        new ArrayList<>(List.of(bashSpec("collect_heap_snapshot"), bashSpec("collect_cdc_state")));

    YSQLComponentSpec ysqlSpec = new YSQLComponentSpec();
    ysqlSpec.setComponentName("ysql_yugabyte_queries");
    ysqlSpec.setDbName("yugabyte");
    ysqlSpec.setQueries(List.of("SELECT datname FROM pg_database;"));
    bundleData.ysqlComponentSpecs = new ArrayList<>(List.of(ysqlSpec));

    YCQLComponentSpec ycqlSpec = new YCQLComponentSpec();
    ycqlSpec.setComponentName("ycql_queries");
    ycqlSpec.setQueries(List.of("SELECT keyspace_name FROM system_schema.keyspaces;"));
    bundleData.ycqlComponentSpecs = new ArrayList<>(List.of(ycqlSpec));

    YbAdminComponentSpec ybAdminSpec = new YbAdminComponentSpec();
    ybAdminSpec.setComponentName("yb_admin_commands");
    ybAdminSpec.setYbAdminCommands(List.of("list_all_masters"));
    bundleData.ybAdminComponentSpecs = new ArrayList<>(List.of(ybAdminSpec));
    return bundleData;
  }

  private static List<String> componentNames(List<? extends ScriptTarComponentSpec> specs) {
    return specs.stream()
        .map(ScriptTarComponentSpec::getComponentName)
        .collect(Collectors.toList());
  }

  @Test
  public void testNodeLevelComponentsAreAcceptedOnK8s() {
    SupportBundleFormDataV2 bundleData = allNodeComponents();

    handler.bundleDataValidation(bundleData, k8sUniverse);

    assertTrue(bundleData.components.contains(ComponentType.FilesComponent));
    assertTrue(bundleData.components.contains(ComponentType.BashComponent));
    assertTrue(bundleData.components.contains(ComponentType.YSQLComponent));
    assertTrue(bundleData.components.contains(ComponentType.YCQLComponent));
    assertTrue(bundleData.components.contains(ComponentType.YbAdminComponent));
  }

  @Test
  public void testUnsupportedFilesSpecsArePrunedOnK8s() {
    SupportBundleFormDataV2 bundleData = allNodeComponents();

    handler.bundleDataValidation(bundleData, k8sUniverse);

    assertEquals(
        List.of("UniverseLogs", "GFlags", "YbcLogs"),
        componentNames(bundleData.filesComponentSpecs));
    // Tserver-only sub-components stay in the request; the pod role is decided at collection time.
    assertEquals(
        List.of("collect_heap_snapshot", "collect_cdc_state"),
        componentNames(bundleData.bashComponentSpecs));
  }

  @Test
  public void testUnsupportedFilesSpecsAreKeptOnAws() {
    Universe awsUniverse = ModelFactory.createUniverse("aws-universe", customer.getId());
    SupportBundleFormDataV2 bundleData = allNodeComponents();

    handler.bundleDataValidation(bundleData, awsUniverse);

    assertEquals(
        List.of("SystemLogs", "UniverseLogs", "GFlags", "NodeAgent", "YbcLogs"),
        componentNames(bundleData.filesComponentSpecs));
  }

  @Test
  public void testComponentTypeIsDroppedWhenAllItsSpecsArePruned() {
    SupportBundleFormDataV2 bundleData = new SupportBundleFormDataV2();
    bundleData.components =
        EnumSet.of(ComponentType.FilesComponent, ComponentType.YbAdminComponent);
    bundleData.filesComponentSpecs =
        new ArrayList<>(List.of(filesSpec("SystemLogs"), filesSpec("NodeAgent")));
    YbAdminComponentSpec ybAdminSpec = new YbAdminComponentSpec();
    ybAdminSpec.setComponentName("yb_admin_commands");
    ybAdminSpec.setYbAdminCommands(List.of("list_all_masters"));
    bundleData.ybAdminComponentSpecs = new ArrayList<>(List.of(ybAdminSpec));

    handler.bundleDataValidation(bundleData, k8sUniverse);

    assertFalse(bundleData.components.contains(ComponentType.FilesComponent));
    assertTrue(bundleData.filesComponentSpecs.isEmpty());
    assertTrue(bundleData.components.contains(ComponentType.YbAdminComponent));
  }

  @Test
  public void testRequestPrunedToNothingIsRejected() {
    SupportBundleFormDataV2 bundleData = new SupportBundleFormDataV2();
    bundleData.components = EnumSet.of(ComponentType.FilesComponent);
    bundleData.filesComponentSpecs =
        new ArrayList<>(List.of(filesSpec("SystemLogs"), filesSpec("NodeAgent")));

    PlatformServiceException exception =
        assertThrows(
            PlatformServiceException.class,
            () -> handler.bundleDataValidation(bundleData, k8sUniverse));

    assertEquals(BAD_REQUEST, exception.getHttpStatus());
  }

  @Test
  public void testNodeExportIsPrunedFromPromDumpOnK8s() {
    SupportBundleFormDataV2 bundleData = new SupportBundleFormDataV2();
    bundleData.components = EnumSet.of(ComponentType.PrometheusMetrics);
    bundleData.prometheusMetricsTypes =
        EnumSet.of(
            PrometheusMetricsType.MASTER_EXPORT,
            PrometheusMetricsType.TSERVER_EXPORT,
            PrometheusMetricsType.NODE_EXPORT);

    handler.bundleDataValidation(bundleData, k8sUniverse);

    assertEquals(
        EnumSet.of(PrometheusMetricsType.MASTER_EXPORT, PrometheusMetricsType.TSERVER_EXPORT),
        bundleData.prometheusMetricsTypes);
    assertTrue(bundleData.components.contains(ComponentType.PrometheusMetrics));
  }

  @Test
  public void testNodeExportIsKeptOnAws() {
    Universe awsUniverse = ModelFactory.createUniverse("aws-universe", customer.getId());
    SupportBundleFormDataV2 bundleData = new SupportBundleFormDataV2();
    bundleData.components = EnumSet.of(ComponentType.PrometheusMetrics);
    bundleData.prometheusMetricsTypes =
        EnumSet.of(PrometheusMetricsType.MASTER_EXPORT, PrometheusMetricsType.NODE_EXPORT);

    handler.bundleDataValidation(bundleData, awsUniverse);

    assertTrue(bundleData.prometheusMetricsTypes.contains(PrometheusMetricsType.NODE_EXPORT));
  }
}
