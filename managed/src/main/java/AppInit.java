// Copyright (c) YugaByte, Inc.

import java.util.ArrayList;
import java.util.List;

import com.avaje.ebean.Ebean;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.cloud.AWSInitializer;
import com.yugabyte.yw.models.Provider;

import play.Application;
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
  public AppInit(Environment environment, Application application) {
    Logger.info("Yugaware Application has started");
    if (!environment.isTest()) {
      // Check if we have provider data, if not, we need to see the database
      if (Provider.find.where().findRowCount() == 0) {
        Logger.debug("Seed the Yugaware DB");
        List<?> all = (ArrayList<?>) Yaml.load(
            application.resourceAsStream("db_seed.yml"),
            application.classloader()
        );
        Ebean.saveAll(all);

      // Enter all the configuration data. This is the first thing that should be done as the other
      // init steps may depend on this data.
      com.yugabyte.yw.common.Configuration.initializeDB();

      // Initialize the cloud engine.
      AWSInitializer aws = new AWSInitializer();
      aws.run();
      }
    }
  }
}
