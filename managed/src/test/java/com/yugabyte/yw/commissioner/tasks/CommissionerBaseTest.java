// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks;

import static com.yugabyte.yw.common.TestHelper.testDatabase;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.fail;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyInt;
import static org.mockito.ArgumentMatchers.anyLong;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.ArgumentMatchers.endsWith;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.lenient;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.spy;
import static org.mockito.Mockito.when;
import static play.inject.Bindings.bind;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.net.HostAndPort;
import com.typesafe.config.Config;
import com.yugabyte.yw.cloud.CloudAPI;
import com.yugabyte.yw.cloud.aws.AWSInitializer;
import com.yugabyte.yw.cloud.gcp.GCPInitializer;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.CallHome;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.commissioner.DefaultExecutorServiceProvider;
import com.yugabyte.yw.commissioner.ExecutorServiceProvider;
import com.yugabyte.yw.commissioner.TaskExecutor;
import com.yugabyte.yw.commissioner.tasks.local.LocalProviderUniverseTestBase;
import com.yugabyte.yw.commissioner.tasks.subtasks.CheckFollowerLag;
import com.yugabyte.yw.commissioner.tasks.subtasks.CheckLeaderlessTablets;
import com.yugabyte.yw.commissioner.tasks.subtasks.CheckUnderReplicatedTablets;
import com.yugabyte.yw.common.AccessManager;
import com.yugabyte.yw.common.ApiHelper;
import com.yugabyte.yw.common.CloudQueryHelper;
import com.yugabyte.yw.common.CloudUtilFactory;
import com.yugabyte.yw.common.ConfigHelper;
import com.yugabyte.yw.common.CustomerTaskManager;
import com.yugabyte.yw.common.DnsManager;
import com.yugabyte.yw.common.LdapUtil;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.NetworkManager;
import com.yugabyte.yw.common.NodeManager;
import com.yugabyte.yw.common.NodeUIApiHelper;
import com.yugabyte.yw.common.NodeUniverseManager;
import com.yugabyte.yw.common.PlatformExecutorFactory;
import com.yugabyte.yw.common.PlatformGuiceApplicationBaseTest;
import com.yugabyte.yw.common.PrometheusConfigManager;
import com.yugabyte.yw.common.ProviderEditRestrictionManager;
import com.yugabyte.yw.common.ReleaseContainer;
import com.yugabyte.yw.common.ReleaseManager;
import com.yugabyte.yw.common.ReleasesUtils;
import com.yugabyte.yw.common.ShellKubernetesManager;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.SwamperHelper;
import com.yugabyte.yw.common.TableManager;
import com.yugabyte.yw.common.TableManagerYb;
import com.yugabyte.yw.common.YcqlQueryExecutor;
import com.yugabyte.yw.common.YsqlQueryExecutor;
import com.yugabyte.yw.common.alerts.AlertConfigurationService;
import com.yugabyte.yw.common.alerts.AlertDefinitionService;
import com.yugabyte.yw.common.alerts.AlertService;
import com.yugabyte.yw.common.backuprestore.BackupHelper;
import com.yugabyte.yw.common.backuprestore.ybc.YbcManager;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.common.config.impl.SettableRuntimeConfigFactory;
import com.yugabyte.yw.common.gflags.AutoFlagUtil;
import com.yugabyte.yw.common.gflags.GFlagsValidation;
import com.yugabyte.yw.common.kms.EncryptionAtRestManager;
import com.yugabyte.yw.common.metrics.MetricService;
import com.yugabyte.yw.common.nodeui.DumpEntitiesResponse;
import com.yugabyte.yw.common.nodeui.DumpEntitiesResponse.Replica;
import com.yugabyte.yw.common.nodeui.DumpEntitiesResponse.Tablet;
import com.yugabyte.yw.common.operator.OperatorStatusUpdater;
import com.yugabyte.yw.common.operator.OperatorStatusUpdaterFactory;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.common.supportbundle.SupportBundleComponent;
import com.yugabyte.yw.common.supportbundle.SupportBundleComponentFactory;
import com.yugabyte.yw.forms.ITaskParams;
import com.yugabyte.yw.forms.SoftwareUpgradeParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.ClusterType;
import com.yugabyte.yw.metrics.MetricQueryHelper;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.CustomerTask.TargetType;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.TaskInfo.State;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collection;
import java.util.Collections;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.Set;
import java.util.TreeMap;
import java.util.UUID;
import java.util.stream.Collectors;
import kamon.instrumentation.play.GuiceModule;
import lombok.extern.slf4j.Slf4j;
import org.jboss.logging.MDC;
import org.junit.Before;
import org.mockito.Mockito;
import org.pac4j.core.context.session.SessionStore;
import org.pac4j.play.CallbackController;
import org.pac4j.play.store.PlayCacheSessionStore;
import org.slf4j.LoggerFactory;
import org.yb.client.AreNodesSafeToTakeDownResponse;
import org.yb.client.GetMasterClusterConfigResponse;
import org.yb.client.ListLiveTabletServersResponse;
import org.yb.client.YBClient;
import org.yb.master.CatalogEntityInfo;
import org.yb.util.TabletServerInfo;
import play.Application;
import play.Environment;
import play.inject.guice.GuiceApplicationBuilder;
import play.libs.Json;

