package com.yugabyte.yw.common.operator;

import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.TaskExecutor;
import com.yugabyte.yw.common.CustomerTaskManager;
import com.yugabyte.yw.common.ValidatingFormFactory;
import com.yugabyte.yw.common.backuprestore.BackupHelper;
import com.yugabyte.yw.common.backuprestore.ScheduleTaskHelper;
import com.yugabyte.yw.common.backuprestore.ybc.YbcManager;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.dr.DrConfigHelper;
import com.yugabyte.yw.common.operator.utils.OperatorUtils;
import com.yugabyte.yw.common.pitr.PitrConfigHelper;
import com.yugabyte.yw.controllers.handlers.CloudProviderHandler;
import com.yugabyte.yw.controllers.handlers.UniverseActionsHandler;
import com.yugabyte.yw.controllers.handlers.UniverseCRUDHandler;
import com.yugabyte.yw.controllers.handlers.UpgradeUniverseHandler;
import io.fabric8.kubernetes.client.KubernetesClient;

public class YBReconcilerFactory {
  @Inject private YBInformerFactory informerFactory;
  @Inject private RuntimeConfGetter confGetter;
  @Inject private UniverseCRUDHandler universeCRUDHandler;
  @Inject private UpgradeUniverseHandler upgradeUniverseHandler;
  @Inject private CloudProviderHandler cloudProviderHandler;
  @Inject private TaskExecutor taskExecutor;
  @Inject private KubernetesOperatorStatusUpdater statusUpdater;
  @Inject private CustomerTaskManager customerTaskManager;
  @Inject private OperatorUtils operatorUtils;
  @Inject private UniverseActionsHandler universeActionsHandler;
  @Inject private YbcManager ybcManager;
  @Inject private BackupHelper backupHelper;
  @Inject private PitrConfigHelper pitrConfigHelper;
  @Inject private DrConfigHelper drConfigHelper;
  @Inject private ValidatingFormFactory formFactory;
  @Inject private ScheduleTaskHelper scheduleTaskHelper;

  public YBUniverseReconciler getYBUniverseReconciler(KubernetesClient client) {
    String namespace = confGetter.getGlobalConf(GlobalConfKeys.KubernetesOperatorNamespace);
    return new YBUniverseReconciler(
        client,
        informerFactory,
        namespace,
        universeCRUDHandler,
        upgradeUniverseHandler,
        cloudProviderHandler,
        taskExecutor,
        statusUpdater,
        confGetter,
        customerTaskManager,
        operatorUtils,
        universeActionsHandler,
        ybcManager);
  }

  public ScheduledBackupReconciler getScheduledBackupReconciler(KubernetesClient client) {
    String namespace = confGetter.getGlobalConf(GlobalConfKeys.KubernetesOperatorNamespace);
    return new ScheduledBackupReconciler(
        backupHelper,
        formFactory,
        namespace,
        operatorUtils,
        client,
        informerFactory,
        scheduleTaskHelper);
  }

  public YBProviderReconciler getYBProviderReconciler(KubernetesClient client) {
    String namespace = confGetter.getGlobalConf(GlobalConfKeys.KubernetesOperatorNamespace);
    return new YBProviderReconciler(
        client, informerFactory, namespace, operatorUtils, cloudProviderHandler);
  }

  public PitrConfigReconciler getPitrConfigReconciler(KubernetesClient client) {
    String namespace = confGetter.getGlobalConf(GlobalConfKeys.KubernetesOperatorNamespace);
    return new PitrConfigReconciler(
        pitrConfigHelper, formFactory, namespace, operatorUtils, client, informerFactory);
  }

  public DrConfigReconciler getDrConfigReconciler(KubernetesClient client) {
    String namespace = confGetter.getGlobalConf(GlobalConfKeys.KubernetesOperatorNamespace);
    return new DrConfigReconciler(
        drConfigHelper, namespace, operatorUtils, client, informerFactory);
  }
}
