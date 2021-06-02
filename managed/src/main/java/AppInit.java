// Copyright (c) YugaByte, Inc.

import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.cloud.AWSInitializer;
import com.yugabyte.yw.commissioner.TaskGarbageCollector;
import com.yugabyte.yw.common.*;
import com.yugabyte.yw.common.ha.PlatformReplicationManager;
import com.yugabyte.yw.models.*;
import io.ebean.Ebean;
import io.prometheus.client.hotspot.DefaultExports;
import play.Application;
import play.Configuration;
import play.Environment;
import play.Logger;

import java.util.List;
import java.util.Map;

/** We will use this singleton to do actions specific to the app environment, like db seed etc. */
@Singleton
public class AppInit {

  @Inject
  public AppInit(
      Environment environment,
      Application application,
      ConfigHelper configHelper,
      ReleaseManager releaseManager,
      AWSInitializer awsInitializer,
      CustomerTaskManager taskManager,
      YamlWrapper yaml,
      ExtraMigrationManager extraMigrationManager,
      TaskGarbageCollector taskGC,
      PlatformReplicationManager replicationManager)
      throws ReflectiveOperationException {
    Logger.info("Yugaware Application has started");
    Configuration appConfig = application.configuration();
    String mode = appConfig.getString("yb.mode", "PLATFORM");

    if (!environment.isTest()) {
      // Check if we have provider data, if not, we need to seed the database
      if (Customer.find.query().where().findCount() == 0
          && appConfig.getBoolean("yb.seedData", false)) {
        Logger.debug("Seed the Yugaware DB");

        List<?> all =
            yaml.load(environment.resourceAsStream("db_seed.yml"), application.classloader());
        Ebean.saveAll(all);
      }

      if (mode.equals("PLATFORM")) {
        String devopsHome = appConfig.getString("yb.devops.home");
        String storagePath = appConfig.getString("yb.storage.path");
        if (devopsHome == null || devopsHome.length() == 0) {
          throw new RuntimeException("yb.devops.home is not set in application.conf");
        }

        if (storagePath == null || storagePath.length() == 0) {
          throw new RuntimeException(("yb.storage.path is not set in application.conf"));
        }
      }

      // Initialize AWS if any of its instance types have an empty volumeDetailsList
      List<Provider> providerList = Provider.find.query().where().findList();
      for (Provider provider : providerList) {
        if (provider.code.equals("aws")) {
          for (InstanceType instanceType :
              InstanceType.findByProvider(provider, application.config())) {
            if (instanceType.instanceTypeDetails != null
                && (instanceType.instanceTypeDetails.volumeDetailsList == null)) {
              awsInitializer.initialize(provider.customerUUID, provider.uuid);
              break;
            }
          }
          break;
        }
      }

      // Load metrics configurations.
      Map<String, Object> configs =
          yaml.load(environment.resourceAsStream("metrics.yml"), application.classloader());
      MetricConfig.loadConfig(configs);

      // Enter all the configuration data. This is the first thing that should be
      // done as the other init steps may depend on this data.
      configHelper.loadConfigsToDB(application);
      configHelper.loadSoftwareVersiontoDB(application);

      // Run and delete any extra migrations.
      for (ExtraMigration m : ExtraMigration.getAll()) {
        m.run(extraMigrationManager);
      }

      // Import new local releases into release metadata
      releaseManager.importLocalReleases();

      // initialize prometheus exports
      DefaultExports.initialize();

      // Fail incomplete tasks
      taskManager.failAllPendingTasks();

      // Schedule garbage collection of old completed tasks in database.
      taskGC.start();

      // Startup platform HA.
      replicationManager.init();

      // Add checksums for all certificates that don't have a checksum.
      CertificateHelper.createChecksums();

      Logger.info("AppInit completed");
    }
  }
}
