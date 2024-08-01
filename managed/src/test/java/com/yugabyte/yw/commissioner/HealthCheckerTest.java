// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner;

import static com.yugabyte.yw.common.metrics.MetricService.buildMetricTemplate;
import static org.hamcrest.MatcherAssert.assertThat;
import static org.hamcrest.Matchers.contains;
import static org.hamcrest.Matchers.equalTo;
import static org.hamcrest.Matchers.hasSize;
import static org.hamcrest.Matchers.notNullValue;
import static org.hamcrest.Matchers.nullValue;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.*;
import static org.mockito.Mockito.doAnswer;
import static org.mockito.Mockito.doThrow;
import static org.mockito.Mockito.spy;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.collect.ImmutableList;
import com.typesafe.config.Config;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.AssertHelper;
import com.yugabyte.yw.common.EmailFixtures;
import com.yugabyte.yw.common.EmailHelper;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.NodeUniverseManager;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.common.PlatformScheduler;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.alerts.MaintenanceService;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.common.config.impl.RuntimeConfig;
import com.yugabyte.yw.common.metrics.MetricService;
import com.yugabyte.yw.forms.AlertingData;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.models.AccessKey;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.HealthCheck;
import com.yugabyte.yw.models.HealthCheck.Details.NodeData;
import com.yugabyte.yw.models.Metric;
import com.yugabyte.yw.models.MetricKey;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.configs.CustomerConfig;
import com.yugabyte.yw.models.helpers.CloudSpecificInfo;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import com.yugabyte.yw.models.helpers.PlatformMetrics;
import com.yugabyte.yw.models.helpers.TaskType;
import io.ebean.Model;
import jakarta.mail.MessagingException;
import java.nio.file.Files;
import java.nio.file.Paths;
import java.util.*;
import java.util.concurrent.ExecutorService;
import junitparams.JUnitParamsRunner;
import junitparams.Parameters;
import org.apache.commons.lang3.StringUtils;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.ArgumentCaptor;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnit;
import org.mockito.junit.MockitoRule;
import play.Environment;
import play.libs.Json;

@RunWith(JUnitParamsRunner.class)
public class HealthCheckerTest extends FakeDBApplication {

  @Rule public MockitoRule rule = MockitoJUnit.rule();

  private static final String YB_ALERT_TEST_EMAIL = "test@yugabyte.com";
  private static final String dummyCheck = "check";

  private HealthChecker healthChecker;

  @Mock private Config mockConfig;
  @Mock private PlatformScheduler mockPlatformScheduler;
  @Mock private ExecutorService executorService;

  private Customer defaultCustomer;
  private Provider defaultProvider;
  private Provider kubernetesProvider;

  private Universe universe;
  private AccessKey accessKey;
  private CustomerConfig customerConfig;

  private HealthCheckerReport report;

  @Mock private EmailHelper mockEmailHelper;

  private MetricService metricService;

  @Mock Config mockRuntimeConfig;

  @Mock RuntimeConfigFactory mockruntimeConfigFactory;
  @Mock RuntimeConfGetter mockConfGetter;
  @Mock Config mockConfigUniverseScope;
  @Mock private NodeUniverseManager mockNodeUniverseManager;
  @Mock private MaintenanceService mockMaintenanceService;

