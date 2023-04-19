/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
package com.yugabyte.yw.common.config.impl;

import static com.yugabyte.yw.common.SwamperHelper.COLLECTION_LEVEL_PARAM;

import com.yugabyte.yw.common.config.RuntimeConfigChangeListener;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import io.ebean.annotation.Transactional;
import java.util.Set;
import javax.inject.Singleton;

@Singleton
public class MetricCollectionLevelListener implements RuntimeConfigChangeListener {

  public String getKeyPath() {
    return COLLECTION_LEVEL_PARAM;
  }

  @Override
  @Transactional(batchSize = 40)
  public void processGlobal() {
    Set<Universe> universes = Universe.getAllWithoutResources();
    universes.forEach(
        universe -> {
          universe.setSwamperConfigWritten(false);
          universe.save();
        });
  }

  @Override
  @Transactional(batchSize = 40)
  public void processCustomer(Customer customer) {
    Set<Universe> universes = Universe.getAllWithoutResources(customer);
    universes.forEach(
        universe -> {
          universe.setSwamperConfigWritten(false);
          universe.save();
        });
  }

  @Override
  public void processUniverse(Universe universe) {
    universe.setSwamperConfigWritten(false);
    universe.save();
  }
}
