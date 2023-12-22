package db.migration.default_.common;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.cloud.PublicCloudConstants.Architecture;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.models.migrations.V_252.AWSRegionCloudInfo;
import com.yugabyte.yw.models.migrations.V_252.AzureRegionCloudInfo;
import com.yugabyte.yw.models.migrations.V_252.Customer;
import com.yugabyte.yw.models.migrations.V_252.GCPRegionCloudInfo;
import com.yugabyte.yw.models.migrations.V_252.ImageBundle;
import com.yugabyte.yw.models.migrations.V_252.ImageBundleDetails;
import com.yugabyte.yw.models.migrations.V_252.Provider;
import com.yugabyte.yw.models.migrations.V_252.ProviderDetails;
import com.yugabyte.yw.models.migrations.V_252.Region;
import io.ebean.DB;
import io.ebean.Ebean;
import io.ebean.SqlRow;
import io.ebean.SqlUpdate;
import java.sql.SQLException;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;
import org.flywaydb.core.api.migration.BaseJavaMigration;
import org.flywaydb.core.api.migration.Context;
import play.libs.Json;

@Slf4j
public class V253__GenerateImageBundle extends BaseJavaMigration {

  public static ObjectMapper objectMapper = new ObjectMapper();

  @Override
  public void migrate(Context context) throws SQLException {
    DB.execute(V253__GenerateImageBundle::generateImageBundles);
  }

  public static void generateImageBundles() {
    for (Customer customer : Customer.find.all()) {
      for (Provider provider : Provider.getAll(customer.uuid)) {
        V253__GenerateImageBundle.createDefaultImageBundlesForProvider(provider);
      }
      V253__GenerateImageBundle.createImageBundleForUniverses(customer);
    }
  }

  public static void createImageBundleForUniverses(Customer customer) {
    String universeGet =
        "SELECT universe_uuid, name, universe_details_json from universe where customer_id= :param";
    List<SqlRow> rows =
        Ebean.createSqlQuery(universeGet).setParameter("param", customer.id).findList();

    for (SqlRow row : rows) {
      // Extract data from the row using column names
      String universeDetailsString = row.getString("universe_details_json");
      String universeUUID = row.getString("universe_uuid");
      String universeName = row.getString("name");

      if (universeDetailsString != null) {
        log.debug(String.format("Populating imageBundle for universe %s", universeName));
        JsonNode universeDetailsJson = Json.parse(universeDetailsString);
        if (universeDetailsJson.isObject()) {
          boolean hasCustomImage = hasCustomImage(universeDetailsJson);
          migrateClusters(
              universeDetailsJson, universeUUID, universeName, customer, hasCustomImage);
        }
      }
    }
  }

  public static boolean hasCustomImage(JsonNode universeDetails) {
    boolean hasCustomImage = false;
    if (universeDetails.has("nodeDetailsSet")) {
      JsonNode nodeDetails = universeDetails.get("nodeDetailsSet");

      ArrayNode arrayNodeDetails = (ArrayNode) nodeDetails;
      for (JsonNode nodeDetail : arrayNodeDetails) {
        if (nodeDetail.has("machineImage")) {
          hasCustomImage = true;
        }
      }
    }
    return hasCustomImage;
  }

