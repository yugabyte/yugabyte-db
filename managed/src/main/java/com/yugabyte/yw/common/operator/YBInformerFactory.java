// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.operator;

import com.google.inject.Inject;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import io.fabric8.kubernetes.api.model.HasMetadata;
import io.fabric8.kubernetes.client.KubernetesClient;
import io.fabric8.kubernetes.client.informers.SharedIndexInformer;
import io.fabric8.kubernetes.client.informers.SharedInformerFactory;
import java.util.HashMap;
import java.util.Map;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class YBInformerFactory {
  private final RuntimeConfGetter confGetter;
  private Long resync = 60 * 1000L;

  private Map<String, SharedIndexInformer<?>> informers = new HashMap<>();

  @Inject
  public YBInformerFactory(RuntimeConfGetter confGetter) {
    this.confGetter = confGetter;
  }

  // Get a shared index informer for the given Custom Resource, either watching on a specific
  // namespace or on all namespaces.
  // Any informer returned will already be started - `run` function called.
  public <T extends HasMetadata> SharedIndexInformer<T> getSharedIndexInformer(
      Class<T> type, KubernetesClient client) {
    SharedIndexInformer<?> cachedInformer = informers.getOrDefault(type.getName(), null);
    SharedIndexInformer<T> informer = null;
    if (cachedInformer != null) {
      try {
        informer = (SharedIndexInformer<T>) cachedInformer;
        if (informer.isRunning()) {
          log.trace(
              "informer for {} already created and running, returning cached instance",
              type.getName());
          return informer;
        }
        log.info("cached informer is stopped, creating a new one");
      } catch (ClassCastException ex) {
        log.warn("found cached informer, but could not cast it. Recreating", ex);
      }
    }

    String namespace = confGetter.getGlobalConf(GlobalConfKeys.KubernetesOperatorNamespace);
    if (namespace == null || namespace.isEmpty()) {
      SharedInformerFactory factory = client.informers();
      informer = factory.sharedIndexInformerFor(type, resync);
      informer.run();
    } else {
      informer = client.resources(type).inNamespace(namespace).runnableInformer(resync);
    }
    informers.put(type.getName(), informer);
    informer.run();
    return informer;
  }
}
