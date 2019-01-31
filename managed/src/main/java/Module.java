// Copyright (c) YugaByte, Inc.

import com.google.inject.AbstractModule;
import com.yugabyte.yw.cloud.AWSInitializer;
import com.yugabyte.yw.commissioner.HealthChecker;
import com.yugabyte.yw.commissioner.CallHome;
import com.yugabyte.yw.common.*;
import com.yugabyte.yw.metrics.MetricQueryHelper;
import com.yugabyte.yw.common.services.LocalYBClientService;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.scheduler.Scheduler;

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
    bind(HealthChecker.class).asEagerSingleton();
    bind(HealthManager.class).asEagerSingleton();
    bind(NodeManager.class).asEagerSingleton();
    bind(MetricQueryHelper.class).asEagerSingleton();
    bind(ShellProcessHandler.class).asEagerSingleton();
    bind(NetworkManager.class).asEagerSingleton();
    bind(AccessManager.class).asEagerSingleton();
    bind(ReleaseManager.class).asEagerSingleton();
    bind(TemplateManager.class).asEagerSingleton();
    bind(AWSInitializer.class).asEagerSingleton();
    bind(KubernetesManager.class).asEagerSingleton();
    bind(CallHome.class).asEagerSingleton();
    bind(Scheduler.class).asEagerSingleton();
  }
}