@Slf4j
public abstract class CommissionerBaseTest extends PlatformGuiceApplicationBaseTest {
  protected static final int MAX_RETRY_COUNT = 10000;
  protected static final String ENABLE_CUSTOM_HOOKS_PATH =
      "yb.security.custom_hooks.enable_custom_hooks";
  protected static final String ENABLE_SUDO_PATH = "yb.security.custom_hooks.enable_sudo";

  protected AccessManager mockAccessManager;
  protected NetworkManager mockNetworkManager;
  protected Config mockConfig;
  protected ConfigHelper mockConfigHelper;
  protected AWSInitializer mockAWSInitializer;
  protected GCPInitializer mockGCPInitializer;
  protected YBClientService mockYBClient;
  protected NodeManager mockNodeManager;
  protected DnsManager mockDnsManager;
  protected TableManager mockTableManager;
  protected TableManagerYb mockTableManagerYb;
  protected CloudQueryHelper mockCloudQueryHelper;
  protected ShellKubernetesManager mockKubernetesManager;
  protected SwamperHelper mockSwamperHelper;
  protected CallHome mockCallHome;
  protected CallbackController mockCallbackController;
  protected PlayCacheSessionStore mockSessionStore;
  protected ApiHelper mockApiHelper;
  protected NodeUIApiHelper mockNodeUIApiHelper;
  protected MetricQueryHelper mockMetricQueryHelper;
  protected MetricService metricService;
  protected AlertService alertService;
  protected AlertDefinitionService alertDefinitionService;
  protected AlertConfigurationService alertConfigurationService;
  protected YcqlQueryExecutor mockYcqlQueryExecutor;
  protected YsqlQueryExecutor mockYsqlQueryExecutor;
  protected LdapUtil mockLdapUtil;
  protected NodeUniverseManager mockNodeUniverseManager;
  protected TaskExecutor taskExecutor;
  protected EncryptionAtRestManager mockEARManager;
  protected SupportBundleComponent mockSupportBundleComponent;
  protected SupportBundleComponentFactory mockSupportBundleComponentFactory;
  protected ReleaseManager mockReleaseManager;
  protected GFlagsValidation mockGFlagsValidation;
  protected AutoFlagUtil mockAutoFlagUtil;
  protected ProviderEditRestrictionManager providerEditRestrictionManager;
  protected BackupHelper mockBackupHelper;
  protected PrometheusConfigManager mockPrometheusConfigManager;
  protected YbcManager mockYbcManager;
  protected OperatorStatusUpdaterFactory mockOperatorStatusUpdaterFactory;
  protected OperatorStatusUpdater mockOperatorStatusUpdater;
  protected CloudUtilFactory mockCloudUtilFactory;
  protected ReleasesUtils mockReleasesUtils;

  protected BaseTaskDependencies mockBaseTaskDependencies =
      Mockito.mock(BaseTaskDependencies.class);

  protected Customer defaultCustomer;
  protected Provider defaultProvider;
  protected Provider gcpProvider;
  protected Provider azuProvider;
  protected Provider onPremProvider;
  protected Provider kubernetesProvider;
  protected SettableRuntimeConfigFactory factory;
  protected RuntimeConfGetter confGetter;
  protected CloudAPI.Factory mockCloudAPIFactory;

  protected Commissioner commissioner;
  protected CustomerTaskManager customerTaskManager;
  protected ReleaseManager.ReleaseMetadata releaseMetadata;
  protected ReleaseContainer releaseContainer;

