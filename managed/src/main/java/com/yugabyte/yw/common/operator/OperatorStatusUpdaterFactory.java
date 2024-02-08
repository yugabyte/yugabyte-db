package com.yugabyte.yw.common.operator;

import com.google.inject.Inject;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;

public class OperatorStatusUpdaterFactory {
  private final RuntimeConfGetter confGetter;

  @Inject
  public OperatorStatusUpdaterFactory(RuntimeConfGetter confGetter) {
    this.confGetter = confGetter;
  }

  public OperatorStatusUpdater create() {
    if (confGetter.getGlobalConf(GlobalConfKeys.KubernetesOperatorEnabled)) {
      return new KubernetesOperatorStatusUpdater(confGetter);
    }
    return new NoOpOperatorStatusUpdater();
  }
}
