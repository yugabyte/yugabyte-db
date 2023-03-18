// Copyright (c) YugaByte, Inc.

package db.migration.default_.common;

import java.sql.Connection;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.collect.ImmutableList;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.controllers.handlers.CloudProviderHandler;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.helpers.CloudInfoInterface;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.helpers.provider.GCPCloudInfo;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;

import io.ebean.Ebean;
import play.libs.Json;

import org.flywaydb.core.api.migration.jdbc.BaseJdbcMigration;

public class V231__ProviderDetailsPersist extends BaseJdbcMigration {

  @Override
  public void migrate(Connection connection) throws Exception {
    Ebean.execute(V231__ProviderDetailsPersist::migrateConfigToDetails);
  }

  public static void migrateConfigToDetails() {
    for (Customer customer : Customer.getAll()) {
      for (Provider provider : Provider.getAll(customer.uuid)) {
        Map<String, String> config = provider.config;
        if (config == null) {
          continue;
        }
        Map<String, String> providerConfig = new HashMap<>(config);

        // Massage the config to be stored in newer format.
        if (provider.getCloudCode().equals(CloudType.gcp)) {
          providerConfig = V231__ProviderDetailsPersist.translateGCPConfigToDetails(config);
        }

        for (Region region : provider.regions) {
          V231__ProviderDetailsPersist.migrateRegionDetails(region);
          for (AvailabilityZone az : region.zones) {
            CloudInfoInterface.setCloudProviderInfoFromConfig(az, az.config);
            az.save();
          }
          region.save();
        }

        CloudInfoInterface.setCloudProviderInfoFromConfig(provider, providerConfig);
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
    if (region.details != null) {
      if (region.details.sg_id != null) {
        region.setSecurityGroupId(region.details.sg_id);
      }
      if (region.details.vnet != null) {
        region.setVnetName(region.details.vnet);
      }
      if (region.details.arch != null) {
        region.setArchitecture(region.details.arch);
      }
    }
  }

  private static Map<String, String> translateGCPConfigToDetails(Map<String, String> config) {

    Map<String, String> modifiedConfigMap = new HashMap<>();
    final Map<String, String> oldToNewConfigKeyMap =
        new HashMap<String, String>() {
          {
            put("project_id", "host_project_id");
            put("GOOGLE_APPLICATION_CREDENTIALS", "config_file_path");
            put("CUSTOM_GCE_NETWORK", "network");
            put(CloudProviderHandler.YB_FIREWALL_TAGS, CloudProviderHandler.YB_FIREWALL_TAGS);
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

    if (provider.details == null) {
      return;
    }
    GCPCloudInfo gcpCloudInfo = provider.details.cloudInfo.gcp;
    if (gcpCloudInfo == null) {
      return;
    }
    gcpCloudInfo.setGceApplicationCredentials(gcpCredentials);
  }
}