  @Before
  public void setUp() {
    defaultCustomer = ModelFactory.testCustomer();
    defaultProvider = ModelFactory.awsProvider(defaultCustomer);
    kubernetesProvider = ModelFactory.kubernetesProvider(defaultCustomer);

    report = spy(new HealthCheckerReport());

    when(mockRuntimeConfig.getInt("yb.health.max_num_parallel_checks")).thenReturn(11);
    when(mockruntimeConfigFactory.forUniverse(any())).thenReturn(mockConfigUniverseScope);
    when(mockConfigUniverseScope.getInt("yb.health.max_num_parallel_node_checks")).thenReturn(10);
    when(mockConfGetter.getConfForScope(any(Universe.class), eq(UniverseConfKeys.dbReadWriteTest)))
        .thenReturn(true);
    when(mockConfGetter.getConfForScope(
            any(Universe.class), eq(UniverseConfKeys.ysqlshConnectivityTest)))
        .thenReturn(true);
    when(mockConfGetter.getConfForScope(
            any(Universe.class), eq(UniverseConfKeys.cqlshConnectivityTest)))
        .thenReturn(true);
    when(mockConfGetter.getConfForScope(any(Universe.class), eq(UniverseConfKeys.healthLogOutput)))
        .thenReturn(false);
    when(mockConfGetter.getConfForScope(any(Universe.class), eq(UniverseConfKeys.enableTriggerAPI)))
        .thenReturn(false);
    when(mockConfGetter.getConfForScope(
            any(Universe.class), eq(UniverseConfKeys.healthCheckTimeDrift)))
        .thenReturn(false);
    when(mockConfGetter.getConfForScope(
            any(Universe.class), eq(UniverseConfKeys.nodeCheckTimeoutSec)))
        .thenReturn(1);
    when(mockConfGetter.getConfForScope(
            any(Universe.class), eq(UniverseConfKeys.ddlAtomicityIntervalSec)))
        .thenReturn(3600);
    when(mockConfGetter.getGlobalConf(eq(GlobalConfKeys.backwardCompatibleDate))).thenReturn(false);
    when(mockFileHelperService.createTempFile(anyString(), anyString()))
        .thenAnswer(
            i -> {
              String fileName = i.getArgument(0);
              String fileExtension = i.getArgument(1);
              return Files.createTempFile(Paths.get("/tmp"), fileName, fileExtension);
            });
    doAnswer(
            i -> {
              Runnable runnable = i.getArgument(0);
              runnable.run();
              return null;
            })
        .when(executorService)
        .execute(any(Runnable.class));

    metricService = app.injector().instanceOf(MetricService.class);

    // Finally setup the mocked instance.
    healthChecker =
        new HealthChecker(
            app.injector().instanceOf(Environment.class),
            mockConfig,
            mockPlatformScheduler,
            report,
            mockEmailHelper,
            metricService,
            mockruntimeConfigFactory,
            mockConfGetter,
            null,
            mockNodeUniverseManager,
            executorService,
            executorService,
            mockFileHelperService,
            mockMaintenanceService) {
          @Override
          RuntimeConfig<Model> getRuntimeConfig() {
            return new RuntimeConfig<>(mockRuntimeConfig);
          }
        };
    healthChecker.initialize();
  }

  private Universe setupUniverse(String name) {
    AccessKey.KeyInfo keyInfo = new AccessKey.KeyInfo();
    keyInfo.sshPort = 3333;
    accessKey = AccessKey.create(defaultProvider.getUuid(), "key-" + name, keyInfo);

    universe = ModelFactory.createUniverse(name, defaultCustomer.getId());
    // Universe modifies customer, so we need to refresh our in-memory view of this reference.
    defaultCustomer = Customer.get(defaultCustomer.getUuid());

    UniverseDefinitionTaskParams.UserIntent userIntent =
        universe.getUniverseDetails().getPrimaryCluster().userIntent;
    userIntent.accessKeyCode = accessKey.getKeyCode();
    userIntent.numNodes = 3;
    return Universe.saveDetails(
        universe.getUniverseUUID(), ApiUtils.mockUniverseUpdater(userIntent));
  }

  private Universe setupK8sUniverse(String name) {
    Region r = Region.create(kubernetesProvider, "region-1", "PlacementRegion-1", "default-image");
    AvailabilityZone az = AvailabilityZone.createOrThrow(r, "az-1", "PlacementAZ-1", "subnet-1");
    PlacementInfo pi = new PlacementInfo();
    PlacementInfoUtil.addPlacementZone(az.getUuid(), pi);
    Map<String, String> config = new HashMap<>();
    config.put("KUBECONFIG", "foo");
    kubernetesProvider.setConfigMap(config);
    kubernetesProvider.save();
    // Universe modifies customer, so we need to refresh our in-memory view of this reference.
    defaultCustomer = Customer.get(defaultCustomer.getUuid());
    universe =
        ModelFactory.createUniverse(
            name, UUID.randomUUID(), defaultCustomer.getId(), Common.CloudType.kubernetes, pi);
    return Universe.saveDetails(
        universe.getUniverseUUID(), ApiUtils.mockUniverseUpdaterWithActiveYSQLNode(az.getUuid()));
  }

  private AlertingData setupAlertingData(
      String alertingEmail, boolean sendAlertsToYb, boolean reportOnlyErrors) {

    AlertingData data = new AlertingData();
    data.sendAlertsToYb = sendAlertsToYb;
    data.alertingEmail = alertingEmail;
    data.reportOnlyErrors = reportOnlyErrors;

    if (null == customerConfig) {
      // Setup alerting data.
      customerConfig =
          CustomerConfig.createAlertConfig(defaultCustomer.getUuid(), Json.toJson(data));
      customerConfig.save();
    } else {
      customerConfig.setData((ObjectNode) Json.toJson(data));
      customerConfig.update();
    }
    return data;
  }

