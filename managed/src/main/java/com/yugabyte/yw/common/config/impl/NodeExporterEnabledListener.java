/*
 * Copyright 2026 YugabyteDB, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
package com.yugabyte.yw.common.config.impl;

import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.config.RuntimeConfigChangeListener;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import io.ebean.annotation.Transactional;
import java.util.Set;
import javax.inject.Singleton;

/**
 * Marks Kubernetes universes for a target rewrite when {@code yb.kubernetes.node_exporter.enabled}
 * changes.
 */
@Singleton
public class NodeExporterEnabledListener implements RuntimeConfigChangeListener {

  public String getKeyPath() {
    return UniverseConfKeys.k8sNodeExporterEnabled.getKey();
  }

  @Override
  @Transactional(batchSize = 40)
  public void processGlobal() {
    markForTargetRewrite(Universe.getAllWithoutResources());
  }

  @Override
  @Transactional(batchSize = 40)
  public void processCustomer(Customer customer) {
    markForTargetRewrite(Universe.getAllWithoutResources(customer));
  }

  @Override
  public void processUniverse(Universe universe) {
    markForTargetRewrite(Set.of(universe));
  }

  private static void markForTargetRewrite(Set<Universe> universes) {
    universes.stream()
        .filter(Util::isKubernetesBasedUniverse)
        .forEach(
            universe -> {
              universe.setSwamperConfigWritten(false);
              universe.save();
            });
  }
}
