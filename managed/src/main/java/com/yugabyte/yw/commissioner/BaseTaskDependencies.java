/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
package com.yugabyte.yw.commissioner;

import com.typesafe.config.Config;
import com.yugabyte.yw.common.ConfigHelper;
import com.yugabyte.yw.common.NodeManager;
import com.yugabyte.yw.common.PlatformExecutorFactory;
import com.yugabyte.yw.common.RestoreManagerYb;
import com.yugabyte.yw.common.TableManager;
import com.yugabyte.yw.common.TableManagerYb;
import com.yugabyte.yw.common.alerts.AlertConfigurationService;
import com.yugabyte.yw.common.backuprestore.BackupHelper;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.common.gflags.AutoFlagUtil;
import com.yugabyte.yw.common.metrics.MetricService;
import com.yugabyte.yw.common.services.YBClientService;
import javax.inject.Inject;
import javax.inject.Singleton;
import lombok.Getter;
import lombok.RequiredArgsConstructor;
import play.Application;

@Singleton
@Getter
@RequiredArgsConstructor(onConstructor_ = {@Inject})
public class BaseTaskDependencies {
  private final Application application;
  private final play.Environment environment;
  private final Config config;
  private final ConfigHelper configHelper;
  private final RuntimeConfigFactory runtimeConfigFactory;
  private final RuntimeConfGetter confGetter;
  private final MetricService metricService;
  private final AlertConfigurationService alertConfigurationService;
  private final YBClientService ybService;
  private final RestoreManagerYb restoreManagerYb;
  private final TableManager tableManager;
  private final TableManagerYb tableManagerYb;
  private final PlatformExecutorFactory executorFactory;
  private final TaskExecutor taskExecutor;
  private final HealthChecker healthChecker;
  private final NodeManager nodeManager;
  private final BackupHelper backupHelper;
  private final AutoFlagUtil autoFlagUtil;
  private final Commissioner commissioner;
}
