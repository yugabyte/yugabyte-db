package com.yugabyte.yw.common.operator;

import com.google.inject.Inject;
import com.yugabyte.yw.common.KubernetesManagerFactory;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.operator.utils.OperatorUtils;

public class OperatorStatusUpdaterFactory {
  private final RuntimeConfGetter confGetter;
  private final OperatorUtils operatorUtils;
  private final KubernetesManagerFactory kubernetesManagerFactory;

  private KubernetesOperatorStatusUpdater kubernetesOperatorStatusUpdater;
  private NoOpOperatorStatusUpdater noOpOperatorStatusUpdater;

  @Inject
  public OperatorStatusUpdaterFactory(
      RuntimeConfGetter confGetter,
      OperatorUtils operatorUtils,
      KubernetesManagerFactory kubernetesManagerFactory) {
    this.confGetter = confGetter;
    this.operatorUtils = operatorUtils;
    this.kubernetesManagerFactory = kubernetesManagerFactory;
  }

  public synchronized OperatorStatusUpdater create() {
    if (confGetter.getGlobalConf(GlobalConfKeys.KubernetesOperatorEnabled)) {
      if (kubernetesOperatorStatusUpdater == null) {
        kubernetesOperatorStatusUpdater =
            new KubernetesOperatorStatusUpdater(
                this.confGetter, this.operatorUtils, this.kubernetesManagerFactory);
      }
      return kubernetesOperatorStatusUpdater;
    }
    if (noOpOperatorStatusUpdater == null) {
      noOpOperatorStatusUpdater = new NoOpOperatorStatusUpdater();
    }
    return noOpOperatorStatusUpdater;
  }
}