  private Universe setupDisabledAlertsConfig(String email, long disabledUntilSecs) {
    Universe u = setupUniverse("univ1");
    setupAlertingData(email, false, false);
    Map<String, String> config = new HashMap<>();
    config.put(Universe.DISABLE_ALERTS_UNTIL, Long.toString(disabledUntilSecs));
    u.updateConfig(config);
    u.save();
    return u;
  }

  private void verifyNodeUniverseManager(int uploads, int commands) {
    verify(mockNodeUniverseManager, times(uploads))
        .uploadFileToNode(any(), any(), any(), any(), any(), any());
    verify(mockNodeUniverseManager, times(commands)).runCommand(any(), any(), anyList(), any());
  }

  private void verifyK8sHealthManager() {
    ArgumentCaptor<List<String>> command = ArgumentCaptor.forClass(List.class);
    verify(mockNodeUniverseManager, times(1)).runCommand(any(), any(), command.capture(), any());
    // TODO assert
  }

  private void testSingleUniverse(
      Universe u, String expectedEmail, boolean shouldFail, int invocationsCount) {
    testSingleUniverse(u, expectedEmail, shouldFail, invocationsCount, u.getNodes().size() * 2);
  }

  private void testSingleUniverse(
      Universe u, String expectedEmail, boolean shouldFail, int invocationsCount, int uploadCount) {
    setupAlertingData(expectedEmail, false, false);
    healthChecker.checkSingleUniverse(
        new HealthChecker.CheckSingleUniverseParams(
            u, defaultCustomer, true, false, false, expectedEmail));
    verifyNodeUniverseManager(uploadCount, invocationsCount * u.getNodes().size());

    u.getNodes()
        .forEach(
            node -> {
              assertCheckStatusMetricValue(node.cloudInfo.private_ip, "Node", !shouldFail);
              if (!shouldFail) {
                assertCheckStatusMetricValue(node.cloudInfo.private_ip, dummyCheck, false);
              } else {
                assertCheckStatusMetricValue(node.cloudInfo.private_ip, dummyCheck, null);
              }
            });
  }

  private void assertCheckStatusMetricValue(String node, String checkName, Boolean value) {
    Metric checkStatusMetric =
        metricService.get(
            MetricKey.builder()
                .customerUuid(defaultCustomer.getUuid())
                .name(PlatformMetrics.YB_UNIV_HEALTH_STATUS.getMetricName())
                .sourceUuid(universe.getUniverseUUID())
                .sourceLabel(HealthCheckMetrics.kNodeLabel, node)
                .sourceLabel(HealthCheckMetrics.kCheckLabel, checkName)
                .build());

    if (value == null) {
      assertThat(checkStatusMetric, nullValue());
    } else {
      Double val = checkStatusMetric.getValue();
      assertEquals(val.intValue(), value ? 0 : 1);
    }
  }

  private void testSingleK8sUniverse(Universe u) {
    healthChecker.checkSingleUniverse(
        new HealthChecker.CheckSingleUniverseParams(u, defaultCustomer, true, false, false, null));
    verifyK8sHealthManager();
  }

  private void validateNoDevopsCall() {
    healthChecker.checkCustomer(defaultCustomer);
    verifyNodeUniverseManager(0, 0);
  }

  @Test
  public void testCheckSingleUniverse_NoEmail() {
    mockBadHealthResponse();
    Universe u = setupUniverse("univ1");
    setupAlertingData(null, false, false);
    testSingleUniverse(u, null, false, 1);
  }

  @Test
  public void testCheckStatusMetricsCleaned() {
    mockBadHealthResponse();
    Universe u = setupUniverse("univ1");
    setupAlertingData(null, false, false);
    testSingleUniverse(u, null, false, 1);

    ShellResponse dummyShellResponseFail = ShellResponse.create(1, "Should error");
    doAnswer(i -> dummyShellResponseFail)
        .when(mockNodeUniverseManager)
        .runCommand(any(), any(), anyList(), any());

    testSingleUniverse(u, null, true, 2);
  }

  @Test
  public void testCheckSingleUniverse_K8s_NoEmail() {
    Universe u = setupK8sUniverse("univ1");
    setupAlertingData(null, false, false);
    testSingleK8sUniverse(u);
  }

