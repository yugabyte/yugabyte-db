// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.cloud;

import com.google.inject.Inject;
import com.yugabyte.yw.common.ApiHelper;
import com.yugabyte.yw.common.CloudQueryHelper;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.mvc.Result;

import java.util.UUID;

public abstract class AbstractInitializer {
  public static final Logger LOG = LoggerFactory.getLogger(AbstractInitializer.class);

  @Inject
  ApiHelper apiHelper;

  @Inject
  CloudQueryHelper cloudQueryHelper;

  public abstract Result initialize(UUID customerUUID, UUID providerUUID);
}