  @Before
  public void setUp() {
    mockConfig = spy(app.config());
    commissioner = app.injector().instanceOf(Commissioner.class);
    customerTaskManager = app.injector().instanceOf(CustomerTaskManager.class);
    defaultCustomer = ModelFactory.testCustomer();
    defaultProvider = ModelFactory.awsProvider(defaultCustomer);
    gcpProvider = ModelFactory.gcpProvider(defaultCustomer);
    azuProvider = ModelFactory.azuProvider(defaultCustomer);
    onPremProvider = ModelFactory.onpremProvider(defaultCustomer);
    kubernetesProvider = ModelFactory.kubernetesProvider(defaultCustomer);
    metricService = app.injector().instanceOf(MetricService.class);
    alertService = app.injector().instanceOf(AlertService.class);
    alertDefinitionService = app.injector().instanceOf(AlertDefinitionService.class);
    RuntimeConfigFactory configFactory = app.injector().instanceOf(RuntimeConfigFactory.class);
    alertConfigurationService = spy(app.injector().instanceOf(AlertConfigurationService.class));
    taskExecutor = app.injector().instanceOf(TaskExecutor.class);
    providerEditRestrictionManager =
        app.injector().instanceOf(ProviderEditRestrictionManager.class);
    mockCloudUtilFactory = mock(CloudUtilFactory.class);
    mockReleasesUtils = mock(ReleasesUtils.class);

    // Enable custom hooks in tests
    factory = app.injector().instanceOf(SettableRuntimeConfigFactory.class);
    confGetter = app.injector().instanceOf(RuntimeConfGetter.class);
    factory.globalRuntimeConf().setValue(ENABLE_CUSTOM_HOOKS_PATH, "true");
    factory.globalRuntimeConf().setValue(ENABLE_SUDO_PATH, "true");
    factory.globalRuntimeConf().setValue("yb.universe.consistency_check_enabled", "true");

    when(mockBaseTaskDependencies.getApplication()).thenReturn(app);
    when(mockBaseTaskDependencies.getConfig()).thenReturn(mockConfig);
    when(mockBaseTaskDependencies.getConfigHelper()).thenReturn(mockConfigHelper);
    when(mockBaseTaskDependencies.getEnvironment())
        .thenReturn(app.injector().instanceOf(Environment.class));
    when(mockBaseTaskDependencies.getYbService()).thenReturn(mockYBClient);
    when(mockBaseTaskDependencies.getTableManager()).thenReturn(mockTableManager);
    when(mockBaseTaskDependencies.getTableManagerYb()).thenReturn(mockTableManagerYb);
    when(mockBaseTaskDependencies.getMetricService()).thenReturn(metricService);
    when(mockBaseTaskDependencies.getRuntimeConfigFactory()).thenReturn(configFactory);
    when(mockBaseTaskDependencies.getConfGetter()).thenReturn(confGetter);
    when(mockBaseTaskDependencies.getAlertConfigurationService())
        .thenReturn(alertConfigurationService);
    when(mockBaseTaskDependencies.getExecutorFactory())
        .thenReturn(app.injector().instanceOf(PlatformExecutorFactory.class));
    when(mockBaseTaskDependencies.getTaskExecutor()).thenReturn(taskExecutor);
    when(mockBaseTaskDependencies.getHealthChecker()).thenReturn(mockHealthChecker);
    when(mockBaseTaskDependencies.getNodeManager()).thenReturn(mockNodeManager);
    when(mockBaseTaskDependencies.getBackupHelper()).thenReturn(mockBackupHelper);
    when(mockBaseTaskDependencies.getCommissioner()).thenReturn(commissioner);
    when(mockBaseTaskDependencies.getNodeUIApiHelper()).thenReturn(mockNodeUIApiHelper);
    when(mockBaseTaskDependencies.getReleaseManager()).thenReturn(mockReleaseManager);
    when(mockBaseTaskDependencies.getYsqlQueryExecutor()).thenReturn(mockYsqlQueryExecutor);
    when(mockBaseTaskDependencies.getGFlagsValidation()).thenReturn(mockGFlagsValidation);
    when(mockBaseTaskDependencies.getNodeUniverseManager()).thenReturn(mockNodeUniverseManager);
    releaseMetadata = ReleaseManager.ReleaseMetadata.create("1.0.0.0-b1");
    releaseContainer =
        new ReleaseContainer(releaseMetadata, mockCloudUtilFactory, mockConfig, mockReleasesUtils);
    lenient().when(mockReleaseManager.getReleaseByVersion(any())).thenReturn(releaseContainer);
  }