  @Test
  public void testCheckSingleUniverse_YbEmail() {
    mockBadHealthResponse();
    Universe u = setupUniverse("univ1");
    setupAlertingData(null, true, false);
    testSingleUniverse(u, YB_ALERT_TEST_EMAIL, false, 1);
  }

  @Test
  public void testCheckSingleUniverse_ReportOnlyErrors() {
    Universe u = setupUniverse("univ1");

    // enable report only errors
    setupAlertingData(null, true, true);
    healthChecker.checkSingleUniverse(
        new HealthChecker.CheckSingleUniverseParams(
            u, defaultCustomer, false, true, false, YB_ALERT_TEST_EMAIL));
    verifyNodeUniverseManager(6, 3);

    // Erase stored into DB data to avoid DuplicateKeyException.
    HealthCheck.keepOnlyLast(u.getUniverseUUID(), 0);

    // disable report only errors
    setupAlertingData(null, true, false);
    healthChecker.checkSingleUniverse(
        new HealthChecker.CheckSingleUniverseParams(
            u, defaultCustomer, false, false, false, YB_ALERT_TEST_EMAIL));
    verifyNodeUniverseManager(6, 6);
  }

  @Test
  public void testCheckSingleUniverse_CustomEmail() {
    mockBadHealthResponse();
    Universe u = setupUniverse("univ1");
    String email = "foo@yugabyte.com";
    setupAlertingData(email, false, false);
    testSingleUniverse(u, email, false, 1);
  }

  @Test
  public void testCheckSingleUniverse_DisabledAlerts1() {
    mockBadHealthResponse();
    String email = "foo@yugabyte.com";
    Universe u = setupDisabledAlertsConfig(email, 0);
    testSingleUniverse(u, email, false, 1);
  }

  @Test
  public void testCheckSingleUniverse_DisabledAlerts2() {
    mockBadHealthResponse();
    String email = "foo@yugabyte.com";
    Universe u = setupDisabledAlertsConfig(email, Long.MAX_VALUE);
    testSingleUniverse(u, null, false, 1);
  }

  @Test
  public void testCheckSingleUniverse_DisabledAlerts3() {
    mockBadHealthResponse();
    long now = System.currentTimeMillis() / 1000;
    String email = "foo@yugabyte.com";
    Universe u = setupDisabledAlertsConfig(email, now + 1000);
    testSingleUniverse(u, null, false, 1);
  }

  @Test
  public void testCheckSingleUniverse_DisabledAlerts4() {
    mockBadHealthResponse();
    long now = System.currentTimeMillis() / 1000;
    String email = "foo@yugabyte.com";
    Universe u = setupDisabledAlertsConfig(email, now - 10);
    testSingleUniverse(u, email, false, 1);
  }

  @Test
  public void testCheckSingleUniverse_MultipleEmails() {
    mockBadHealthResponse();
    Universe u = setupUniverse("univ1");
    String email = "foo@yugabyte.com";
    setupAlertingData(email, true, false);
    testSingleUniverse(u, String.format("%s,%s", YB_ALERT_TEST_EMAIL, email), false, 1);
  }

  @Test
  public void testCheckSingleUniverse_MultipleUniversesIndividually() {
    mockBadHealthResponse();
    Universe univ1 = setupUniverse("univ1");
    setupAlertingData(null, false, false);
    testSingleUniverse(univ1, null, false, 1);
    Universe univ2 = setupUniverse("univ2");
    testSingleUniverse(univ2, null, false, 2, 12);
  }

  @Test
  public void testCheckAllUniverses_TwoUniverses() {
    Universe u1 = setupUniverse("univ1");
    Universe u2 = setupUniverse("univ2");
    AlertingData alertingData = setupAlertingData(null, false, false);
    healthChecker.checkAllUniverses(defaultCustomer, alertingData, true, false);
    try {
      // Wait for scheduled health checks to run.
      while (!healthChecker.runningHealthChecks.get(u1.getUniverseUUID()).isDone()
          || !healthChecker.runningHealthChecks.get(u2.getUniverseUUID()).isDone()) {}
    } catch (Exception ignored) {
    }
    verifyNodeUniverseManager(12, 6);
  }

  @Test
  public void testCheckCustomer_NoUniverse() {
    validateNoDevopsCall();
  }

  @Test
  public void testCheckCustomer_NoAlertingConfig() {
    setupUniverse("univ1");
    healthChecker.checkCustomer(defaultCustomer);
    verifyNodeUniverseManager(6, 3);
  }

