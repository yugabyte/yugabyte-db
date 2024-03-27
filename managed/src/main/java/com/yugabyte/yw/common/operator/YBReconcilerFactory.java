package com.yugabyte.yw.common.operator;

import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.TaskExecutor;
import com.yugabyte.yw.common.CustomerTaskManager;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.controllers.handlers.CloudProviderHandler;
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
        customerTaskManager);
  }
}
