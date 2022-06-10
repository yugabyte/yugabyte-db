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
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import lombok.Getter;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

@Getter
public abstract class AbstractInitializer {

  public static final Logger LOG = LoggerFactory.getLogger(AbstractInitializer.class);

  @Inject private ApiHelper apiHelper;

  @Inject private CloudQueryHelper cloudQueryHelper;

  public abstract void initialize(UUID customerUUID, UUID providerUUID);

  @Getter
  public static class InitializationContext {

    private final Provider provider;
    private final List<Map<String, String>> availableInstances = new ArrayList<>();

    public InitializationContext(Provider provider) {
      this.provider = provider;
    }
  }
}
