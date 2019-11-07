// Copyright (c) YugaByte, Inc.

import com.google.inject.AbstractModule;
import com.yugabyte.yw.cloud.AWSInitializer;
import com.yugabyte.yw.commissioner.HealthChecker;
import com.yugabyte.yw.commissioner.CallHome;
import com.yugabyte.yw.commissioner.SetUniverseKey;
import com.yugabyte.yw.common.*;
import com.yugabyte.yw.metrics.MetricQueryHelper;
import com.yugabyte.yw.common.services.LocalYBClientService;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.scheduler.Scheduler;
import play.Configuration;
import play.Environment;

import com.yugabyte.yw.common.kms.EncryptionAtRestManager;
import com.yugabyte.yw.common.kms.util.EncryptionAtRestUniverseKeyCache;

/**
 * This class is a Guice module that tells Guice to bind different types
 *
 * Play will automatically use any class called 'Module' in the root package
 */
public class Module extends AbstractModule {

  private final Environment environment;
  private final Configuration config;

  public Module(Environment environment, Configuration config) {
    this.environment = environment;
    this.config = config;
  }

  @Override
  public void configure() {
    // Bind Application Initializer
    bind(AppInit.class).asEagerSingleton();
    bind(ConfigHelper.class).asEagerSingleton();
    // Set LocalClientService as the implementation for YBClientService
    bind(YBClientService.class).to(LocalYBClientService.class);

    // We only needed to bind below ones for Platform mode.
    if (config.getString("yb.mode", "PLATFORM").equals("PLATFORM")) {
      bind(SwamperHelper.class).asEagerSingleton();
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
      bind(HealthChecker.class).asEagerSingleton();
      bind(EncryptionAtRestManager.class).asEagerSingleton();
      bind(EncryptionAtRestUniverseKeyCache.class).asEagerSingleton();
      bind(SetUniverseKey.class).asEagerSingleton();
    }
  }
}
