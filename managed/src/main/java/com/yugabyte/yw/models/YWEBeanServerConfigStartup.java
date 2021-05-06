/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.models;

import io.ebean.config.ServerConfig;
import io.ebean.event.ServerConfigStartup;
import play.libs.Json;

/**
 * Here we will modify EBean server config at startup.
 * EBeans framework will make sure that this gets executed at start.
 */
public class YWEBeanServerConfigStartup implements ServerConfigStartup {
  @Override
  public void onStart(ServerConfig serverConfig) {
    // Use same object mapper so that play.libs.Json and ebean's json serialization and
    // deserialization yields same results. Specifically FAIL_ON_UNKNOWN_PROPERTIES is
    // set to false by play.
    serverConfig.setObjectMapper(Json.mapper());
  }
}