  @Override
  protected Application provideApplication() {
    mockAccessManager = mock(AccessManager.class);
    mockNetworkManager = mock(NetworkManager.class);
    mockConfigHelper = mock(ConfigHelper.class);
    mockAWSInitializer = mock(AWSInitializer.class);
    mockGCPInitializer = mock(GCPInitializer.class);
    mockYBClient = mock(YBClientService.class);
    mockNodeManager = mock(NodeManager.class);
    mockDnsManager = mock(DnsManager.class);
    mockCloudQueryHelper = mock(CloudQueryHelper.class);
    mockTableManager = mock(TableManager.class);
    mockTableManagerYb = mock(TableManagerYb.class);
    mockKubernetesManager = mock(ShellKubernetesManager.class);
    mockSwamperHelper = mock(SwamperHelper.class);
    mockCallHome = mock(CallHome.class);
    mockCallbackController = mock(CallbackController.class);
    mockSessionStore = mock(PlayCacheSessionStore.class);
    mockApiHelper = mock(ApiHelper.class);
    mockNodeUIApiHelper = mock(NodeUIApiHelper.class);
    mockMetricQueryHelper = mock(MetricQueryHelper.class);
    mockYcqlQueryExecutor = mock(YcqlQueryExecutor.class);
    mockYsqlQueryExecutor = mock(YsqlQueryExecutor.class);
    mockLdapUtil = mock(LdapUtil.class);
    mockNodeUniverseManager = mock(NodeUniverseManager.class);
    mockEARManager = mock(EncryptionAtRestManager.class);
    mockSupportBundleComponent = mock(SupportBundleComponent.class);
    mockSupportBundleComponentFactory = mock(SupportBundleComponentFactory.class);
    mockGFlagsValidation = mock(GFlagsValidation.class);
    mockAutoFlagUtil = mock(AutoFlagUtil.class);
    mockReleaseManager = mock(ReleaseManager.class);
    mockCloudAPIFactory = mock(CloudAPI.Factory.class);
    mockBackupHelper = mock(BackupHelper.class);
    mockYbcManager = mock(YbcManager.class);
    mockPrometheusConfigManager = mock(PrometheusConfigManager.class);
    mockOperatorStatusUpdaterFactory = mock(OperatorStatusUpdaterFactory.class);
    mockOperatorStatusUpdater = mock(OperatorStatusUpdater.class);

    return configureApplication(
            new GuiceApplicationBuilder()
                .disable(GuiceModule.class)
                .configure(testDatabase())
                .overrides(bind(AccessManager.class).toInstance(mockAccessManager))
                .overrides(bind(NetworkManager.class).toInstance(mockNetworkManager))
                .overrides(bind(ConfigHelper.class).toInstance(mockConfigHelper))
                .overrides(bind(AWSInitializer.class).toInstance(mockAWSInitializer))
                .overrides(bind(GCPInitializer.class).toInstance(mockGCPInitializer))
                .overrides(bind(YBClientService.class).toInstance(mockYBClient))
                .overrides(bind(NodeManager.class).toInstance(mockNodeManager))
                .overrides(bind(DnsManager.class).toInstance(mockDnsManager))
                .overrides(bind(CloudQueryHelper.class).toInstance(mockCloudQueryHelper))
                .overrides(bind(TableManager.class).toInstance(mockTableManager))
                .overrides(bind(TableManagerYb.class).toInstance(mockTableManagerYb))
                .overrides(bind(ShellKubernetesManager.class).toInstance(mockKubernetesManager))
                .overrides(bind(SwamperHelper.class).toInstance(mockSwamperHelper))
                .overrides(bind(CallHome.class).toInstance(mockCallHome))
                .overrides(bind(CallbackController.class).toInstance(mockCallbackController))
                .overrides(bind(SessionStore.class).toInstance(mockSessionStore))
                .overrides(bind(ApiHelper.class).toInstance(mockApiHelper))
                .overrides(bind(MetricQueryHelper.class).toInstance(mockMetricQueryHelper))
                .overrides(bind(BaseTaskDependencies.class).toInstance(mockBaseTaskDependencies))
                .overrides(
                    bind(SupportBundleComponent.class).toInstance(mockSupportBundleComponent))
                .overrides(
                    bind(SupportBundleComponentFactory.class)
                        .toInstance(mockSupportBundleComponentFactory))
                .overrides(bind(YcqlQueryExecutor.class).toInstance(mockYcqlQueryExecutor))
                .overrides(bind(YsqlQueryExecutor.class).toInstance(mockYsqlQueryExecutor))
                .overrides(bind(LdapUtil.class).toInstance(mockLdapUtil))
                .overrides(bind(NodeUniverseManager.class).toInstance(mockNodeUniverseManager))
                .overrides(
                    bind(ExecutorServiceProvider.class).to(DefaultExecutorServiceProvider.class))
                .overrides(bind(EncryptionAtRestManager.class).toInstance(mockEARManager))
                .overrides(bind(GFlagsValidation.class).toInstance(mockGFlagsValidation))
                .overrides(bind(AutoFlagUtil.class).toInstance(mockAutoFlagUtil))
                .overrides(bind(NodeUIApiHelper.class).toInstance(mockNodeUIApiHelper))
                .overrides(bind(BackupHelper.class).toInstance(mockBackupHelper))
                .overrides(bind(YbcManager.class).toInstance(mockYbcManager))
                .overrides(
                    bind(PrometheusConfigManager.class).toInstance(mockPrometheusConfigManager))
                .overrides(bind(ReleaseManager.class).toInstance(mockReleaseManager)))
        .overrides(bind(CloudAPI.Factory.class).toInstance(mockCloudAPIFactory))
        .overrides(
            bind(OperatorStatusUpdaterFactory.class).toInstance(mockOperatorStatusUpdaterFactory))
        .build();
  }

  public void mockWaits(YBClient mockClient) {
    mockWaits(mockClient, 1);
  }

  public void mockWaits(YBClient mockClient, int version) {
    try {
      // PlacementUtil mock.
      CatalogEntityInfo.SysClusterConfigEntryPB.Builder configBuilder =
          CatalogEntityInfo.SysClusterConfigEntryPB.newBuilder().setVersion(version);
      GetMasterClusterConfigResponse gcr =
          new GetMasterClusterConfigResponse(0, "", configBuilder.build(), null);
      when(mockClient.getMasterClusterConfig()).thenReturn(gcr);
    } catch (Exception e) {
      fail(e.getMessage());
    }
  }

  protected TaskType assertTaskType(List<TaskInfo> tasks, TaskType expectedTaskType) {
    TaskType taskType = tasks.get(0).getTaskType();
    assertEquals(expectedTaskType, taskType);
    return taskType;
  }

  protected TaskType assertTaskType(List<TaskInfo> tasks, TaskType expectedTaskType, int position) {
    TaskType taskType = tasks.get(0).getTaskType();
    assertEquals("at position " + position, expectedTaskType, taskType);
    return taskType;
  }

  public TaskInfo waitForTask(UUID taskUUID) throws InterruptedException {
    return waitForTask(taskUUID, 200);
  }

  public TaskInfo waitForTask(UUID taskUUID, long sleepDuration) throws InterruptedException {
    int numRetries = 0;
    TaskInfo taskInfo = null;
    while (numRetries < MAX_RETRY_COUNT) {
      // Here is a hack to decrease amount of accidental problems for tests using this
      // function:
      // Surrounding the next block with try {} catch {} as sometimes h2 raises NPE
      // inside the get() request. We are not afraid of such exception as the next
      // request will succeeded.
      if (!commissioner.isTaskRunning(taskUUID)) {
        try {
          taskInfo = TaskInfo.getOrBadRequest(taskUUID);
          // Also, ensure task details are set before returning.
          if (TaskInfo.COMPLETED_STATES.contains(taskInfo.getTaskState())
              && taskInfo.getTaskParams() != null) {
            return taskInfo;
          }
        } catch (Exception e) {
        }
      }
      Thread.sleep(sleepDuration);
      numRetries++;
    }

    String runningTasks =
        taskInfo.getSubTasks().stream()
            .filter(t -> t.getTaskState() == State.Running)
            .map(t -> getBriefTaskInfo(t))
            .collect(Collectors.joining(","));

    throw new RuntimeException(
        "WaitFor task exceeded maxRetries! Task state is "
            + taskInfo.getTaskState()
            + ".\n Running subtasks: "
            + runningTasks);
  }

