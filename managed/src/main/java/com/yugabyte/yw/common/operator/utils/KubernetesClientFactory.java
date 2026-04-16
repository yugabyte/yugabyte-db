package com.yugabyte.yw.common.operator.utils;

import io.fabric8.kubernetes.client.Config;
import io.fabric8.kubernetes.client.KubernetesClient;
import io.fabric8.kubernetes.client.KubernetesClientBuilder;

public class KubernetesClientFactory {
  public KubernetesClient getKubernetesClientWithConfig(Config config) {
    return new KubernetesClientBuilder().withConfig(config).build();
  }
}