  @Test
  public void testCheckCustomer_InvalidUniverseNullDetails() {
    Universe u = setupUniverse("test");
    // Set the details to null.
    Universe.saveDetails(u.getUniverseUUID(), univ -> univ.setUniverseDetails(null));
    setupAlertingData(null, false, false);
    // Add a reference to this on the customer anyway.
    validateNoDevopsCall();
  }

  @Test
  public void testCheckCustomer_UpdateInProgress() {
    Universe u = setupUniverse("test");
    // Set updateInProgress to true.
    Universe.saveDetails(
        u.getUniverseUUID(),
        univ -> {
          UniverseDefinitionTaskParams details = univ.getUniverseDetails();
          details.updateInProgress = true;
          univ.setUniverseDetails(details);
        });
    setupAlertingData(null, false, false);
    validateNoDevopsCall();
  }

  @Test
  @Parameters({
    "BackupTable, true",
    "MultiTableBackup, true",
    "AddNodeToUniverse, false",
    "CreateBackup, true",
    "RestoreBackup, true"
  })
  public void testCheckSingleUniverse_UpdateInProgress(TaskType updatingTask, boolean shouldCheck) {
    Universe u = setupUniverse("test");
    // Set updateInProgress to true.
    u =
        Universe.saveDetails(
            u.getUniverseUUID(),
            univ -> {
              UniverseDefinitionTaskParams details = univ.getUniverseDetails();
              details.updateInProgress = true;
              details.updatingTask = updatingTask;
              univ.setUniverseDetails(details);
            });

    setupAlertingData(null, false, false);
    healthChecker.checkSingleUniverse(
        new HealthChecker.CheckSingleUniverseParams(
            u, defaultCustomer, false, false, false, YB_ALERT_TEST_EMAIL));
    verifyNodeUniverseManager(shouldCheck ? 6 : 0, shouldCheck ? 3 : 0);
  }

  @Test
  public void testCheckCustomer_InvalidUniverseBadProvider() {
    Universe u = setupUniverse("test");
    // Setup an invalid provider.
    Universe.saveDetails(
        u.getUniverseUUID(),
        univ -> {
          UniverseDefinitionTaskParams details = univ.getUniverseDetails();
          UniverseDefinitionTaskParams.UserIntent userIntent =
              details.getPrimaryCluster().userIntent;
          userIntent.provider = UUID.randomUUID().toString();
          univ.setUniverseDetails(details);
        });
    setupAlertingData(null, false, false);
    validateNoDevopsCall();
  }

  public void testCheckCustomer_InvalidUniverseNoAccessKey() {
    setupUniverse("test");
    setupAlertingData(null, false, false);
    accessKey.delete();
    validateNoDevopsCall();
  }

  @Test
  public void testTimingLogic() throws MessagingException {
    when(mockEmailHelper.getSmtpData(defaultCustomer.getUuid()))
        .thenReturn(EmailFixtures.createSmtpData());
    when(mockEmailHelper.getDestinations(defaultCustomer.getUuid()))
        .thenReturn(Collections.singletonList(YB_ALERT_TEST_EMAIL));
    setupAlertingData(YB_ALERT_TEST_EMAIL, false, false);
    mockBadHealthResponse();

    // Setup some waits.
    long waitMs = 500;
    // Wait one cycle between checks.
    when(mockConfig.getLong("yb.health.check_interval_ms")).thenReturn(waitMs);
    when(mockConfig.getLong("yb.health.store_interval_ms")).thenReturn(waitMs);
    // Wait two cycles between status updates.
    when(mockConfig.getLong("yb.health.status_interval_ms")).thenReturn(2 * waitMs);

    // Default prep.
    Universe u = setupUniverse("test");

    // First time we both check and send update.
    healthChecker.checkCustomer(defaultCustomer);
    try {
      while (!healthChecker.runningHealthChecks.get(u.getUniverseUUID()).isDone()) {}
    } catch (Exception ignored) {
    }
    verifyNodeUniverseManager(6, 3);
    verify(mockEmailHelper, times(1)).sendEmail(any(), any(), any(), any(), any());

    healthChecker.checkCustomer(defaultCustomer);
    try {
      while (!healthChecker.runningHealthChecks.get(u.getUniverseUUID()).isDone()) {}
    } catch (Exception ignored) {
    }
    verifyNodeUniverseManager(6, 6);
    // Should be onlyMetrics.
    verify(mockEmailHelper, times(1)).sendEmail(any(), any(), any(), any(), any());

    try {
      Thread.sleep(waitMs);
    } catch (InterruptedException e) {
    }
    // One cycle later, we should be running another test, but no status update, so first time
    // running with false.
    healthChecker.checkCustomer(defaultCustomer);
    try {
      while (!healthChecker.runningHealthChecks.get(u.getUniverseUUID()).isDone()) {}
    } catch (Exception ignored) {
    }
    verifyNodeUniverseManager(6, 9);
    verify(mockEmailHelper, times(2)).sendEmail(any(), any(), any(), any(), any());

    // Another cycle later, we should be running yet another test, but now with status update.
    try {
      Thread.sleep(waitMs);
    } catch (InterruptedException e) {
    }
    // One cycle later, we should be running another test, but no status update, so second time
    // running with true.
    healthChecker.checkCustomer(defaultCustomer);
    try {
      while (!healthChecker.runningHealthChecks.get(u.getUniverseUUID()).isDone()) {}
    } catch (Exception ignored) {
    }
    verifyNodeUniverseManager(6, 12);
    verify(mockEmailHelper, times(3)).sendEmail(any(), any(), any(), any(), any());
  }