  public static String getBriefTaskInfo(TaskInfo taskInfo) {
    StringBuilder sb = new StringBuilder();
    sb.append(taskInfo.getTaskType());
    if (taskInfo.getTaskParams() != null && taskInfo.getTaskParams().has("nodeName")) {
      sb.append("(");
      sb.append(taskInfo.getTaskParams().get("nodeName").textValue());
      if (taskInfo.getTaskParams().has("serverType")) {
        sb.append(" ").append(taskInfo.getTaskParams().get("serverType").textValue());
      }
      if (taskInfo.getTaskParams().has("state")) {
        sb.append(" ").append(taskInfo.getTaskParams().get("state").textValue());
      }
      if (taskInfo.getTaskParams().has("process")) {
        sb.append(" ").append(taskInfo.getTaskParams().get("process").textValue());
      }
      if (taskInfo.getTaskParams().has("command")) {
        sb.append(" ").append(taskInfo.getTaskParams().get("command").textValue());
      }
      sb.append(")");
    }
    return sb.toString();
  }

  public boolean waitForTaskRunning(UUID taskUUID) throws InterruptedException {
    int numRetries = 0;
    while (numRetries < MAX_RETRY_COUNT) {
      // Here is a hack to decrease amount of accidental problems for tests using this
      // function:
      // Surrounding the next block with try {} catch {} as sometimes h2 raises NPE
      // inside the get() request. We are not afraid of such exception as the next
      // request will succeeded.
      boolean isRunning = commissioner.isTaskRunning(taskUUID);
      if (isRunning) {
        return isRunning;
      }
      TaskInfo taskInfo = TaskInfo.getOrBadRequest(taskUUID);
      if (TaskInfo.COMPLETED_STATES.contains(taskInfo.getTaskState())) {
        return false;
      }
      Thread.sleep(10);
      numRetries++;
    }
    throw new RuntimeException(
        "WaitFor task running exceeded maxRetries! Task state is "
            + TaskInfo.getOrBadRequest(taskUUID).getTaskState());
  }

  public void waitForTaskPaused(UUID taskUuid) throws InterruptedException {
    waitForTaskPaused(taskUuid, commissioner);
  }

  public static void waitForTaskPaused(UUID taskUuid, Commissioner commissioner)
      throws InterruptedException {
    int numRetries = 0;
    while (numRetries < MAX_RETRY_COUNT) {
      if (!commissioner.isTaskRunning(taskUuid)) {
        throw new RuntimeException(String.format("Task %s is not running", taskUuid));
      }
      if (commissioner.isTaskPaused(taskUuid)) {
        return;
      }
      Thread.sleep(10);
      numRetries++;
    }
    throw new RuntimeException(
        "WaitFor task exceeded maxRetries! Task state is "
            + TaskInfo.getOrBadRequest(taskUuid).getTaskState());
  }

  private void setAbortPosition(int abortPosition) {
    MDC.remove(Commissioner.SUBTASK_PAUSE_POSITION_PROPERTY);
    MDC.put(Commissioner.SUBTASK_ABORT_POSITION_PROPERTY, String.valueOf(abortPosition));
  }

  public static void setPausePosition(int pausePosition) {
    MDC.remove(Commissioner.SUBTASK_ABORT_POSITION_PROPERTY);
    MDC.put(Commissioner.SUBTASK_PAUSE_POSITION_PROPERTY, String.valueOf(pausePosition));
  }

  public static void clearAbortOrPausePositions() {
    MDC.remove(Commissioner.SUBTASK_ABORT_POSITION_PROPERTY);
    MDC.remove(Commissioner.SUBTASK_PAUSE_POSITION_PROPERTY);
  }

  public void verifyTaskRetries(
      Customer customer,
      CustomerTask.TaskType customerTaskType,
      TargetType targetType,
      UUID targetUuid,
      TaskType taskType,
      ITaskParams taskParams) {
    verifyTaskRetries(
        customer, customerTaskType, targetType, targetUuid, taskType, taskParams, true, 1);
  }

  public void verifyTaskRetries(
      Customer customer,
      CustomerTask.TaskType customerTaskType,
      TargetType targetType,
      UUID targetUuid,
      TaskType taskType,
      ITaskParams taskParams,
      boolean checkStrictOrdering) {
    verifyTaskRetries(
        customer,
        customerTaskType,
        targetType,
        targetUuid,
        taskType,
        taskParams,
        checkStrictOrdering,
        1);
  }

