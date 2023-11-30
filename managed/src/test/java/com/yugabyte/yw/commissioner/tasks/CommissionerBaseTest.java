// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks;

import static com.yugabyte.yw.common.TestHelper.testDatabase;
import static org.junit.Assert.assertEquals;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.spy;
import static org.mockito.Mockito.when;
import static play.inject.Bindings.bind;

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
import com.yugabyte.yw.common.AccessManager;
import com.yugabyte.yw.common.ApiHelper;
import com.yugabyte.yw.common.CloudQueryHelper;
import com.yugabyte.yw.common.ConfigHelper;
import com.yugabyte.yw.common.DnsManager;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.NetworkManager;
import com.yugabyte.yw.common.NodeManager;
import com.yugabyte.yw.common.NodeUIApiHelper;
import com.yugabyte.yw.common.NodeUniverseManager;
import com.yugabyte.yw.common.PlatformExecutorFactory;
import com.yugabyte.yw.common.PlatformGuiceApplicationBaseTest;
import com.yugabyte.yw.common.PrometheusConfigManager;
import com.yugabyte.yw.common.ProviderEditRestrictionManager;
import com.yugabyte.yw.common.ReleaseManager;
import com.yugabyte.yw.common.ShellKubernetesManager;
import com.yugabyte.yw.common.SwamperHelper;
import com.yugabyte.yw.common.TableManager;
import com.yugabyte.yw.common.TableManagerYb;
import com.yugabyte.yw.common.YcqlQueryExecutor;
import com.yugabyte.yw.common.YsqlQueryExecutor;
import com.yugabyte.yw.common.alerts.AlertConfigurationService;
import com.yugabyte.yw.common.alerts.AlertDefinitionService;
import com.yugabyte.yw.common.alerts.AlertService;
import com.yugabyte.yw.common.backuprestore.BackupHelper;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.common.config.impl.SettableRuntimeConfigFactory;
import com.yugabyte.yw.common.gflags.GFlagsValidation;
import com.yugabyte.yw.common.kms.EncryptionAtRestManager;
import com.yugabyte.yw.common.metrics.MetricService;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.common.supportbundle.SupportBundleComponent;
import com.yugabyte.yw.common.supportbundle.SupportBundleComponentFactory;
import com.yugabyte.yw.metrics.MetricQueryHelper;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.List;
import java.util.UUID;
import kamon.instrumentation.play.GuiceModule;
import org.junit.Before;
import org.mockito.Mockito;
import org.pac4j.play.CallbackController;
import org.pac4j.play.store.PlayCacheSessionStore;
import org.pac4j.play.store.PlaySessionStore;
import org.yb.client.GetMasterClusterConfigResponse;
import org.yb.client.YBClient;
import org.yb.master.CatalogEntityInfo;
import play.Application;
import play.Environment;
import play.inject.guice.GuiceApplicationBuilder;

public abstract class CommissionerBaseTest extends PlatformGuiceApplicationBaseTest {
  private static final int MAX_RETRY_COUNT = 2000;
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
  protected NodeUniverseManager mockNodeUniverseManager;
  protected TaskExecutor taskExecutor;
  protected EncryptionAtRestManager mockEARManager;
  protected SupportBundleComponent mockSupportBundleComponent;
  protected SupportBundleComponentFactory mockSupportBundleComponentFactory;
  protected ReleaseManager mockReleaseManager;
  protected GFlagsValidation mockGFlagsValidation;
  protected ProviderEditRestrictionManager providerEditRestrictionManager;
  protected BackupHelper mockBackupHelper;
  protected PrometheusConfigManager mockPrometheusConfigManager;

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

  @Before
  public void setUp() {
    mockConfig = spy(app.config());
    commissioner = app.injector().instanceOf(Commissioner.class);
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

    // Enable custom hooks in tests
    factory = app.injector().instanceOf(SettableRuntimeConfigFactory.class);
    confGetter = app.injector().instanceOf(RuntimeConfGetter.class);
    factory.globalRuntimeConf().setValue(ENABLE_CUSTOM_HOOKS_PATH, "true");
    factory.globalRuntimeConf().setValue(ENABLE_SUDO_PATH, "true");

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
    mockNodeUniverseManager = mock(NodeUniverseManager.class);
    mockEARManager = mock(EncryptionAtRestManager.class);
    mockSupportBundleComponent = mock(SupportBundleComponent.class);
    mockSupportBundleComponentFactory = mock(SupportBundleComponentFactory.class);
    mockGFlagsValidation = mock(GFlagsValidation.class);
    mockReleaseManager = mock(ReleaseManager.class);
    mockCloudAPIFactory = mock(CloudAPI.Factory.class);
    mockBackupHelper = mock(BackupHelper.class);
    mockPrometheusConfigManager = mock(PrometheusConfigManager.class);

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
                .overrides(bind(PlaySessionStore.class).toInstance(mockSessionStore))
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
                .overrides(bind(NodeUniverseManager.class).toInstance(mockNodeUniverseManager))
                .overrides(
                    bind(ExecutorServiceProvider.class).to(DefaultExecutorServiceProvider.class))
                .overrides(bind(EncryptionAtRestManager.class).toInstance(mockEARManager))
                .overrides(bind(GFlagsValidation.class).toInstance(mockGFlagsValidation))
                .overrides(bind(NodeUIApiHelper.class).toInstance(mockNodeUIApiHelper))
                .overrides(bind(BackupHelper.class).toInstance(mockBackupHelper))
                .overrides(
                    bind(PrometheusConfigManager.class).toInstance(mockPrometheusConfigManager))
                .overrides(bind(ReleaseManager.class).toInstance(mockReleaseManager)))
        .overrides(bind(CloudAPI.Factory.class).toInstance(mockCloudAPIFactory))
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
      e.printStackTrace();
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

  public static TaskInfo waitForTask(UUID taskUUID) throws InterruptedException {
    int numRetries = 0;
    while (numRetries < MAX_RETRY_COUNT) {
      // Here is a hack to decrease amount of accidental problems for tests using this
      // function:
      // Surrounding the next block with try {} catch {} as sometimes h2 raises NPE
      // inside the get() request. We are not afraid of such exception as the next
      // request will succeeded.
      try {
        TaskInfo taskInfo = TaskInfo.get(taskUUID);
        if (TaskInfo.COMPLETED_STATES.contains(taskInfo.getTaskState())) {
          // Also, ensure task details are set before returning.
          if (taskInfo.getDetails() != null) {
            return taskInfo;
          }
        }
      } catch (Exception e) {
      }
      Thread.sleep(100);
      numRetries++;
    }
    throw new RuntimeException(
        "WaitFor task exceeded maxRetries! Task state is " + TaskInfo.get(taskUUID).getTaskState());
  }
}
