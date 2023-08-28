package db.migration.default_.common;

import static com.yugabyte.yw.common.TestHelper.createTempFile;
import static org.junit.Assert.assertEquals;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.when;

import com.google.common.collect.ImmutableList;
import com.yugabyte.yw.cloud.PublicCloudConstants.StorageType;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.common.TestHelper;
import com.yugabyte.yw.forms.CertificateParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.CertificateInfo;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.ImageBundle;
import com.yugabyte.yw.models.ImageBundleDetails;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CloudSpecificInfo;
import com.yugabyte.yw.models.helpers.DeviceInfo;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import java.io.IOException;
import java.security.NoSuchAlgorithmException;
import java.util.Date;
import java.util.Map;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;

public class V253__GenerateImageBundleTest extends FakeDBApplication {

  protected Customer defaultCustomer;

  protected static final String CERT_CONTENTS =
      "-----BEGIN CERTIFICATE-----\n"
          + "MIIDEjCCAfqgAwIBAgIUEdzNoxkMLrZCku6H1jQ4pUgPtpQwDQYJKoZIhvcNAQEL\n"
          + "BQAwLzERMA8GA1UECgwIWXVnYWJ5dGUxGjAYBgNVBAMMEUNBIGZvciBZdWdhYnl0\n"
          + "ZURCMB4XDTIwMTIyMzA3MjU1MVoXDTIxMDEyMjA3MjU1MVowLzERMA8GA1UECgwI\n"
          + "WXVnYWJ5dGUxGjAYBgNVBAMMEUNBIGZvciBZdWdhYnl0ZURCMIIBIjANBgkqhkiG\n"
          + "9w0BAQEFAAOCAQ8AMIIBCgKCAQEAuLPcCR1KpVSs3B2515xNAR8ntfhOM5JjLl6Y\n"
          + "WjqoyRQ4wiOg5fGQpvjsearpIntr5t6uMevpzkDMYY4U21KbIW8Vvg/kXiASKMmM\n"
          + "W4ToH3Q0NfgLUNb5zJ8df3J2JZ5CgGSpipL8VPWsuSZvrqL7V77n+ndjMTUBNf57\n"
          + "eW4VjzYq+YQhyloOlXtlfWT6WaAsoaVOlbU0GK4dE2rdeo78p2mS2wQZSBDXovpp\n"
          + "0TX4zhT6LsJaRBZe49GE4SMkxz74alK1ezRvFcrPiNKr5NOYYG9DUUqFHWg47Bmw\n"
          + "KbiZ5dLdyxgWRDbanwl2hOMfExiJhHE7pqgr8XcizCiYuUzlDwIDAQABoyYwJDAO\n"
          + "BgNVHQ8BAf8EBAMCAuQwEgYDVR0TAQH/BAgwBgEB/wIBATANBgkqhkiG9w0BAQsF\n"
          + "AAOCAQEAVI3NTJVNX4XTcVAxXXGumqCbKu9CCLhXxaJb+J8YgmMQ+s9lpmBuC1eB\n"
          + "38YFdSEG9dKXZukdQcvpgf4ryehtvpmk03s/zxNXC5237faQQqejPX5nm3o35E3I\n"
          + "ZQqN3h+mzccPaUvCaIlvYBclUAt4VrVt/W66kLFPsfUqNKVxm3B56VaZuQL1ZTwG\n"
          + "mrIYBoaVT/SmEeIX9PNjlTpprDN/oE25fOkOxwHyI9ydVFkMCpBNRv+NisQN9c+R\n"
          + "/SBXfs+07aqFgrGTte6/I4VZ/6vz2cWMwZU+TUg/u0fc0Y9RzOuJrZBV2qPAtiEP\n"
          + "YvtLjmJF//b3rsty6NFIonSVgq6Nqw==\n"
          + "-----END CERTIFICATE-----\n";

  @Before
  public void setup() {
    defaultCustomer = ModelFactory.testCustomer();
    when(mockCloudQueryHelper.getDefaultImage(any(Region.class), any()))
        .thenReturn("test_image_id");
  }

  @Test
  public void generateImageBundleAWSProvider() {
    Provider defaultProvider = ModelFactory.awsProvider(defaultCustomer);
    Region region = Region.create(defaultProvider, "us-west-1", "us-west-1", "us-west-1-image");

    V253__GenerateImageBundle.generateImageBundles();
    defaultProvider.refresh();
    ImageBundle defaultImageBundle = ImageBundle.getDefaultForProvider(defaultProvider.getUuid());

    assertEquals(1, defaultProvider.getImageBundles().size());
    assertEquals(defaultImageBundle.getUuid(), defaultProvider.getImageBundles().get(0).getUuid());
    assertEquals(
        String.format("for_provider_%s", defaultProvider.getName()),
        defaultProvider.getImageBundles().get(0).getName());
    Map<String, ImageBundleDetails.BundleInfo> regionsBundleInfo =
        defaultProvider.getImageBundles().get(0).getDetails().getRegions();
    assertEquals("us-west-1-image", regionsBundleInfo.get(region.getCode()).getYbImage());
  }