  /** This method returns all the subtasks of a task. */
  private Map<Integer, List<TaskInfo>> getSubtasks(UUID taskUuid) throws Exception {
    // Pause at the beginning to capture the sub-tasks to be executed.
    waitForTaskPaused(taskUuid);
    TaskInfo taskInfo = TaskInfo.getOrBadRequest(taskUuid);
    Optional<Integer> optionalIdx =
        taskInfo.getSubTasks().stream()
            .filter(t -> t.getTaskType() == TaskType.FreezeUniverse)
            .map(TaskInfo::getPosition)
            .findFirst();
    if (optionalIdx.isPresent()) {
      // Resume the task to get past the freeze subtask so that all the sub-tasks are created.
      setPausePosition(optionalIdx.get() + 1);
      commissioner.resumeTask(taskUuid);
      waitForTaskPaused(taskUuid);
      taskInfo.refresh();
    }
    // Fetch the original list of sub-tasks to be executed before any retry.
    Map<Integer, List<TaskInfo>> subtaskMap =
        taskInfo.getSubTasks().stream()
            .collect(
                Collectors.groupingBy(
                    TaskInfo::getPosition, () -> new TreeMap<>(), Collectors.toList()));
    // Verify that it has some subtasks after FreezeUniverse if it is present.
    assertTrue(
        "At least some real subtasks must be present",
        subtaskMap.size() > (optionalIdx.isPresent() ? optionalIdx.get() + 1 : 1));
    return subtaskMap;
  }

  /**
   * This method aborts before every sub-task starting from position 0 and retries to make sure no
   * pending subtasks from the first attempt are skipped on every retry. This mainly verifies that
   * conditional blocks (e.g if isMaster) on an enclosing subtask outcome (e.g isMaster = true) do
   * not skip any sub-tasks.
   */
  public void verifyTaskRetries(
      Customer customer,
      CustomerTask.TaskType customerTaskType,
      TargetType targetType,
      UUID targetUuid,
      TaskType taskType,
      ITaskParams taskParams,
      boolean checkStrictOrdering,
      int abortStep) {
    try {

      // Turning off logs for task retry tests as we're doing 194 retries in this test sometimes,
      // and it spams logs like crazy - which will cause OOMs in Jenkins
      // - as Jenkins caches stdout in memory until test finishes.
      ch.qos.logback.classic.Logger rootLogger =
          (ch.qos.logback.classic.Logger)
              LoggerFactory.getLogger(ch.qos.logback.classic.Logger.ROOT_LOGGER_NAME);
      rootLogger.detachAppender("ASYNCSTDOUT");

      setPausePosition(0);
      UUID taskUuid = commissioner.submit(taskType, taskParams);
      CustomerTask.create(
          customer, targetUuid, taskUuid, targetType, customerTaskType, "fake-name");
      Map<Integer, List<TaskInfo>> expectedSubTaskMap = getSubtasks(taskUuid);
      List<TaskType> expectedSubTaskTypes =
          expectedSubTaskMap.values().stream()
              .map(l -> l.get(0).getTaskType())
              .collect(Collectors.toList());
      int freezeIdx = expectedSubTaskTypes.indexOf(TaskType.FreezeUniverse);
      // Number of sub-tasks to be executed on any run.
      int totalSubTaskCount = expectedSubTaskMap.size();
      int pendingSubTaskCount =
          freezeIdx >= 0 ? totalSubTaskCount - (freezeIdx + 1) : totalSubTaskCount;
      int retryCount = 0;
      while (pendingSubTaskCount > 0) {
        clearAbortOrPausePositions();
        // Abort starts from the first sub-task until there is no more sub-task left.
        int abortPosition = totalSubTaskCount - pendingSubTaskCount;
        if (pendingSubTaskCount > 1) {
          log.info(
              "Abort position at {} in the original subtasks {}",
              expectedSubTaskTypes.size() - pendingSubTaskCount,
              expectedSubTaskMap);
          setAbortPosition(abortPosition);
        }

        // mock db version during upgrade and rollback task to take action on all nodes.
        if (taskType.equals(TaskType.SoftwareUpgradeYB)
            || taskType.equals(TaskType.RollbackUpgrade)) {
          Universe universe = Universe.getOrBadRequest(targetUuid);
          int masterTserverNodesCount =
              universe.getMasters().size() + universe.getTServers().size();
          String oldVersion =
              universe.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion;
          if (pendingSubTaskCount <= 1) {
            String version;
            if (taskType.equals(TaskType.SoftwareUpgradeYB)) {
              SoftwareUpgradeParams params = (SoftwareUpgradeParams) taskParams;
              version = params.ybSoftwareVersion;
            } else {
              version = universe.getUniverseDetails().prevYBSoftwareConfig.getSoftwareVersion();
            }
            mockDBServerVersion(version, masterTserverNodesCount);
          } else {
            mockDBServerVersion(oldVersion, masterTserverNodesCount);
          }
        }

        // Resume task will resume and abort it if any abort position is set.
        commissioner.resumeTask(taskUuid);
        // Wait for the task to abort.
        TaskInfo taskInfo = waitForTask(taskUuid);
        if (taskInfo.getTaskState() == State.Failure) {
          throw new IllegalStateException(
              "Task failed " + LocalProviderUniverseTestBase.getAllErrorsStr(taskInfo));
        }
        if (pendingSubTaskCount <= 1) {
          assertEquals(State.Success, taskInfo.getTaskState());
        } else {
          assertEquals(State.Aborted, taskInfo.getTaskState());
          // Before retry, set the pause position to capture the list of subtasks
          // for the next abort in the next iteration.
          setPausePosition(0);
          CustomerTask customerTask =
              customerTaskManager.retryCustomerTask(customer.getUuid(), taskUuid);
          retryCount++;
          // New task UUID for the retry.
          taskUuid = customerTask.getTaskUUID();
          // Get the task and sub-tasks that are to be executed on retry.
          Map<Integer, List<TaskInfo>> retrySubTaskMap = getSubtasks(taskUuid);
          log.info(
              "Validating subtasks for next abort position at {} in the original subtasks {}",
              expectedSubTaskTypes.size() - pendingSubTaskCount + 1,
              expectedSubTaskMap);
          List<TaskType> retryTaskTypes =
              retrySubTaskMap.values().stream()
                  .map(l -> l.get(0).getTaskType())
                  .collect(Collectors.toList());
          // Get the tail-end of the sub-tasks with size equal to the pending sub-task count.
          List<TaskType> expectedTailTaskTypes =
              new ArrayList<>(
                  expectedSubTaskTypes.subList(
                      expectedSubTaskTypes.size() - pendingSubTaskCount,
                      expectedSubTaskTypes.size()));
          // The number of sub-tasks to be executed must be at least the pending sub-tasks as some
          // sub-tasks can be re-executed.
          if (retryTaskTypes.size() < pendingSubTaskCount) {
            throw new RuntimeException(
                String.format(
                    "Some subtasks are skipped on retry %d. At least %d sub-tasks are expected, but"
                        + " only %d are found. Expected(at least): %s, found: %s",
                    retryCount,
                    pendingSubTaskCount,
                    retryTaskTypes.size(),
                    expectedTailTaskTypes,
                    retryTaskTypes));
          }
          List<TaskType> tailTaskTypes =
              new ArrayList<>(
                  retryTaskTypes.subList(
                      retryTaskTypes.size() - pendingSubTaskCount, retryTaskTypes.size()));
          if (!checkStrictOrdering) {
            Collections.sort(expectedTailTaskTypes);
            Collections.sort(tailTaskTypes);
          }
          // The tail sublist of sub-subtasks must be exactly equal.
          if (!expectedTailTaskTypes.equals(tailTaskTypes)) {
            throw new RuntimeException(
                String.format(
                    "Mismatched order detected in subtasks (pending %d/%d) on retry %d. Expected:"
                        + " %s, found: %s",
                    pendingSubTaskCount,
                    expectedSubTaskTypes.size(),
                    retryCount,
                    expectedTailTaskTypes,
                    tailTaskTypes));
          }
          totalSubTaskCount = retryTaskTypes.size();
        }
        pendingSubTaskCount -= abortStep;
      }
    } catch (RuntimeException e) {
      throw e;
    } catch (Exception e) {
      throw new RuntimeException(e);
    } finally {
      clearAbortOrPausePositions();
    }
  }

