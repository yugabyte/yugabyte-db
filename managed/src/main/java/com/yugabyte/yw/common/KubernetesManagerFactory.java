// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.config.impl.SettableRuntimeConfigFactory;

@Singleton
public class KubernetesManagerFactory {

  private final SettableRuntimeConfigFactory sRuntimeConfigFactory;

  private final RuntimeConfGetter confGetter;

  private final ShellKubernetesManager shellKubernetesManager;

  private final NativeKubernetesManager nativeKubernetesManager;

  @Inject
  public KubernetesManagerFactory(
      SettableRuntimeConfigFactory runtimeConfigFactory,
      RuntimeConfGetter confGetter,
      ShellKubernetesManager shellKubernetesManager,
      NativeKubernetesManager nativeKubernetesManager) {
    this.sRuntimeConfigFactory = runtimeConfigFactory;
    this.confGetter = confGetter;
    this.shellKubernetesManager = shellKubernetesManager;
    this.nativeKubernetesManager = nativeKubernetesManager;
  }

  public KubernetesManager getManager() {
    if (confGetter.getGlobalConf(GlobalConfKeys.useKubectl)) {
      return shellKubernetesManager;
    }
    return nativeKubernetesManager;
  }
}
