// Copyright (c) YugaByte, Inc.

package db.migration.default_.common;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.collect.ImmutableList;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.common.CloudProviderHelper;
import com.yugabyte.yw.models.helpers.provider.GCPCloudInfo;
import com.yugabyte.yw.models.migrations.V231.AvailabilityZone;
import com.yugabyte.yw.models.migrations.V231.CloudInfoInterface_Clone;
import com.yugabyte.yw.models.migrations.V231.Customer;
import com.yugabyte.yw.models.migrations.V231.Provider;
import com.yugabyte.yw.models.migrations.V231.Region;
import io.ebean.DB;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import org.flywaydb.core.api.migration.BaseJavaMigration;
import org.flywaydb.core.api.migration.Context;
import play.libs.Json;

public class V231__ProviderDetailsPersist extends BaseJavaMigration {

  @Override
  public void migrate(Context context) {
    DB.execute(V231__ProviderDetailsPersist::migrateConfigToDetails);
  }

  public static void migrateConfigToDetails() {
    for (Customer customer : Customer.find.all()) {
      for (Provider provider : Provider.getAll(customer.getUuid())) {
        Map<String, String> config = provider.getConfig();
        if (config == null) {
          continue;
        }
        Map<String, String> providerConfig = new HashMap<>(config);

        // Massage the config to be stored in newer format.
        if (provider.getCloudCode().equals(CloudType.gcp)) {
          providerConfig = V231__ProviderDetailsPersist.translateGCPConfigToDetails(config);
        }

        for (Region region : provider.getRegions()) {
          V231__ProviderDetailsPersist.migrateRegionDetails(region);
          for (AvailabilityZone az : region.getZones()) {
            CloudInfoInterface_Clone.setCloudProviderInfoFromConfig(az, az.getConfig());
            az.save();
          }
          region.save();
        }

        CloudInfoInterface_Clone.setCloudProviderInfoFromConfig(provider, providerConfig);
        provider.save();
        if (provider.getCloudCode().equals(CloudType.gcp)) {
          V231__ProviderDetailsPersist.populateGCPCredential(provider, config);
        }
        provider.save();
      }
    }
  }

  private static void migrateRegionDetails(Region region) {
    if (region.ybImage != null) {
      region.setYbImage(region.ybImage);
    }
    if (region.getDetails() != null) {
      if (region.getDetails().sg_id != null) {
        region.setSecurityGroupId(region.getDetails().sg_id);
      }
      if (region.getDetails().vnet != null) {
        region.setVnetName(region.getDetails().vnet);
      }
      if (region.getDetails().arch != null) {
        region.setArchitecture(region.getDetails().arch);
      }
    }
  }

  private static Map<String, String> translateGCPConfigToDetails(Map<String, String> config) {

    Map<String, String> modifiedConfigMap = new HashMap<>();
    final Map<String, String> oldToNewConfigKeyMap =
        new HashMap<String, String>() {
          {
            put("GCE_HOST_PROJECT", "host_project_id");
            put("project_id", "project_id");
            put("GOOGLE_APPLICATION_CREDENTIALS", "config_file_path");
            put("CUSTOM_GCE_NETWORK", "network");
            put(CloudProviderHelper.YB_FIREWALL_TAGS, CloudProviderHelper.YB_FIREWALL_TAGS);
          }
        };

    for (Map.Entry<String, String> entry : oldToNewConfigKeyMap.entrySet()) {
      if (config.containsKey(entry.getKey())) {
        modifiedConfigMap.put(entry.getValue(), config.get(entry.getKey()));
      }
    }
    return modifiedConfigMap;
  }

  private static void populateGCPCredential(Provider provider, Map<String, String> config) {
    List<String> credKeys =
        ImmutableList.of(
            "client_email",
            "project_id",
            "auth_provider_x509_cert_url",
            "auth_uri",
            "client_id",
            "client_x509_cert_url",
            "private_key",
            "private_key_id",
            "token_uri",
            "type");
    ObjectMapper mapper = Json.mapper();
    ObjectNode gcpCredentials = mapper.createObjectNode();

    for (String key : credKeys) {
      if (config.containsKey(key)) {
        gcpCredentials.put(key, config.get(key));
      }
    }

    if (provider.getDetails() == null) {
      return;
    }
    GCPCloudInfo gcpCloudInfo = provider.getDetails().getCloudInfo().gcp;
    if (gcpCloudInfo == null) {
      return;
    }
    gcpCloudInfo.setGceApplicationCredentials(gcpCredentials);
  }
}
