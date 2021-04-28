/*
 * Copyright 2019 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 *     https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.cloud;

import com.google.inject.Inject;
import com.yugabyte.yw.common.ApiHelper;
import com.yugabyte.yw.common.CloudQueryHelper;
import com.yugabyte.yw.models.Provider;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.mvc.Result;

import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.UUID;

public abstract class AbstractInitializer {
  public static final Logger LOG = LoggerFactory.getLogger(AbstractInitializer.class);

  @Inject
  ApiHelper apiHelper;

  @Inject
  CloudQueryHelper cloudQueryHelper;

  public abstract Result initialize(UUID customerUUID, UUID providerUUID);

  protected static class InitializationContext {
    final Provider provider;
    final List<Map<String, String>> availableInstances = new ArrayList<>();

    protected InitializationContext(Provider provider) {
      this.provider = provider;
    }
  }
}