  public static void migrateClusters(
      JsonNode universeDetails,
      String universeUUID,
      String universeName,
      Customer customer,
      boolean hasCustomImage) {
    if (universeDetails.has("nodeDetailsSet")) {
      ArrayNode nodeDetails = (ArrayNode) universeDetails.get("nodeDetailsSet");

      ArrayNode clusters = null;
      if (universeDetails.has("clusters")) {
        clusters = (ArrayNode) universeDetails.get("clusters");
      } else {
        log.error("No cluster associated with the universe, skipping...");
        return;
      }
      ImageBundleDetails details = new ImageBundleDetails();
      Map<String, ImageBundleDetails.BundleInfo> regionsImageInfo = new HashMap<>();
      List<String> clusterVisited = new ArrayList<>();
      Provider provider = null;
      ImageBundle bundle = null;

      for (JsonNode nodeDetail : nodeDetails) {
        String placementUUID = nodeDetail.get("placementUuid").asText();
        if (placementUUID.isEmpty()) {
          log.warn(
              "Universe {} is not correctly configured. Missing placement info, skipping...",
              universeName);
          return;
        }

        if (clusterVisited.contains(placementUUID)) {
          // If the cluster associated with node is already processed continue;
          continue;
        }
        ObjectNode nodeCluster = null;
        int nodeClusterIdx = -1;
        String providerUUID = null;
        for (int i = 0; i < clusters.size(); i++) {
          JsonNode cluster = clusters.get(i);
          if (cluster.get("uuid").asText().equals(placementUUID)) {
            nodeCluster = (ObjectNode) cluster;
            nodeClusterIdx = i;
            if (!cluster.has("userIntent") || cluster.get("userIntent").isEmpty()) {
              log.debug(
                  String.format(
                      "Cluster %s exists without userIntent, can't continue",
                      cluster.get("uuid").asText()));
              continue;
            }
            providerUUID = cluster.get("userIntent").get("provider").asText();
          }
        }

        if (nodeCluster.isEmpty()) {
          log.error("Cluster associated with the universe does not exist");
          continue;
        }

        ObjectNode userIntent = (ObjectNode) nodeCluster.get("userIntent");
        if (userIntent.has("imageBundleUUID") || userIntent.get("imageBundleUUID") != null) {
          log.debug(
              "Universe {} is already associated with imageBundle, skipping...", universeName);
          return;
        }

        provider = Provider.get(customer.uuid, UUID.fromString(providerUUID));
        if (provider.getCloudCode().equals(CloudType.kubernetes)
            || provider.getCloudCode().equals(CloudType.onprem)) {
          log.debug("Universe {} is deployed with k8s/onprem provider, skipping...", universeName);
          continue;
        }

        if (hasCustomImage) {
          log.debug("Generating image bundle for the universe {}", universeUUID);
          JsonNode cloudInfo = nodeDetail.get("cloudInfo");
          if (cloudInfo != null) {
            if (!regionsImageInfo.containsKey(cloudInfo.get("region").asText())) {
              Region region = Region.getByCode(provider, cloudInfo.get("region").asText());
              ImageBundleDetails.BundleInfo regionImageInfo = new ImageBundleDetails.BundleInfo();
              Architecture arch = null;
              if (provider.getCloudCode().equals(CloudType.aws)) {
                AWSRegionCloudInfo awsRegionCloudInfo = region.details.cloudInfo.getAws();
                arch = awsRegionCloudInfo.getArch();
              }
              if (arch == null) {
                arch = Architecture.x86_64;
              }

              // Populating the architecture for the bundle.
              if (arch != null) {
                details.setArch(arch);
              }
              String ybImage = nodeDetail.get("machineImage").asText();
              if (cloudInfo.get("cloud").asText().equals(CloudType.aws.toString())) {
                regionImageInfo.setYbImage(ybImage);
                regionImageInfo.setSshUserOverride(provider.getDetails().sshUser);
                regionImageInfo.setSshPortOverride(provider.getDetails().sshPort);
              } else {
                details.setGlobalYbImage(ybImage);
              }
              regionsImageInfo.put(region.code, regionImageInfo);
            }
          }
        } else {
          bundle = ImageBundle.getDefaultForProvider(UUID.fromString(providerUUID));
        }

        if (hasCustomImage) {
          details.setRegions(regionsImageInfo);
          String imageBundleName =
              String.format(
                  "for_universe_%s_%s",
                  universeName, nodeCluster.get("clusterType").asText().toLowerCase());
          bundle = ImageBundle.create(provider, imageBundleName, details, false);
        }

        clusterVisited.add(nodeCluster.get("uuid").asText());
        if (bundle != null) {
          userIntent.put("imageBundleUUID", bundle.uuid.toString());
          nodeCluster.set("userIntent", userIntent);
        }

        clusters.set(nodeClusterIdx, nodeCluster);
      }
      ((ObjectNode) universeDetails).set("clusters", clusters);
    }

    String universeUpdate =
        "UPDATE universe SET universe_details_json = :universe_details WHERE universe_uuid = :uuid";
    SqlUpdate sqlUpdate = Ebean.createSqlUpdate(universeUpdate);
    sqlUpdate.setParameter("universe_details", Json.stringify(universeDetails));
    sqlUpdate.setParameter("uuid", UUID.fromString(universeUUID));

    // Execute the SQL update query
    int numRowsUpdated = sqlUpdate.execute();
    log.debug("Updated universe {}", universeName);
  }

