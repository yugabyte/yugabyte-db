// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.config.impl.SettableRuntimeConfigFactory;

@Singleton
public class KubernetesManagerFactory {

  private final SettableRuntimeConfigFactory runtimeConfigFactory;

  private final ShellKubernetesManager shellKubernetesManager;

  private final NativeKubernetesManager nativeKubernetesManager;

  private static final String USE_KUBECTL_PATH = "yb.use_kubectl";

  @Inject
  public KubernetesManagerFactory(
      SettableRuntimeConfigFactory runtimeConfigFactory,
      ShellKubernetesManager shellKubernetesManager,
      NativeKubernetesManager nativeKubernetesManager) {
    this.runtimeConfigFactory = runtimeConfigFactory;
    this.shellKubernetesManager = shellKubernetesManager;
    this.nativeKubernetesManager = nativeKubernetesManager;
  }

  public KubernetesManager getManager() {
    if (runtimeConfigFactory.globalRuntimeConf().getBoolean(USE_KUBECTL_PATH)) {
      return shellKubernetesManager;
    }
    return nativeKubernetesManager;
  }
}