  @Test
  public void testCheckSingleUniverse_ScriptFailure() {
    ShellResponse dummyShellResponseFail = ShellResponse.create(1, "Should error");

    when(mockNodeUniverseManager.runCommand(any(), any(), anyList(), any()))
        .thenReturn(dummyShellResponseFail);
    Universe u = setupUniverse("univ1");
    setupAlertingData(null, false, false);
    testSingleUniverse(u, null, true, 1);
  }

  @Test
  public void testCheckSingleUniverse_YedisEnabled() {
    testSingleUniverseWithYedisState(true);
  }

  @Test
  public void testCheckSingleUniverse_YedisDisabled() {
    testSingleUniverseWithYedisState(false);
  }

  private void testSingleUniverseWithYedisState(boolean enabledYEDIS) {
    Universe u = setupUniverse("univ1");
    UniverseDefinitionTaskParams details = u.getUniverseDetails();
    Cluster cluster = details.clusters.get(0);
    cluster.userIntent.enableYEDIS = enabledYEDIS;

    NodeDetails nd = new NodeDetails();
    nd.isRedisServer = enabledYEDIS;
    nd.redisServerRpcPort = 1234;
    nd.placementUuid = cluster.uuid;
    nd.cloudInfo = new CloudSpecificInfo();
    nd.cloudInfo.private_ip = "1.2.3.4";
    nd.nodeName = "some_node";

    details.nodeDetailsSet.add(nd);
    setupAlertingData(null, true, false);

    healthChecker.checkSingleUniverse(
        new HealthChecker.CheckSingleUniverseParams(u, defaultCustomer, true, false, false, null));
    ArgumentCaptor<List<String>> expectedCommand = ArgumentCaptor.forClass(List.class);
    verify(mockNodeUniverseManager, times(4))
        .runCommand(any(), any(), expectedCommand.capture(), any());

    assertThat(
        expectedCommand.getValue(), equalTo(ImmutableList.of("/home/yugabyte/bin/node_health.py")));
  }

  @Test
  public void testInvalidUniverseFailureMetric() throws MessagingException {
    Universe u = setupUniverse("test");
    setupAlertingData(YB_ALERT_TEST_EMAIL, false, false);

    // Imitate error while sending the email.
    doThrow(new MessagingException("TestException"))
        .when(mockEmailHelper)
        .sendEmail(any(), any(), any(), any(), any());
    when(mockEmailHelper.getSmtpData(defaultCustomer.getUuid()))
        .thenReturn(EmailFixtures.createSmtpData());

    healthChecker.checkSingleUniverse(
        new HealthChecker.CheckSingleUniverseParams(
            u, defaultCustomer, true, false, false, YB_ALERT_TEST_EMAIL));

    verify(mockEmailHelper, times(1)).sendEmail(any(), any(), any(), any(), any());
    // To check that metric is created.
    AssertHelper.assertMetricValue(
        metricService,
        MetricKey.builder()
            .customerUuid(defaultCustomer.getUuid())
            .name(PlatformMetrics.HEALTH_CHECK_NOTIFICATION_STATUS.getMetricName())
            .sourceUuid(u.getUniverseUUID())
            .build(),
        0.0);
  }

