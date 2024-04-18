package com.yugabyte.yw.common.operator;

import com.google.inject.Inject;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;

public class OperatorStatusUpdaterFactory {
  private final RuntimeConfGetter confGetter;

  private KubernetesOperatorStatusUpdater kubernetesOperatorStatusUpdater;
  private NoOpOperatorStatusUpdater noOpOperatorStatusUpdater;

  @Inject
  public OperatorStatusUpdaterFactory(RuntimeConfGetter confGetter) {
    this.confGetter = confGetter;
  }

  public synchronized OperatorStatusUpdater create() {
    if (confGetter.getGlobalConf(GlobalConfKeys.KubernetesOperatorEnabled)) {
      if (kubernetesOperatorStatusUpdater == null) {
        kubernetesOperatorStatusUpdater = new KubernetesOperatorStatusUpdater(this.confGetter);
      }
      return kubernetesOperatorStatusUpdater;
    }
    if (noOpOperatorStatusUpdater == null) {
      noOpOperatorStatusUpdater = new NoOpOperatorStatusUpdater();
    }
    return noOpOperatorStatusUpdater;
  }
}
