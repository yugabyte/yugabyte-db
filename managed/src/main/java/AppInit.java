// Copyright (c) YugaByte, Inc.

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;

import com.avaje.ebean.Ebean;
import com.google.common.collect.ImmutableMap;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.cloud.AWSInitializer;
import com.yugabyte.yw.common.ConfigHelper;
import com.yugabyte.yw.common.ReleaseManager;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.InstanceType;
import com.yugabyte.yw.models.MetricConfig;
import com.yugabyte.yw.models.Provider;

import org.yaml.snakeyaml.Yaml;
import play.Application;
import play.Configuration;
import play.Environment;
import play.Logger;

import static com.yugabyte.yw.common.ConfigHelper.ConfigType.SoftwareVersion;
import static com.yugabyte.yw.common.ConfigHelper.ConfigType.YugawareMetadata;


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
    Yaml yaml = new Yaml();
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
      if (Customer.find.where().findRowCount() == 0 &&
          appConfig.getBoolean("yb.seedData", false)) {
        Logger.debug("Seed the Yugaware DB");

        List<?> all = (ArrayList<?>) yaml.load(
            application.resourceAsStream("db_seed.yml")
        );
        Ebean.saveAll(all);
      }

      // TODO: Version added to Yugaware metadata, now slowly decomission SoftwareVersion property
      Object version = yaml.load(application.resourceAsStream("version.txt"));
      configHelper.loadConfigToDB(SoftwareVersion, ImmutableMap.of("version", version));
      Map <String, Object> ywMetadata = new HashMap<String, Object>();
      // Assign a new Yugaware UUID if not already present in the DB i.e. first install
      Object ywUUID = configHelper.getConfig(YugawareMetadata).getOrDefault("yugaware_uuid", UUID.randomUUID());
      ywMetadata.put("yugaware_uuid", ywUUID);
      ywMetadata.put("version", version);
      configHelper.loadConfigToDB(YugawareMetadata, ywMetadata);

      // Initialize AWS if any of its instance types have an empty volumeDetailsList
      List<Provider> providerList = Provider.find.where().findList();
      for (Provider provider : providerList) {
        if (provider.code.equals("aws")) {
          for (InstanceType instanceType : InstanceType.findByProvider(provider)) {
            if (instanceType.instanceTypeDetails != null &&
                (instanceType.instanceTypeDetails.volumeDetailsList == null ||
                instanceType.instanceTypeDetails.volumeDetailsList.isEmpty())) {
              awsInitializer.initialize(provider.customerUUID, provider.uuid);
              break;
            }
          }
          break;
        }
      }

      // Load metrics configurations.
      Map<String, Object> configs = (HashMap<String, Object>) yaml.load(
        application.resourceAsStream("metrics.yml")
      );
      MetricConfig.loadConfig(configs);
      
      // Enter all the configuration data. This is the first thing that should be done as the other
      // init steps may depend on this data.
      configHelper.loadConfigsToDB(application);

      // Import new local releases into release metadata
      releaseManager.importLocalReleases();
    }
  }
}