  @Test
  public void testCheckSingleUniverse_EmailSentWithTwoContentTypes() throws MessagingException {
    Universe u = setupUniverse("test");
    when(mockEmailHelper.getSmtpData(defaultCustomer.getUuid()))
        .thenReturn(EmailFixtures.createSmtpData());
    healthChecker.checkSingleUniverse(
        new HealthChecker.CheckSingleUniverseParams(
            u, defaultCustomer, true, false, false, YB_ALERT_TEST_EMAIL));

    verify(mockEmailHelper, times(1)).sendEmail(any(), any(), any(), any(), any());
    verify(report, times(1)).asHtml(eq(u), any(), anyBoolean());
    verify(report, times(1)).asPlainText(any(), anyBoolean());
  }

  private void mockGoodHealthResponse() {
    when(mockNodeUniverseManager.runCommand(any(), any(), anyList(), any()))
        .thenAnswer(
            i -> {
              NodeDetails nodeDetails = i.getArgument(0);

              return ShellResponse.create(
                  0,
                  ("{''error'': false, ''data'': [ {''node'':''"
                          + nodeDetails.cloudInfo.private_ip
                          + "'', ''has_error'': false, ''message'':''"
                          + dummyCheck
                          + "'', ''details'': [], ''has_warning'': false, ''node_name'': ''"
                          + nodeDetails.nodeName
                          + "'', ''timestamp_iso'': ''2022-03-01T09:22:23Z'' } ] }")
                      .replace("''", "\""));
            });
  }

  private void mockBadHealthResponse() {
    when(mockNodeUniverseManager.runCommand(any(), any(), anyList(), any()))
        .thenAnswer(
            i -> {
              NodeDetails nodeDetails = i.getArgument(0);

              return ShellResponse.create(
                  0,
                  "Command output:\n{\"data\":"
                      + "[{\"node\":\""
                      + nodeDetails.cloudInfo.private_ip
                      + "\", \"has_error\": true,"
                      + " \"message\":\""
                      + dummyCheck
                      + "\", \"details\": [\"Some details\"],"
                      + " \"has_warning\": true,"
                      + " \"node_name\": \""
                      + nodeDetails.nodeName
                      + "\","
                      + "\"timestamp_iso\":\"2022-03-01T09:22:23Z\"}]}");
            });
  }

  @Test
  public void testCheckSingleUniverse_EmailSent_RightAlertsResetted() throws MessagingException {
    Universe u = setupUniverse("test");
    when(mockEmailHelper.getSmtpData(defaultCustomer.getUuid()))
        .thenReturn(EmailFixtures.createSmtpData());
    setupAlertingData(YB_ALERT_TEST_EMAIL, false, false);
    mockGoodHealthResponse();

    metricService.setFailureStatusMetric(
        buildMetricTemplate(PlatformMetrics.HEALTH_CHECK_STATUS, u));
    metricService.setFailureStatusMetric(
        buildMetricTemplate(PlatformMetrics.HEALTH_CHECK_NOTIFICATION_STATUS, u));
    metricService.setFailureStatusMetric(
        buildMetricTemplate(PlatformMetrics.ALERT_MANAGER_STATUS, u));

    healthChecker.checkSingleUniverse(
        new HealthChecker.CheckSingleUniverseParams(
            u, defaultCustomer, true, false, false, YB_ALERT_TEST_EMAIL));

    AssertHelper.assertMetricValue(
        metricService,
        MetricKey.builder()
            .customerUuid(defaultCustomer.getUuid())
            .name(PlatformMetrics.HEALTH_CHECK_STATUS.getMetricName())
            .sourceUuid(u.getUniverseUUID())
            .build(),
        1.0);

    AssertHelper.assertMetricValue(
        metricService,
        MetricKey.builder()
            .customerUuid(defaultCustomer.getUuid())
            .name(PlatformMetrics.HEALTH_CHECK_NOTIFICATION_STATUS.getMetricName())
            .sourceUuid(u.getUniverseUUID())
            .build(),
        1.0);

    AssertHelper.assertMetricValue(
        metricService,
        MetricKey.builder()
            .customerUuid(defaultCustomer.getUuid())
            .name(PlatformMetrics.ALERT_MANAGER_STATUS.getMetricName())
            .sourceUuid(u.getUniverseUUID())
            .build(),
        0.0);
  }