  @Test
  public void generateImageBundleGCPProvider() {
    Provider defaultProvider = ModelFactory.gcpProvider(defaultCustomer);
    Region region = Region.create(defaultProvider, "us-west1", "us-west1", "test_image_id");

    V253__GenerateImageBundle.generateImageBundles();
    defaultProvider.refresh();

    assertEquals(1, defaultProvider.getImageBundles().size());
    assertEquals(
        String.format("for_provider_%s", defaultProvider.getName()),
        defaultProvider.getImageBundles().get(0).getName());
    assertEquals(
        "test_image_id", defaultProvider.getImageBundles().get(0).getDetails().getGlobalYbImage());
  }

  @Test
  public void generateImageBundleUniverses() {
    Provider defaultProvider = ModelFactory.awsProvider(defaultCustomer);

    // Create test region and Availability Zones
    Region region = Region.create(defaultProvider, "us-west-1", "us-west-1", "yb-image-1");
    AvailabilityZone az1 = AvailabilityZone.createOrThrow(region, "az-1", "AZ 1", "subnet-1");
    AvailabilityZone az2 = AvailabilityZone.createOrThrow(region, "az-2", "AZ 2", "subnet-2");
    AvailabilityZone az3 = AvailabilityZone.createOrThrow(region, "az-3", "AZ 3", "subnet-3");
    Universe defaultUniverse;

    // Create test certificate
    UUID certUUID = UUID.randomUUID();
    Date date = new Date();
    CertificateParams.CustomCertInfo customCertInfo = new CertificateParams.CustomCertInfo();
    customCertInfo.rootCertPath = "rootCertPath";
    customCertInfo.nodeCertPath = "nodeCertPath";
    customCertInfo.nodeKeyPath = "nodeKeyPath";
    createTempFile("upgrade_task_test_ca.crt", CERT_CONTENTS);
    try {
      CertificateInfo.create(
          certUUID,
          defaultCustomer.getUuid(),
          "test",
          date,
          date,
          TestHelper.TMP_PATH + "/upgrade_task_test_ca.crt",
          customCertInfo);
    } catch (IOException | NoSuchAlgorithmException ignored) {
    }

    // Create default universe
    UniverseDefinitionTaskParams.UserIntent userIntent =
        new UniverseDefinitionTaskParams.UserIntent();
    userIntent.ybSoftwareVersion = "old-version";
    userIntent.accessKeyCode = "demo-access";
    userIntent.regionList = ImmutableList.of(region.getUuid());
    userIntent.providerType = Common.CloudType.valueOf(defaultProvider.getCode());
    userIntent.provider = defaultProvider.getUuid().toString();
    userIntent.deviceInfo = new DeviceInfo();
    userIntent.deviceInfo.volumeSize = 100;
    userIntent.deviceInfo.numVolumes = 2;

    defaultUniverse = ModelFactory.createUniverse(defaultCustomer.getId(), certUUID);

    PlacementInfo placementInfo = new PlacementInfo();
    PlacementInfoUtil.addPlacementZone(az1.getUuid(), placementInfo, 1, 1, false);
    PlacementInfoUtil.addPlacementZone(az2.getUuid(), placementInfo, 1, 1, true);
    PlacementInfoUtil.addPlacementZone(az3.getUuid(), placementInfo, 1, 1, false);
    userIntent.numNodes =
        placementInfo.cloudList.get(0).regionList.get(0).azList.stream()
            .mapToInt(p -> p.numNodesInAZ)
            .sum();

    defaultUniverse =
        Universe.saveDetails(
            defaultUniverse.getUniverseUUID(),
            ApiUtils.mockUniverseUpdater(userIntent, placementInfo, true));

    Universe.UniverseUpdater updater =
        universe -> {
          UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
          Cluster primaryCluster = universeDetails.getPrimaryCluster();
          UserIntent clusterUserIntent = primaryCluster.userIntent;
          clusterUserIntent.regionList = ImmutableList.of(region.getUuid());
          universe.setUniverseDetails(universeDetails);

          for (NodeDetails node : universeDetails.nodeDetailsSet) {
            node.nodeUuid = UUID.randomUUID();
            node.machineImage = "test-image-in-node";
            node.cloudInfo = new CloudSpecificInfo();
            node.cloudInfo.region = "us-west-1";
            node.cloudInfo.cloud = Common.CloudType.aws.toString();
          }

          clusterUserIntent.providerType = Common.CloudType.aws;
          clusterUserIntent.deviceInfo = new DeviceInfo();
          clusterUserIntent.deviceInfo.storageType = StorageType.Persistent;
        };

    defaultUniverse = Universe.saveDetails(defaultUniverse.getUniverseUUID(), updater);
    V253__GenerateImageBundle.generateImageBundles();
    defaultProvider.refresh();

    assertEquals(2, defaultProvider.getImageBundles().size());
    assertEquals(
        String.format("for_provider_%s", defaultProvider.getName()),
        defaultProvider.getImageBundles().get(0).getName());
    assertEquals(
        String.format("for_universe_%s", defaultUniverse.getName()),
        defaultProvider.getImageBundles().get(1).getName());
  }
}
