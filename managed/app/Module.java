/**
 * Copyright (c) YugaByte, Inc.
 *
 * Created by ram on 6/1/16.
 */

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
        // Set LocalClientService as the implementation for YBClientService
        bind(YBClientService.class).to(LocalYBClientService.class);
    }
}