  @Test
  public void testNodeCheckTimeout() {
    Universe u = setupUniverse("test");
    when(mockEmailHelper.getSmtpData(defaultCustomer.getUuid()))
        .thenReturn(EmailFixtures.createSmtpData());
    setupAlertingData(YB_ALERT_TEST_EMAIL, false, false);

    when(mockNodeUniverseManager.runCommand(any(), any(), anyList(), any()))
        .thenReturn(ShellResponse.create(9, StringUtils.EMPTY));
    healthChecker.checkSingleUniverse(
        new HealthChecker.CheckSingleUniverseParams(
            u, defaultCustomer, true, false, false, YB_ALERT_TEST_EMAIL));

    HealthCheck results = HealthCheck.getLatest(u.getUniverseUUID());
    assertThat(results, notNullValue());
    assertThat(results.getDetailsJson().getData(), hasSize(3));
    for (NodeData nodeData : results.getDetailsJson().getData()) {

      assertThat(
          nodeData.getDetails(),
          contains(
              "Node check failed: java.lang.RuntimeException:"
                  + " Error occurred. Code: 9. Output: "));
    }
  }

  @Test
  public void testCheckSingleUniverse_WithUnprovisionedNode_AlertSent() {
    Universe u = setupUniverse("univ1");
    UniverseDefinitionTaskParams details = u.getUniverseDetails();
    Cluster cluster = details.clusters.get(0);

    NodeDetails nd = new NodeDetails();
    nd.nodeName = "test";
    nd.redisServerRpcPort = 1234;
    nd.placementUuid = cluster.uuid;
    nd.cloudInfo = new CloudSpecificInfo();

    details.nodeDetailsSet.add(nd);
    setupAlertingData(null, true, false);

    healthChecker.checkSingleUniverse(
        new HealthChecker.CheckSingleUniverseParams(u, defaultCustomer, true, false, false, null));
    verifyNodeUniverseManager(0, 0);
    AssertHelper.assertMetricValue(
        metricService,
        MetricKey.builder()
            .customerUuid(defaultCustomer.getUuid())
            .name(PlatformMetrics.HEALTH_CHECK_STATUS.getMetricName())
            .sourceUuid(u.getUniverseUUID())
            .build(),
        0.0);
  }

  @Test
  public void testCheckSingleUniverse_WithUnprovisionedNodeNoName_AlertSent() {
    Universe u = setupUniverse("univ1");
    UniverseDefinitionTaskParams details = u.getUniverseDetails();
    Cluster cluster = details.clusters.get(0);

    NodeDetails nd = new NodeDetails();
    nd.redisServerRpcPort = 1234;
    nd.placementUuid = cluster.uuid;
    nd.cloudInfo = new CloudSpecificInfo();

    details.nodeDetailsSet.add(nd);
    setupAlertingData(null, true, false);

    healthChecker.checkSingleUniverse(
        new HealthChecker.CheckSingleUniverseParams(u, defaultCustomer, true, false, false, null));
    verifyNodeUniverseManager(0, 0);
    AssertHelper.assertMetricValue(
        metricService,
        MetricKey.builder()
            .customerUuid(defaultCustomer.getUuid())
            .name(PlatformMetrics.HEALTH_CHECK_STATUS.getMetricName())
            .sourceUuid(u.getUniverseUUID())
            .build(),
        0.0);
  }

  @Test
  public void testCanHealthCheckUniverse_MissingUniverse() {
    assertFalse(HealthChecker.canHealthCheckUniverse(UUID.randomUUID()));
  }

  @Test
  public void testCanHealthCheckUniverse_ExistingUniverseUnlocked() {
    Universe u = setupUniverse("univ1");
    assertTrue(HealthChecker.canHealthCheckUniverse(u.getUniverseUUID()));
  }

  @Test
  public void testCanHealthCheckUniverse_ExistingUniverseLocked() {
    Universe u = setupUniverse("univ1");
    Universe.saveDetails(
        u.getUniverseUUID(),
        univ -> {
          UniverseDefinitionTaskParams details = univ.getUniverseDetails();
          details.updateInProgress = true;
          univ.setUniverseDetails(details);
        });
    assertFalse(HealthChecker.canHealthCheckUniverse(u.getUniverseUUID()));
  }

  @Test
  public void testCheckSingleUniverse_EmailNotSentIfMetricsOnly() throws MessagingException {
    Universe u = setupUniverse("test");
    when(mockEmailHelper.getSmtpData(defaultCustomer.getUuid()))
        .thenReturn(EmailFixtures.createSmtpData());
    healthChecker.checkSingleUniverse(
        new HealthChecker.CheckSingleUniverseParams(
            u, defaultCustomer, true, false, true, YB_ALERT_TEST_EMAIL));

    verify(mockEmailHelper, times(0)).sendEmail(any(), any(), any(), any(), any());
  }
}
