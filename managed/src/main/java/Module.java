// Copyright (c) YugaByte, Inc.

import com.google.inject.AbstractModule;
import com.yugabyte.yw.cloud.AWSInitializer;
import com.yugabyte.yw.common.AccessManager;
import com.yugabyte.yw.common.ConfigHelper;
import com.yugabyte.yw.common.HealthManager;
import com.yugabyte.yw.common.NetworkManager;
import com.yugabyte.yw.common.NodeManager;
import com.yugabyte.yw.common.ReleaseManager;
import com.yugabyte.yw.common.ShellProcessHandler;
import com.yugabyte.yw.common.SwamperHelper;
import com.yugabyte.yw.common.TemplateManager;
import com.yugabyte.yw.metrics.MetricQueryHelper;
import com.yugabyte.yw.common.services.LocalYBClientService;
import com.yugabyte.yw.common.services.YBClientService;

/**
 * This class is a Guice module that tells Guice to bind different types
 *
 * Play will automatically use any class called 'Module' in the root package
 */
public class Module extends AbstractModule {
  @Override
  public void configure() {
    // Bind Application Initializer
    bind(AppInit.class).asEagerSingleton();
    bind(ConfigHelper.class).asEagerSingleton();
    bind(SwamperHelper.class).asEagerSingleton();
    // Set LocalClientService as the implementation for YBClientService
    bind(YBClientService.class).to(LocalYBClientService.class);

    bind(HealthManager.class).asEagerSingleton();
    bind(NodeManager.class).asEagerSingleton();
    bind(MetricQueryHelper.class).asEagerSingleton();
    bind(ShellProcessHandler.class).asEagerSingleton();
    bind(NetworkManager.class).asEagerSingleton();
    bind(AccessManager.class).asEagerSingleton();
    bind(ReleaseManager.class).asEagerSingleton();
    bind(TemplateManager.class).asEagerSingleton();
    bind(AWSInitializer.class).asEagerSingleton();
  }
}
