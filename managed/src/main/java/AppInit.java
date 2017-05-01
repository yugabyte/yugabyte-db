// Copyright (c) YugaByte, Inc.

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

import com.avaje.ebean.Ebean;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.cloud.AWSInitializer;
import com.yugabyte.yw.common.ConfigHelper;
import com.yugabyte.yw.common.ReleaseManager;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.MetricConfig;
import com.yugabyte.yw.models.Provider;

import play.Application;
import play.Configuration;
import play.Environment;
import play.Logger;
import play.libs.Yaml;


/**
 * We will use this singleton to do actions specific to the app environment, like
 * db seed etc.
 */
@Singleton
public class AppInit {

  @Inject
  public AppInit(Environment environment, Application application,
                 ConfigHelper configHelper, ReleaseManager releaseManager,
                 AWSInitializer awsInitializer) {
    Logger.info("Yugaware Application has started");
    Configuration appConfig = application.configuration();
    String devopsHome = appConfig.getString("yb.devops.home");
    String storagePath = appConfig.getString("yb.storage.path");

    if (!environment.isTest()) {
      if (devopsHome == null || devopsHome.length() == 0) {
        throw new RuntimeException("yb.devops.home is not set in application.conf");
      }

      if (storagePath == null || storagePath.length() == 0) {
        throw new RuntimeException(("yb.storage.path is not set in application.conf"));
      }

      // Check if we have provider data, if not, we need to seed the database
      if (Provider.find.where().findRowCount() == 0 &&
          appConfig.getBoolean("yb.seedData", false)) {
        Logger.debug("Seed the Yugaware DB");

        List<?> all = (ArrayList<?>) Yaml.load(
            application.resourceAsStream("db_seed.yml"),
            application.classloader()
        );
        Ebean.saveAll(all);
      }

      // Initialize AWS
      List<Provider> providerList = Provider.find.where().findList();
      for (Provider provider : providerList) {
        if (provider.code.equals("aws")) {
          awsInitializer.initialize(provider.customerUUID, provider.uuid);
          break;
        }
      }

      // Load metrics configurations.
      Map<String, Object> configs = (HashMap<String, Object>) Yaml.load(
        application.resourceAsStream("metrics.yml"),
        application.classloader()
      );
      MetricConfig.loadConfig(configs);
      
      // Enter all the configuration data. This is the first thing that should be done as the other
      // init steps may depend on this data.
      configHelper.loadConfigsToDB(application);
      releaseManager.loadReleasesToDB();
    }
  }
}