  protected void setCheckNodesAreSafeToTakeDown(YBClient mockClient) {
    try {
      when(mockClient.areNodesSafeToTakeDown(any(), any(), anyLong()))
          .thenReturn(new AreNodesSafeToTakeDownResponse(null));
    } catch (Exception e) {
      throw new RuntimeException(e);
    }
  }

  public void setLeaderlessTabletsMock() {
    ObjectMapper mapper = new ObjectMapper();
    ObjectNode resultJson = mapper.createObjectNode();
    resultJson.set(CheckLeaderlessTablets.KEY, mapper.createArrayNode());
    when(mockNodeUIApiHelper.getRequest(endsWith(CheckLeaderlessTablets.URL_SUFFIX)))
        .thenReturn(resultJson);
  }

  public void setFollowerLagMock() {
    ObjectMapper mapper = new ObjectMapper();
    ArrayNode followerLagJson = mapper.createArrayNode();
    when(mockNodeUIApiHelper.getRequest(endsWith(CheckFollowerLag.URL_SUFFIX)))
        .thenReturn(followerLagJson);
  }

  public void setUnderReplicatedTabletsMock() {
    ObjectNode underReplicatedTabletsJson = Json.newObject();
    underReplicatedTabletsJson.put("underreplicated_tablets", Json.newArray());
    lenient()
        .when(mockNodeUIApiHelper.getRequest(endsWith(CheckUnderReplicatedTablets.URL_SUFFIX)))
        .thenReturn(underReplicatedTabletsJson);
  }

  /**
   * Mocks the dump entities endpoint response.
   *
   * @param universe the universe we are getting the dump entities endpoint response from.
   * @param nodeName if specified will add the specified node as a replica. If none is specified,
   *     will set all nodes in universe as a replica.
   * @param hasTablets whether or not the nodes will have tablets assigned.
   */
  public void setDumpEntitiesMock(Universe universe, String nodeName, boolean hasTablets) {
    Tablet tablet = new Tablet();
    tablet.setTabletId("Tablet id 1");

    Collection<NodeDetails> nodes = new HashSet<>();
    if (nodeName.isEmpty()) {
      nodes = universe.getNodes();
    } else {
      nodes.add(universe.getNode(nodeName));
    }

    List<Replica> replicas = new ArrayList<Replica>();
    tablet.setReplicas(replicas);

    if (hasTablets) {
      for (NodeDetails node : nodes) {
        // Replica replica = tablet.getReplicas() == null ? new Replica() : tablet.getReplicas();
        Replica replica = new Replica();
        replica.setAddr(node.cloudInfo.private_ip + ":" + node.tserverRpcPort);
        replicas.add(replica);
      }
    }

    DumpEntitiesResponse response = new DumpEntitiesResponse();
    response.setTablets(Arrays.asList(tablet));
    ObjectNode dumpEntitiesJson = (ObjectNode) Json.toJson(response);
    when(mockNodeUIApiHelper.getRequest(endsWith(UniverseTaskBase.DUMP_ENTITIES_URL_SUFFIX)))
        .thenReturn(dumpEntitiesJson);
  }

