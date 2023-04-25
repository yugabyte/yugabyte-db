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

import io.ebean.BackgroundExecutor;
import io.ebean.config.dbplatform.PlatformIdGenerator;
import io.ebean.config.dbplatform.h2.H2Platform;
import javax.sql.DataSource;

public class H2V2Platform extends H2Platform {

  @Override
  public PlatformIdGenerator createSequenceIdGenerator(
      BackgroundExecutor be, DataSource ds, int stepSize, String seqName) {
    return new H2V2SequenceIdGenerator(be, ds, seqName, sequenceBatchSize);
  }
}