  public static void createDefaultImageBundlesForProvider(Provider provider) {
    ProviderDetails providerDetails = provider.getDetails();
    List<Region> regions = provider.regions;
    if (regions.size() == 0) {
      log.debug(
          String.format(
              "Provider %s has no regions configured, skipping...", provider.uuid.toString()));
      return;
    }

    // Generate the default image bundle per provider.
    if (provider.getCloudCode().equals(CloudType.kubernetes)
        || provider.getCloudCode().equals(CloudType.onprem)) {
      return;
    }
    log.debug(String.format("Preparing imageBundle for provider %s", provider.name));

    ImageBundleDetails imageBundleDetails = new ImageBundleDetails();
    if (provider.getCloudCode().equals(CloudType.aws)) {
      // For AWS we can have region level override for AMIs.
      // As of now we allow AMIs of the same arch, therefore
      // it's safe to assume that first region will give us the correct architecture.

      Map<String, ImageBundleDetails.BundleInfo> regionsImageInfo = new HashMap<>();
      for (Region region : regions) {
        ImageBundleDetails.BundleInfo regionImageInfo = new ImageBundleDetails.BundleInfo();
        if (region.details == null || region.details.cloudInfo == null) {
          regionsImageInfo.put(region.code, regionImageInfo);
          continue;
        } else {
          AWSRegionCloudInfo awsRegionCloudInfo = region.details.cloudInfo.getAws();
          String ybImage = awsRegionCloudInfo.getYbImage();
          regionImageInfo.setYbImage(ybImage);
          if (imageBundleDetails.getArch() == null) {
            Architecture arch = awsRegionCloudInfo.getArch();
            if (arch == null) {
              arch = Architecture.x86_64;
            }
            imageBundleDetails.setArch(arch);
          }
          // As of now, we have support for same family AMIs. Therefore, it will be
          // okay to assume provider.details.sshUser as sshUserOverride for current regions.
          regionImageInfo.setSshUserOverride(providerDetails.sshUser);
          regionsImageInfo.put(region.code, regionImageInfo);
        }
      }
      imageBundleDetails.setRegions(regionsImageInfo);
    } else {
      Region region = regions.get(0);
      Architecture arch = Architecture.x86_64;

      String ybImage = null;
      if (provider.getCloudCode().equals(CloudType.gcp)) {
        GCPRegionCloudInfo gcpRegionCloudInfo = region.details.cloudInfo.getGcp();
        ybImage = gcpRegionCloudInfo.getYbImage();
      } else if (provider.getCloudCode().equals(CloudType.azu)) {
        AzureRegionCloudInfo azuRegionCloudInfo = region.details.cloudInfo.getAzu();
        ybImage = azuRegionCloudInfo.getYbImage();
      }

      if (ybImage != null) {
        imageBundleDetails.setGlobalYbImage(ybImage);
      }
      imageBundleDetails.setArch(arch);
    }
    String imageBundleName = String.format("for_provider_%s", provider.name);
    ImageBundle.create(provider, imageBundleName, imageBundleDetails, true);
  }
}
