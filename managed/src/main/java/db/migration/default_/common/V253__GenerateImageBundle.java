package db.migration.default_.common;

import com.yugabyte.yw.cloud.PublicCloudConstants.Architecture;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.models.helpers.CloudSpecificInfo;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.migrations.V_252.AWSRegionCloudInfo;
import com.yugabyte.yw.models.migrations.V_252.AzureRegionCloudInfo;
import com.yugabyte.yw.models.migrations.V_252.Customer;
import com.yugabyte.yw.models.migrations.V_252.GCPRegionCloudInfo;
import com.yugabyte.yw.models.migrations.V_252.ImageBundle;
import com.yugabyte.yw.models.migrations.V_252.ImageBundleDetails;
import com.yugabyte.yw.models.migrations.V_252.Provider;
import com.yugabyte.yw.models.migrations.V_252.ProviderDetails;
import com.yugabyte.yw.models.migrations.V_252.Region;
import com.yugabyte.yw.models.migrations.V_252.Universe;
import io.ebean.DB;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;
import org.flywaydb.core.api.migration.BaseJavaMigration;
import org.flywaydb.core.api.migration.Context;

@Slf4j
public class V253__GenerateImageBundle extends BaseJavaMigration {

  @Override
  public void migrate(Context context) {
    DB.execute(V253__GenerateImageBundle::generateImageBundles);
  }

  public static void generateImageBundles() {
    log.info("----- STARTING IMAGE BUNDLE GENERATION MIGRATION -----");

    for (Customer customer : Customer.find.all()) {
      for (Provider provider : Provider.getAll(customer.uuid)) {
        V253__GenerateImageBundle.createDefaultImageBundlesForProvider(provider);
      }

      for (Universe universe : Universe.getAllWithoutResources(customer)) {
        UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
        if (universeDetails == null) {
          continue;
        }

        boolean hasCustomImage = V253__GenerateImageBundle.hasCustomImage(universeDetails);
        V253__GenerateImageBundle.migrateClusters(universe, customer, hasCustomImage);
      }
    }
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

    ImageBundleDetails imageBundleDetails = new ImageBundleDetails();
    if (provider.getCloudCode().equals(CloudType.aws)) {
      // For AWS we can have region level override for AMIs.
      // As of now we allow AMIs of the same arch, therefore
      // it's safe to assume that first region will give us the correct architecture.

      Map<String, ImageBundleDetails.BundleInfo> regionsImageInfo = new HashMap<>();
      for (Region region : regions) {
        ImageBundleDetails.BundleInfo regionImageInfo = new ImageBundleDetails.BundleInfo();
        if (region.details == null) {
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
      log.debug(String.format("Generated Regions Mapping %s", regionsImageInfo));
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

  public static boolean hasCustomImage(UniverseDefinitionTaskParams universeDetails) {
    Set<NodeDetails> nodeDetails = universeDetails.nodeDetailsSet;
    boolean hasCustomImage = false;
    for (NodeDetails nodeDetail : nodeDetails) {
      // machineImage is set on the node detail only when we have
      // perform VM image upgrade on the universe.
      if (nodeDetail.machineImage != null) {
        hasCustomImage = true;
      }
    }

    return hasCustomImage;
  }

  public static void migrateClusters(Universe universe, Customer customer, boolean hasCustomImage) {
    UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
    Set<NodeDetails> nodeDetails = universeDetails.nodeDetailsSet;

    List<Cluster> clusters = universeDetails.clusters;
    ImageBundleDetails details = new ImageBundleDetails();
    Map<String, ImageBundleDetails.BundleInfo> regionsImageInfo = new HashMap<>();
    Provider provider = null;
    ImageBundle bundle = null;
    for (NodeDetails nodeDetail : nodeDetails) {
      // machineImage is set on the node detail only when we have
      // perform VM image upgrade on the universe.
      UUID placementUUID = nodeDetail.placementUuid;
      String providerUUID = null;
      log.debug("Retrieving provider for the given node");
      for (Cluster cluster : clusters) {
        if (cluster.uuid.equals(placementUUID)) {
          if (cluster.userIntent == null) {
            log.debug(
                String.format(
                    "Cluster %s exists without userIntent, can't continue", cluster.uuid));
            continue;
          }
          providerUUID = cluster.userIntent.provider;
        }
      }
      provider = Provider.get(customer.uuid, UUID.fromString(providerUUID));
      if (provider.getCloudCode().equals(CloudType.kubernetes)
          || provider.getCloudCode().equals(CloudType.onprem)) {
        log.debug("Skipping image bundle for k8s & onprem provider");
        continue;
      }

      if (hasCustomImage) {
        log.debug(
            String.format("Generating image bundle for the universe %s", universe.universeUUID));
        CloudSpecificInfo cloudInfo = nodeDetail.cloudInfo;
        if (!regionsImageInfo.containsKey(cloudInfo.region)) {
          Region region = Region.getByCode(provider, cloudInfo.region);
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
          String ybImage = nodeDetail.machineImage;
          if (cloudInfo.cloud.equals(CloudType.aws.toString())) {
            regionImageInfo.setYbImage(ybImage);
            regionImageInfo.setSshUserOverride(provider.details.sshUser);
            regionImageInfo.setSshPortOverride(provider.details.sshPort);
          } else {
            details.setGlobalYbImage(ybImage);
          }
          regionsImageInfo.put(region.code, regionImageInfo);
        }
      } else {
        bundle = ImageBundle.getDefaultForProvider(provider.uuid);
      }
    }

    if (hasCustomImage) {
      log.debug(String.format("Generated Region Mapping %s", regionsImageInfo));
      details.setRegions(regionsImageInfo);
      String imageBundleName = String.format("for_universe_%s", universe.name);
      bundle = ImageBundle.create(provider, imageBundleName, details, false);
    }

    if (bundle != null) {
      final UUID imageBundleUUID = bundle.uuid;

      clusters.forEach(
          (cluster) -> {
            cluster.userIntent.imageBundleUUID = imageBundleUUID;
          });
      universe.setUniverseDetails(universeDetails);
      universe.save();
    }
  }
}