  public void mockDBServerVersion(String oldVersion, String newVersion, int count) {
    mockDBServerVersion(oldVersion, count, newVersion, count);
  }

  public void mockDBServerVersion(String version, int count) {
    List<Optional<String>> response = new ArrayList<>();
    for (int i = 1; i < count; i++) {
      response.add(Optional.of(version));
    }
    Optional<String>[] resp = response.toArray(new Optional[0]);
    when(mockYBClient.getServerVersion(any(), anyString(), anyInt()))
        .thenReturn(Optional.of(version), resp);
  }

  public void mockDBServerVersion(
      String oldVersion, int oldVersionCount, String newVersion, int newVersionCount) {
    List<Optional<String>> response = new ArrayList<>();
    for (int i = 1; i < oldVersionCount; i++) {
      response.add(Optional.of(oldVersion));
    }
    for (int i = 0; i < newVersionCount; i++) {
      response.add(Optional.of(newVersion));
    }
    Optional<String>[] resp = response.toArray(new Optional[0]);
    when(mockYBClient.getServerVersion(any(), anyString(), anyInt()))
        .thenReturn(Optional.of(oldVersion), resp);
  }

  protected void mockLocaleCheckResponse(NodeUniverseManager mockNodeUniverseManager) {
    List<String> command = new ArrayList<>();
    command.add("locale");
    command.add("-a");
    command.add("|");
    command.add("grep");
    command.add("-E");
    command.add("-q");
    command.add("\"en_US.utf8|en_US.UTF-8\"");
    command.add("&&");
    command.add("echo");
    command.add("\"Locale is present\"");
    command.add("||");
    command.add("echo");
    command.add("\"Locale is not present\"");
    ShellResponse shellResponse2 = new ShellResponse();
    shellResponse2.message = "Command output:\\nLocale is present";
    shellResponse2.code = 0;
    when(mockNodeUniverseManager.runCommand(any(), any(), eq(command), any()))
        .thenReturn(shellResponse2);
  }

  protected void mockClockSyncResponse(NodeUniverseManager nodeUniverseManager) {
    when(mockNodeUniverseManager.runCommand(any(), any(), any()))
        .thenReturn(
            ShellResponse.create(0, ShellResponse.RUN_COMMAND_OUTPUT_PREFIX + "/usr/bin/chronyc"))
        .thenReturn(
            ShellResponse.create(
                ShellResponse.ERROR_CODE_SUCCESS,
                ShellResponse.RUN_COMMAND_OUTPUT_PREFIX
                    + "Reference ID    : A9FEA9FE (metadata.google.internal)\n"
                    + "    Stratum         : 3\n"
                    + "    Ref time (UTC)  : Mon Jun 12 16:18:24 2023\n"
                    + "    System time     : 0.000000003 seconds slow of NTP time\n"
                    + "    Last offset     : +0.000019514 seconds\n"
                    + "    RMS offset      : 0.000011283 seconds\n"
                    + "    Frequency       : 99.154 ppm slow\n"
                    + "    Residual freq   : +0.009 ppm\n"
                    + "    Skew            : 0.106 ppm\n"
                    + "    Root delay      : 0.000162946 seconds\n"
                    + "    Root dispersion : 0.000101734 seconds\n"
                    + "    Update interval : 32.3 seconds\n"
                    + "    Leap status     : Normal"));
  }

  protected void setMockLiveTabletServers(YBClient mockClient, Universe universe) {
    try {
      List<TabletServerInfo> tabletServerInfoList = new ArrayList<>();

      // Loop through both Primary cluster and RR nodes.
      for (Cluster cluster : universe.getUniverseDetails().clusters) {
        UUID clusterUuid = cluster.uuid;
        Set<NodeDetails> nodesInCluster = new HashSet<>(universe.getNodesByCluster(cluster.uuid));
        for (NodeDetails curNode : nodesInCluster) {
          TabletServerInfo.CloudInfo cloudInfo = new TabletServerInfo.CloudInfo();
          cloudInfo.setCloud(curNode.cloudInfo.cloud);
          cloudInfo.setRegion(curNode.cloudInfo.region);
          cloudInfo.setZone(curNode.cloudInfo.az);

          TabletServerInfo tserverInfo = new TabletServerInfo();
          tserverInfo.setCloudInfo(cloudInfo);
          tserverInfo.setUuid(UUID.randomUUID());
          tserverInfo.setInPrimaryCluster(cluster.clusterType.equals(ClusterType.PRIMARY));
          tserverInfo.setPlacementUuid(clusterUuid);
          tserverInfo.setPrivateRpcAddress(
              HostAndPort.fromParts(curNode.cloudInfo.private_ip, curNode.tserverRpcPort));

          tabletServerInfoList.add(tserverInfo);
        }
      }
      ListLiveTabletServersResponse listLiveTabletServersResponse =
          mock(ListLiveTabletServersResponse.class);
      when(listLiveTabletServersResponse.getTabletServers()).thenReturn(tabletServerInfoList);
      when(mockClient.listLiveTabletServers()).thenReturn(listLiveTabletServersResponse);
    } catch (Exception e) {
      fail();
    }
  }
}
