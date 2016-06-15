// Copyright (c) Yugabyte, Inc.

import com.google.inject.AbstractModule;
import services.LocalYBClientService;
import services.YBClientService;

/**
 * This class is a Guice module that tells Guice to bind different types
 *
 * Play will automatically use any class caleld 'Module' in the root package
 */
public class Module extends AbstractModule {
  @Override
  public void configure() {
    // Bind Application Initializer
    bind(AppInit.class).asEagerSingleton();
    // Set LocalClientService as the implementation for YBClientService
    bind(YBClientService.class).to(LocalYBClientService.class);
  }
}
