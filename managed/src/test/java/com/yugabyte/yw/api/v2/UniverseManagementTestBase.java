// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.api.v2;

import static com.yugabyte.yw.common.ModelFactory.newProvider;
import static com.yugabyte.yw.common.TestHelper.createTempFile;
import static org.hamcrest.MatcherAssert.assertThat;
import static org.hamcrest.Matchers.containsInAnyOrder;
import static org.hamcrest.Matchers.either;
import static org.hamcrest.Matchers.emptyString;
import static org.hamcrest.Matchers.hasEntry;
import static org.hamcrest.Matchers.is;
import static org.hamcrest.Matchers.nullValue;

import com.yugabyte.yba.v2.client.ApiClient;
import com.yugabyte.yba.v2.client.ApiException;
import com.yugabyte.yba.v2.client.Configuration;
import com.yugabyte.yba.v2.client.models.AuditLogConfig;
import com.yugabyte.yba.v2.client.models.AvailabilityZoneGFlags;
import com.yugabyte.yba.v2.client.models.CloudSpecificInfo;
import com.yugabyte.yba.v2.client.models.ClusterEditSpec;
import com.yugabyte.yba.v2.client.models.ClusterGFlags;
import com.yugabyte.yba.v2.client.models.ClusterInfo;
import com.yugabyte.yba.v2.client.models.ClusterNetworkingSpec;
import com.yugabyte.yba.v2.client.models.ClusterNetworkingSpec.EnableExposingServiceEnum;
import com.yugabyte.yba.v2.client.models.ClusterPlacementSpec;
import com.yugabyte.yba.v2.client.models.ClusterProviderEditSpec;
import com.yugabyte.yba.v2.client.models.ClusterProviderSpec;
import com.yugabyte.yba.v2.client.models.ClusterSpec;
import com.yugabyte.yba.v2.client.models.ClusterSpec.ClusterTypeEnum;
import com.yugabyte.yba.v2.client.models.ClusterStorageSpec;
import com.yugabyte.yba.v2.client.models.ClusterStorageSpec.StorageTypeEnum;
import com.yugabyte.yba.v2.client.models.CommunicationPortsSpec;
import com.yugabyte.yba.v2.client.models.EncryptionAtRestSpec;
import com.yugabyte.yba.v2.client.models.EncryptionInTransitSpec;
import com.yugabyte.yba.v2.client.models.NodeDetails;
import com.yugabyte.yba.v2.client.models.PlacementAZ;
import com.yugabyte.yba.v2.client.models.PlacementCloud;
import com.yugabyte.yba.v2.client.models.PlacementRegion;
import com.yugabyte.yba.v2.client.models.UniverseCreateSpec;
import com.yugabyte.yba.v2.client.models.UniverseCreateSpec.ArchEnum;
import com.yugabyte.yba.v2.client.models.UniverseCreateSpecYcql;
import com.yugabyte.yba.v2.client.models.UniverseCreateSpecYsql;
import com.yugabyte.yba.v2.client.models.UniverseEditSpec;
import com.yugabyte.yba.v2.client.models.UniverseInfo;
import com.yugabyte.yba.v2.client.models.UniverseLogsExporterConfig;
import com.yugabyte.yba.v2.client.models.UniverseNetworkingSpec;
import com.yugabyte.yba.v2.client.models.UniverseSpec;
import com.yugabyte.yba.v2.client.models.XClusterInfo;
import com.yugabyte.yba.v2.client.models.YCQLAuditConfig;
import com.yugabyte.yba.v2.client.models.YSQLAuditConfig;
import com.yugabyte.yba.v2.client.models.YcqlSpec;
import com.yugabyte.yba.v2.client.models.YsqlSpec;
import com.yugabyte.yw.cloud.PublicCloudConstants;
import com.yugabyte.yw.cloud.PublicCloudConstants.Architecture;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.TestHelper;
import com.yugabyte.yw.common.certmgmt.CertConfigType;
import com.yugabyte.yw.common.gflags.SpecificGFlags;
import com.yugabyte.yw.common.kms.util.EncryptionAtRestUtil;
import com.yugabyte.yw.controllers.UniverseControllerTestBase;
import com.yugabyte.yw.forms.EncryptionAtRestConfig;
import com.yugabyte.yw.forms.UniverseConfigureTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.forms.UniverseTaskParams.CommunicationPorts;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.CertificateInfo;
import com.yugabyte.yw.models.ImageBundle;
import com.yugabyte.yw.models.ImageBundleDetails;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Universe;
import java.io.IOException;
import java.security.NoSuchAlgorithmException;
import java.time.OffsetDateTime;
import java.time.ZoneOffset;
import java.time.temporal.ChronoUnit;
import java.util.ArrayList;
import java.util.Date;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import org.apache.commons.lang3.StringUtils;
import org.junit.Before;
import org.junit.Rule;
import org.junit.rules.TestName;

public class UniverseManagementTestBase extends UniverseControllerTestBase {

  protected UUID providerUuid;
  protected UUID universeUuid;
  protected UUID rootCA;
  protected UUID clientRootCA;
  protected String rootCAContents =
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
  protected String clientRootCAContents =
      "-----BEGIN CERTIFICATE-----\n"
          + "MIIDAjCCAeqgAwIBAgIGAXVCiJ4gMA0GCSqGSIb3DQEBCwUAMC4xFjAUBgNVBAMM\n"
          + "DXliLWFkbWluLXRlc3QxFDASBgNVBAoMC2V4YW1wbGUuY29tMB4XDTIwMTAxOTIw\n"
          + "MjQxMVoXDTIxMTAxOTIwMjQxMVowLjEWMBQGA1UEAwwNeWItYWRtaW4tdGVzdDEU\n"
          + "MBIGA1UECgwLZXhhbXBsZS5jb20wggEiMA0GCSqGSIb3DQEBAQUAA4IBDwAwggEK\n"
          + "AoIBAQCw8Mk+/MK0mP67ZEKL7cGyTzggau57MzTApveLfGF1Snln/Y7wGzgbskaM\n"
          + "0udz46es9HdaC/jT+PzMAAD9MCtAe5YYSL2+lmWT+WHdeJWF4XC/AVkjqj81N6OS\n"
          + "Uxio6ww0S9cAoDmF3gZlmkRwQcsruiZ1nVyQ7l+5CerQ02JwYBIYolUu/1qMruDD\n"
          + "pLsJ9LPWXPw2JsgYWyuEB5W1xEPDl6+QLTEVCFc9oN6wJOJgf0Y6OQODBrDRxddT\n"
          + "8h0mgJ6yzmkerR8VA0bknPQFeruWNJ/4PKDO9Itk5MmmYU/olvT5zMJ79K8RSvhN\n"
          + "+3gO8N7tcswaRP7HbEUmuVTtjFDlAgMBAAGjJjAkMBIGA1UdEwEB/wQIMAYBAf8C\n"
          + "AQEwDgYDVR0PAQH/BAQDAgLkMA0GCSqGSIb3DQEBCwUAA4IBAQCB10NLTyuqSD8/\n"
          + "HmbkdmH7TM1q0V/2qfrNQW86ywVKNHlKaZp9YlAtBcY5SJK37DJJH0yKM3GYk/ee\n"
          + "4aI566aj65gQn+wte9QfqzeotfVLQ4ZlhhOjDVRqSJVUdiW7yejHQLnqexdOpPQS\n"
          + "vwi73Fz0zGNxqnNjSNtka1rmduGwP0fiU3WKtHJiPL9CQFtRKdIlskKUlXg+WulM\n"
          + "x9yw5oa6xpsbCzSoS31fxYg71KAxVvKJYumdKV3ElGU/+AK1y4loyHv/kPp+59fF\n"
          + "9Q8gq/A6vGFjoZtVuuKUlasbMocle4Y9/nVxqIxWtc+aZ8mmP//J5oVXyzPs56dM\n"
          + "E1pTE1HS\n"
          + "-----END CERTIFICATE-----\n";
  @Rule public TestName testname = new TestName();

  @Before
  public void setUpV2Client() throws NoSuchAlgorithmException, IOException, ApiException {
    ApiClient v2ApiClient = Configuration.getDefaultApiClient();
    String basePath = String.format("http://localhost:%d/api/v2", port);
    v2ApiClient = v2ApiClient.setBasePath(basePath).addDefaultHeader("X-AUTH-TOKEN", authToken);
    Configuration.setDefaultApiClient(v2ApiClient);
    setupProvider(CloudType.aws);
    setupCerts();
  }

  protected void setupProvider(CloudType cloudType) {
    Provider provider = newProvider(customer, cloudType);
    providerUuid = provider.getUuid();
    // add 3 regions with 3 zones in each region
    Region region1 = Region.create(provider, "us-west-1", "us-west-1", "yb-image-1");
    AvailabilityZone.createOrThrow(region1, "r1-az-1", "R1 AZ 1", "subnet-1");
    AvailabilityZone.createOrThrow(region1, "r1-az-2", "R1 AZ 2", "subnet-2");
    AvailabilityZone.createOrThrow(region1, "r1-az-3", "R1 AZ 3", "subnet-3");
    Region region2 = Region.create(provider, "us-west-2", "us-west-2", "yb-image-2");
    AvailabilityZone.createOrThrow(region2, "r2-az-1", "R2 AZ 1", "subnet-1");
    AvailabilityZone.createOrThrow(region2, "r2-az-2", "R2 AZ 2", "subnet-2");
    AvailabilityZone.createOrThrow(region2, "r2-az-3", "R2 AZ 3", "subnet-3");
    Region region3 = Region.create(provider, "us-west-3", "us-west-3", "yb-image-3");
    AvailabilityZone.createOrThrow(region3, "r3-az-1", "R3 AZ 1", "subnet-1");
    AvailabilityZone.createOrThrow(region3, "r3-az-2", "R3 AZ 2", "subnet-2");
    AvailabilityZone.createOrThrow(region3, "r3-az-3", "R3 AZ 3", "subnet-3");
    // add image bundle to provider
    ImageBundleDetails imageDetails = new ImageBundleDetails();
    imageDetails.setArch(Architecture.aarch64);
    imageDetails.setGlobalYbImage("yb-image-global");
    ImageBundle.create(provider, "centos-image-ami", imageDetails, true);
    // add access keys to provider (TODO?)
  }

  protected void setupCerts() throws NoSuchAlgorithmException, IOException {
    String tmpPath = TestHelper.TMP_PATH + "/" + testname.getMethodName();
    createTempFile(tmpPath, "universe_management_api_controller_test_ca.crt", rootCAContents);
    rootCA = UUID.randomUUID();
    CertificateInfo.create(
        rootCA,
        customer.getUuid(),
        "test1",
        new Date(),
        new Date(),
        "privateKey",
        tmpPath + "/universe_management_api_controller_test_ca.crt",
        CertConfigType.SelfSigned);
    createTempFile(
        tmpPath, "universe_management_api_controller_test_client_ca.crt", clientRootCAContents);
    clientRootCA = UUID.randomUUID();
    CertificateInfo.create(
        clientRootCA,
        customer.getUuid(),
        "test2",
        new Date(),
        new Date(),
        "privateKey",
        tmpPath + "/universe_management_api_controller_test_client_ca.crt",
        CertConfigType.SelfSigned);
  }

  protected AvailabilityZoneGFlags createAZGFlags(String azCode) {
    AvailabilityZoneGFlags azGFlags = new AvailabilityZoneGFlags();
    azGFlags.setMaster(
        Map.of(azCode + "mflag1", azCode + "mval1", azCode + "mflag2", azCode + "mval2"));
    azGFlags.setTserver(
        Map.of(azCode + "tflag1", azCode + "tval1", azCode + "tflag2", azCode + "tval2"));
    return azGFlags;
  }

  protected ClusterGFlags createPrimaryClusterGFlags() {
    ClusterGFlags primaryGflags = new ClusterGFlags();
    primaryGflags.setMaster(Map.of("mflag1", "mval1", "mflag2", "mval2"));
    primaryGflags.setTserver(Map.of("tflag1", "tval1", "tflag2", "tval2"));
    Region.getByProvider(providerUuid)
        .get(0)
        .getZones()
        .forEach(
            z -> {
              primaryGflags.putAzGflagsItem(z.getUuid().toString(), createAZGFlags(z.getCode()));
            });
    return primaryGflags;
  }

  protected AuditLogConfig createPrimaryAuditLogConfig() {
    AuditLogConfig auditLogConfig = new AuditLogConfig();
    return auditLogConfig;
  }

  protected PlacementRegion getOrCreatePlacementRegion(
      PlacementCloud placementCloud, Region region) {
    PlacementRegion placementRegion = null;
    if (placementCloud.getRegionList() == null) {
      placementCloud.setRegionList(new ArrayList<>());
    }
    Optional<PlacementRegion> optPlacementRegion =
        placementCloud.getRegionList().stream()
            .filter(r -> r.getUuid().equals(region.getUuid()))
            .findAny();
    if (optPlacementRegion.isEmpty()) {
      placementRegion =
          new PlacementRegion()
              .code(region.getCode())
              .name(region.getName())
              .uuid(region.getUuid());
      placementCloud.addRegionListItem(placementRegion);
    } else {
      placementRegion = optPlacementRegion.get();
    }
    return placementRegion;
  }

  protected PlacementAZ getOrCreatePlacementAz(
      PlacementRegion placementRegion, AvailabilityZone zone) {
    PlacementAZ placementAZ = null;
    if (placementRegion.getAzList() == null) {
      placementRegion.setAzList(new ArrayList<>());
    }
    Optional<PlacementAZ> optPlacementAz =
        placementRegion.getAzList().stream()
            .filter(z -> z.getUuid().equals(zone.getUuid()))
            .findAny();
    if (optPlacementAz.isEmpty()) {
      placementAZ =
          new PlacementAZ()
              .uuid(zone.getUuid())
              .name(zone.getName())
              .subnet(zone.getSubnet())
              .secondarySubnet(zone.getSecondarySubnet())
              .leaderAffinity(true);
      placementRegion.addAzListItem(placementAZ);
    } else {
      placementAZ = optPlacementAz.get();
    }
    return placementAZ;
  }

  // Place nodes evenly across zones of first region
  protected PlacementCloud placementFromProvider(int numNodesToPlace, int rfTotal) {
    Provider provider = Provider.get(customer.getUuid(), providerUuid);
    PlacementCloud placementCloud =
        new PlacementCloud().uuid(providerUuid).code(provider.getCode());
    Region region = Region.getByProvider(providerUuid).get(0);
    PlacementRegion pr = getOrCreatePlacementRegion(placementCloud, region);
    int numPlacedNodes = 0;
    int rf = 0;
    while (numPlacedNodes < numNodesToPlace) {
      List<AvailabilityZone> zones = AvailabilityZone.getAZsForRegion(region.getUuid());
      for (AvailabilityZone zone : zones) {
        PlacementAZ pAz = getOrCreatePlacementAz(pr, zone);
        int numNodesInAz = pAz.getNumNodesInAz() != null ? pAz.getNumNodesInAz() : 0;
        pAz.numNodesInAz(numNodesInAz + 1);
        if (rf < rfTotal) {
          // increment rf in this az
          int currRf = pAz.getReplicationFactor() != null ? pAz.getReplicationFactor() : 0;
          pAz.setReplicationFactor(currRf + 1);
          rf++;
        }
        numPlacedNodes++;
        if (numPlacedNodes >= numNodesToPlace) {
          break;
        }
      }
    }
    return placementCloud;
  }

  protected UniverseCreateSpec getUniverseCreateSpecV2() {
    UniverseCreateSpec universeCreateSpec = new UniverseCreateSpec();
    universeCreateSpec.arch(ArchEnum.AARCH64);
    universeCreateSpec.ysql(new UniverseCreateSpecYsql().password("ysqlPassword"));
    universeCreateSpec.ycql(new UniverseCreateSpecYcql().password("ycqlPassword"));
    UniverseSpec universeSpec = new UniverseSpec();
    universeCreateSpec.spec(universeSpec);
    universeSpec.name("Test-V2-Universe");
    universeSpec.setYbSoftwareVersion("2.20.0.0-b123");
    universeSpec.setUseTimeSync(true);
    universeSpec.setUseSystemd(true);
    universeSpec.ysql(new YsqlSpec().enable(true).enableAuth(false));
    universeSpec.ycql(new YcqlSpec().enable(true).enableAuth(false));
    universeSpec.networkingSpec(
        new UniverseNetworkingSpec()
            .assignPublicIp(true)
            .assignStaticPublicIp(true)
            .enableIpv6(false)
            .communicationPorts(
                new CommunicationPortsSpec().masterHttpPort(1234).tserverHttpPort(5678)));
    EncryptionAtRestSpec ear = new EncryptionAtRestSpec().kmsConfigUuid(UUID.randomUUID());
    universeSpec.encryptionAtRestSpec(ear);
    EncryptionInTransitSpec eit = new EncryptionInTransitSpec();
    eit.enableNodeToNodeEncrypt(true)
        .enableClientToNodeEncrypt(true)
        .rootCa(rootCA)
        .clientRootCa(clientRootCA);
    universeSpec.encryptionInTransitSpec(eit);
    ClusterSpec primaryClusterSpec = new ClusterSpec();
    primaryClusterSpec.setClusterType(ClusterTypeEnum.PRIMARY);
    primaryClusterSpec.setNumNodes(6);
    primaryClusterSpec.setInstanceType(ApiUtils.UTIL_INST_TYPE);
    primaryClusterSpec.setReplicationFactor(5);
    primaryClusterSpec.setUseSpotInstance(true);
    primaryClusterSpec.setInstanceTags(Map.of("itag1", "ival1", "itag2", "ival2"));
    PlacementCloud placementCloud =
        placementFromProvider(
            primaryClusterSpec.getNumNodes(), primaryClusterSpec.getReplicationFactor());
    primaryClusterSpec.setPlacementSpec(
        new ClusterPlacementSpec().cloudList(List.of(placementCloud)));
    ClusterProviderSpec providerSpec = new ClusterProviderSpec();
    providerSpec.setProvider(providerUuid);
    providerSpec.setRegionList(List.of(Region.getByProvider(providerUuid).get(0).getUuid()));
    providerSpec.setImageBundleUuid(ImageBundle.getAll(providerUuid).get(0).getUuid());
    providerSpec.setAccessKeyCode(ApiUtils.DEFAULT_ACCESS_KEY_CODE);
    primaryClusterSpec.setProviderSpec(providerSpec);
    primaryClusterSpec.setStorageSpec(
        new ClusterStorageSpec().volumeSize(54321).numVolumes(2).storageType(StorageTypeEnum.GP2));
    primaryClusterSpec.setNetworkingSpec(
        new ClusterNetworkingSpec()
            .enableLb(true)
            .enableExposingService(EnableExposingServiceEnum.EXPOSED));
    primaryClusterSpec.setGflags(createPrimaryClusterGFlags());
    primaryClusterSpec.setAuditLogConfig(createPrimaryAuditLogConfig());
    universeSpec.addClustersItem(primaryClusterSpec);
    return universeCreateSpec;
  }

  // validations
  protected void validateUniverseCreateSpec(UniverseCreateSpec v2Univ, Universe dbUniv) {
    UniverseDefinitionTaskParams dbUnivDetails = dbUniv.getUniverseDetails();
    assertThat(v2Univ.getArch().getValue(), is(dbUnivDetails.arch.name()));
    validateUniverseSpec(v2Univ.getSpec(), dbUniv);
  }

  protected void validateUniverseSpec(UniverseSpec v2UnivSpec, Universe dbUniv) {
    UniverseDefinitionTaskParams dbUnivDetails = dbUniv.getUniverseDetails();
    assertThat(v2UnivSpec.getName(), is(dbUniv.getName()));
    validateUniverseSpec(v2UnivSpec, dbUnivDetails);
  }

  private void validateUniverseSpec(
      UniverseSpec v2UnivSpec, UniverseDefinitionTaskParams dbUnivDetails) {
    UserIntent dbUserIntent = dbUnivDetails.getPrimaryCluster().userIntent;
    assertThat(v2UnivSpec.getYbSoftwareVersion(), is(dbUserIntent.ybSoftwareVersion));
    if (v2UnivSpec.getUseSystemd() == null) {
      assertThat(dbUserIntent.useSystemd, is(false));
    } else {
      assertThat(v2UnivSpec.getUseSystemd(), is(dbUserIntent.useSystemd));
    }
    if (v2UnivSpec.getUseTimeSync() == null) {
      assertThat(dbUserIntent.useTimeSync, is(false));
    } else {
      assertThat(v2UnivSpec.getUseTimeSync(), is(dbUserIntent.useTimeSync));
    }
    if (v2UnivSpec.getOverridePrebuiltAmiDbVersion() == null) {
      assertThat(dbUnivDetails.overridePrebuiltAmiDBVersion, is(false));
    } else {
      assertThat(
          v2UnivSpec.getOverridePrebuiltAmiDbVersion(),
          is(dbUnivDetails.overridePrebuiltAmiDBVersion));
    }
    if (StringUtils.isEmpty(v2UnivSpec.getRemotePackagePath())) {
      assertThat(
          dbUnivDetails.remotePackagePath, is(either(nullValue(String.class)).or(emptyString())));
    } else {
      assertThat(v2UnivSpec.getRemotePackagePath(), is(dbUnivDetails.remotePackagePath));
    }
    validateUniverseNetworkginSpec(v2UnivSpec.getNetworkingSpec(), dbUnivDetails);
    validateEncryptionAtRest(
        v2UnivSpec.getEncryptionAtRestSpec(), dbUnivDetails.encryptionAtRestConfig);
    validateEncryptionInTransit(v2UnivSpec.getEncryptionInTransitSpec(), dbUnivDetails);
    validateYsqlSpec(v2UnivSpec.getYsql(), dbUnivDetails);
    validateYcqlSpec(v2UnivSpec.getYcql(), dbUnivDetails);
    validateClusters(v2UnivSpec.getClusters(), dbUnivDetails.clusters);
  }

  private void validateUniverseNetworkginSpec(
      UniverseNetworkingSpec v2NetworkingSpec, UniverseDefinitionTaskParams dbUniv) {
    UserIntent primaryUserIntent = dbUniv.getPrimaryCluster().userIntent;
    if (v2NetworkingSpec.getAssignPublicIp() == null) {
      assertThat(primaryUserIntent.assignPublicIP, is(true));
    } else {
      assertThat(v2NetworkingSpec.getAssignPublicIp(), is(primaryUserIntent.assignPublicIP));
    }
    if (v2NetworkingSpec.getAssignStaticPublicIp() == null) {
      assertThat(primaryUserIntent.assignStaticPublicIP, is(false));
    } else {
      assertThat(
          v2NetworkingSpec.getAssignStaticPublicIp(), is(primaryUserIntent.assignStaticPublicIP));
    }
    if (v2NetworkingSpec.getEnableIpv6() == null) {
      assertThat(primaryUserIntent.enableIPV6, is(false));
    } else {
      assertThat(v2NetworkingSpec.getEnableIpv6(), is(primaryUserIntent.enableIPV6));
    }
    validateCommunicationPorts(v2NetworkingSpec.getCommunicationPorts(), dbUniv.communicationPorts);
  }

  private void validateCommunicationPorts(CommunicationPortsSpec v2CP, CommunicationPorts dbCP) {
    if (v2CP.getMasterHttpPort() == null) {
      assertThat(dbCP.masterHttpPort, is(7000));
    } else {
      assertThat(dbCP.masterHttpPort, is(v2CP.getMasterHttpPort()));
    }
    if (v2CP.getMasterRpcPort() == null) {
      assertThat(dbCP.masterRpcPort, is(7100));
    } else {
      assertThat(dbCP.masterRpcPort, is(v2CP.getMasterRpcPort()));
    }
    if (v2CP.getNodeExporterPort() == null) {
      assertThat(dbCP.nodeExporterPort, is(9300));
    } else {
      assertThat(dbCP.nodeExporterPort, is(v2CP.getNodeExporterPort()));
    }
    if (v2CP.getOtelCollectorMetricsPort() == null) {
      // default is coming from Provider runtime config yb.universe.otel_collector_metrics_port
      assertThat(dbCP.otelCollectorMetricsPort, is(0));
    } else {
      assertThat(dbCP.otelCollectorMetricsPort, is(v2CP.getOtelCollectorMetricsPort()));
    }
    if (v2CP.getRedisServerHttpPort() == null) {
      assertThat(dbCP.redisServerHttpPort, is(11000));
    } else {
      assertThat(dbCP.redisServerHttpPort, is(v2CP.getRedisServerHttpPort()));
    }
    if (v2CP.getRedisServerRpcPort() == null) {
      assertThat(dbCP.redisServerRpcPort, is(6379));
    } else {
      assertThat(dbCP.redisServerRpcPort, is(v2CP.getRedisServerRpcPort()));
    }
    if (v2CP.getTserverHttpPort() == null) {
      assertThat(dbCP.tserverHttpPort, is(9000));
    } else {
      assertThat(dbCP.tserverHttpPort, is(v2CP.getTserverHttpPort()));
    }
    if (v2CP.getTserverRpcPort() == null) {
      assertThat(dbCP.tserverRpcPort, is(9100));
    } else {
      assertThat(dbCP.tserverRpcPort, is(v2CP.getTserverRpcPort()));
    }
    if (v2CP.getYbControllerHttpPort() == null) {
      assertThat(dbCP.ybControllerHttpPort, is(14000));
    } else {
      assertThat(dbCP.ybControllerHttpPort, is(v2CP.getYbControllerHttpPort()));
    }
    if (v2CP.getYbControllerRpcPort() == null) {
      assertThat(dbCP.ybControllerrRpcPort, is(18018));
    } else {
      assertThat(dbCP.ybControllerrRpcPort, is(v2CP.getYbControllerRpcPort()));
    }
    if (v2CP.getYqlServerHttpPort() == null) {
      assertThat(dbCP.yqlServerHttpPort, is(12000));
    } else {
      assertThat(dbCP.yqlServerHttpPort, is(v2CP.getYqlServerHttpPort()));
    }
    if (v2CP.getYqlServerRpcPort() == null) {
      assertThat(dbCP.yqlServerRpcPort, is(9042));
    } else {
      assertThat(dbCP.yqlServerRpcPort, is(v2CP.getYqlServerRpcPort()));
    }
    if (v2CP.getYsqlServerHttpPort() == null) {
      assertThat(dbCP.ysqlServerHttpPort, is(13000));
    } else {
      assertThat(dbCP.ysqlServerHttpPort, is(v2CP.getYsqlServerHttpPort()));
    }
    if (v2CP.getYsqlServerRpcPort() == null) {
      assertThat(dbCP.ysqlServerRpcPort, is(5433));
    } else {
      assertThat(dbCP.ysqlServerRpcPort, is(v2CP.getYsqlServerRpcPort()));
    }
  }

  private void validateEncryptionAtRest(EncryptionAtRestSpec v2Enc, EncryptionAtRestConfig dbEnc) {
    assertThat(v2Enc.getKmsConfigUuid(), is(dbEnc.kmsConfigUUID));
    assertThat(dbEnc.type, is(EncryptionAtRestUtil.KeyType.DATA_KEY));
  }

  private void validateEncryptionInTransit(
      EncryptionInTransitSpec v2EIT, UniverseDefinitionTaskParams dbUniv) {
    assertThat(
        v2EIT.getEnableNodeToNodeEncrypt(),
        is(dbUniv.getPrimaryCluster().userIntent.enableNodeToNodeEncrypt));
    assertThat(
        v2EIT.getEnableClientToNodeEncrypt(),
        is(dbUniv.getPrimaryCluster().userIntent.enableClientToNodeEncrypt));
    assertThat(v2EIT.getRootCa(), is(dbUniv.rootCA));
    assertThat(v2EIT.getClientRootCa(), is(dbUniv.getClientRootCA()));
    if (v2EIT.getRootCa() != null) {
      assertThat(
          dbUniv.rootAndClientRootCASame, is(v2EIT.getRootCa().equals(v2EIT.getClientRootCa())));
    } else {
      assertThat(dbUniv.rootAndClientRootCASame, is(true));
    }
  }

  private void validateYsqlSpec(YsqlSpec ysql, UniverseDefinitionTaskParams dbUniv) {
    if (ysql != null) {
      assertThat(ysql.getEnable(), is(dbUniv.getPrimaryCluster().userIntent.enableYSQL));
      assertThat(ysql.getEnableAuth(), is(dbUniv.getPrimaryCluster().userIntent.enableYSQLAuth));
    } else {
      assertThat(dbUniv.getPrimaryCluster().userIntent.enableYSQL, is(false));
      assertThat(dbUniv.getPrimaryCluster().userIntent.enableYSQLAuth, is(false));
    }
  }

  private void validateYcqlSpec(YcqlSpec ycql, UniverseDefinitionTaskParams dbUniv) {
    if (ycql != null) {
      assertThat(ycql.getEnable(), is(dbUniv.getPrimaryCluster().userIntent.enableYCQL));
      assertThat(ycql.getEnableAuth(), is(dbUniv.getPrimaryCluster().userIntent.enableYCQLAuth));
    } else {
      assertThat(dbUniv.getPrimaryCluster().userIntent.enableYCQL, is(false));
      assertThat(dbUniv.getPrimaryCluster().userIntent.enableYCQLAuth, is(false));
    }
  }

  private void validateClusters(List<ClusterSpec> v2Clusters, List<Cluster> dbClusters) {
    assertThat(v2Clusters.size(), is(dbClusters.size()));
    for (int i = 0; i < v2Clusters.size(); i++) {
      validateCluster(v2Clusters.get(i), dbClusters.get(i));
    }
  }

  private void validateCluster(ClusterSpec v2Cluster, Cluster dbCluster) {
    assertThat(v2Cluster.getClusterType().getValue(), is(dbCluster.clusterType.name()));
    assertThat(v2Cluster.getNumNodes(), is(dbCluster.userIntent.numNodes));
    assertThat(v2Cluster.getReplicationFactor(), is(dbCluster.userIntent.replicationFactor));
    assertThat(v2Cluster.getInstanceType(), is(dbCluster.userIntent.instanceType));
    assertThat(v2Cluster.getUseSpotInstance(), is(dbCluster.userIntent.useSpotInstance));
    validateProviderSpec(v2Cluster.getProviderSpec(), dbCluster);
    validatePlacementSpec(v2Cluster.getPlacementSpec(), dbCluster.placementInfo);
    validateStorageSpec(v2Cluster.getStorageSpec(), dbCluster);
    validateNetworkingSpec(v2Cluster.getNetworkingSpec(), dbCluster);
    validateGFlags(v2Cluster.getGflags(), dbCluster.userIntent.specificGFlags);
    validateInstanceTags(v2Cluster.getInstanceTags(), dbCluster.userIntent.instanceTags);
    validateAuditLogConfig(v2Cluster.getAuditLogConfig(), dbCluster.userIntent.auditLogConfig);
  }

  private void validateProviderSpec(ClusterProviderSpec v2ProviderSpec, Cluster dbCluster) {
    assertThat(v2ProviderSpec.getProvider(), is(UUID.fromString(dbCluster.userIntent.provider)));
    assertThat(v2ProviderSpec.getImageBundleUuid(), is(dbCluster.userIntent.imageBundleUUID));
    assertThat(v2ProviderSpec.getPreferredRegion(), is(dbCluster.userIntent.preferredRegion));
    assertThat(v2ProviderSpec.getAccessKeyCode(), is(dbCluster.userIntent.accessKeyCode));
    assertThat(
        v2ProviderSpec.getRegionList(),
        containsInAnyOrder(dbCluster.userIntent.regionList.toArray()));
  }

  private void validatePlacementSpec(
      ClusterPlacementSpec v2PI, com.yugabyte.yw.models.helpers.PlacementInfo dbPI) {
    if (v2PI == null) {
      assertThat(dbPI, is(nullValue()));
      return;
    }
    if (v2PI.getCloudList() == null) {
      assertThat(dbPI.cloudList, is(nullValue()));
      return;
    }
    assertThat(v2PI.getCloudList().size(), is(dbPI.cloudList.size()));
    for (PlacementCloud v2Cloud : v2PI.getCloudList()) {
      // find corresponding cloud in db cloud list
      com.yugabyte.yw.models.helpers.PlacementInfo.PlacementCloud dbCloud =
          dbPI.cloudList.stream()
              .filter(c -> c.uuid.equals(v2Cloud.getUuid()))
              .findFirst()
              .orElseThrow();
      assertThat(v2Cloud.getCode(), is(dbCloud.code));
      assertThat(v2Cloud.getDefaultRegion(), is(dbCloud.defaultRegion));
      verifyPlacementRegion(v2Cloud.getRegionList(), dbCloud.regionList);
    }
  }

  private void validateStorageSpec(ClusterStorageSpec v2StorageSpec, Cluster dbCluster) {
    assertThat(v2StorageSpec.getNumVolumes(), is(dbCluster.userIntent.deviceInfo.numVolumes));
    assertThat(v2StorageSpec.getVolumeSize(), is(dbCluster.userIntent.deviceInfo.volumeSize));
    assertThat(v2StorageSpec.getDiskIops(), is(dbCluster.userIntent.deviceInfo.diskIops));
    assertThat(v2StorageSpec.getMountPoints(), is(dbCluster.userIntent.deviceInfo.mountPoints));
    if (v2StorageSpec.getStorageClass() == null) {
      assertThat(dbCluster.userIntent.deviceInfo.storageClass, is(emptyString()));
    } else {
      assertThat(v2StorageSpec.getStorageClass(), is(dbCluster.userIntent.deviceInfo.storageClass));
    }
    assertThat(
        v2StorageSpec.getStorageType().getValue(),
        is(dbCluster.userIntent.deviceInfo.storageType.name()));
    assertThat(v2StorageSpec.getThroughput(), is(dbCluster.userIntent.deviceInfo.throughput));
  }

  private void validateNetworkingSpec(ClusterNetworkingSpec v2NetworkingSpec, Cluster dbCluster) {
    assertThat(
        v2NetworkingSpec.getEnableExposingService().getValue(),
        is(dbCluster.userIntent.enableExposingService.name()));
    assertThat(v2NetworkingSpec.getEnableLb(), is(dbCluster.userIntent.enableLB));
  }

  private void validateInstanceTags(
      Map<String, String> v2InstanceTags, Map<String, String> dbInstanceTags) {
    assertThat(v2InstanceTags.size(), is(dbInstanceTags.size()));
    v2InstanceTags
        .entrySet()
        .forEach(e -> assertThat(dbInstanceTags, hasEntry(e.getKey(), e.getValue())));
  }

  private void validateGFlags(ClusterGFlags v2GFlags, SpecificGFlags dbGFlags) {
    v2GFlags
        .getMaster()
        .entrySet()
        .forEach(
            e ->
                assertThat(
                    dbGFlags.getPerProcessFlags().value.get(ServerType.MASTER),
                    hasEntry(e.getKey(), e.getValue())));
    v2GFlags
        .getTserver()
        .entrySet()
        .forEach(
            e ->
                assertThat(
                    dbGFlags.getPerProcessFlags().value.get(ServerType.TSERVER),
                    hasEntry(e.getKey(), e.getValue())));
    v2GFlags
        .getAzGflags()
        .entrySet()
        .forEach(
            e -> {
              UUID azUuid = UUID.fromString(e.getKey());
              AvailabilityZoneGFlags azGFlags = e.getValue();
              azGFlags
                  .getMaster()
                  .entrySet()
                  .forEach(
                      m ->
                          assertThat(
                              dbGFlags.getPerAZ().get(azUuid).value.get(ServerType.MASTER),
                              hasEntry(m.getKey(), m.getValue())));
              azGFlags
                  .getTserver()
                  .entrySet()
                  .forEach(
                      t ->
                          assertThat(
                              dbGFlags.getPerAZ().get(azUuid).value.get(ServerType.TSERVER),
                              hasEntry(t.getKey(), t.getValue())));
            });
  }

  private void validateAuditLogConfig(
      AuditLogConfig v2AuditLogConfig,
      com.yugabyte.yw.models.helpers.audit.AuditLogConfig dbAuditLogConfig) {
    if (v2AuditLogConfig == null) {
      assertThat(dbAuditLogConfig, is(nullValue()));
      return;
    }
    if (v2AuditLogConfig.getExportActive() == null) {
      assertThat(dbAuditLogConfig.isExportActive(), is(true));
    } else {
      assertThat(v2AuditLogConfig.getExportActive(), is(dbAuditLogConfig.isExportActive()));
    }
    assertThat(
        v2AuditLogConfig.getUniverseLogsExporterConfig().size(),
        is(dbAuditLogConfig.getUniverseLogsExporterConfig().size()));
    for (int i = 0; i < v2AuditLogConfig.getUniverseLogsExporterConfig().size(); i++) {
      validateUniverseLogsExportedConfig(
          v2AuditLogConfig.getUniverseLogsExporterConfig().get(i),
          dbAuditLogConfig.getUniverseLogsExporterConfig().get(i));
    }
    validateYsqlAuditConfig(
        v2AuditLogConfig.getYsqlAuditConfig(), dbAuditLogConfig.getYsqlAuditConfig());
    validateYcqlAuditConfig(
        v2AuditLogConfig.getYcqlAuditConfig(), dbAuditLogConfig.getYcqlAuditConfig());
  }

  private void validateUniverseLogsExportedConfig(
      UniverseLogsExporterConfig v2UniverseLogsExporterConfig,
      com.yugabyte.yw.models.helpers.audit.UniverseLogsExporterConfig
          dbUniverseLogsExporterConfig) {
    if (v2UniverseLogsExporterConfig == null) {
      assertThat(dbUniverseLogsExporterConfig, is(nullValue()));
      return;
    }
    assertThat(
        v2UniverseLogsExporterConfig.getExporterUuid(),
        is(dbUniverseLogsExporterConfig.getExporterUuid()));
    v2UniverseLogsExporterConfig
        .getAdditionalTags()
        .entrySet()
        .forEach(
            e ->
                assertThat(
                    dbUniverseLogsExporterConfig.getAdditionalTags(),
                    hasEntry(e.getKey(), e.getValue())));
  }

  private void validateYsqlAuditConfig(
      YSQLAuditConfig v2YsqlAuditConfig,
      com.yugabyte.yw.models.helpers.audit.YSQLAuditConfig dbYsqlAuditConfig) {
    if (v2YsqlAuditConfig == null) {
      assertThat(dbYsqlAuditConfig, is(nullValue()));
      return;
    }
    Set<String> v2ClassNames =
        v2YsqlAuditConfig.getClasses().stream().map(c -> c.getValue()).collect(Collectors.toSet());
    Set<String> dbClasseNames =
        dbYsqlAuditConfig.getClasses().stream().map(c -> c.name()).collect(Collectors.toSet());
    assertThat(v2ClassNames, containsInAnyOrder(dbClasseNames.toArray()));
    assertThat(v2YsqlAuditConfig.getEnabled(), is(dbYsqlAuditConfig.isEnabled()));
    assertThat(v2YsqlAuditConfig.getLogCatalog(), is(dbYsqlAuditConfig.isLogCatalog()));
    assertThat(v2YsqlAuditConfig.getLogClient(), is(dbYsqlAuditConfig.isLogClient()));
    if (v2YsqlAuditConfig.getLogLevel() == null) {
      assertThat(dbYsqlAuditConfig.getLogLevel(), is(nullValue()));
    } else {
      assertThat(
          v2YsqlAuditConfig.getLogLevel().getValue(), is(dbYsqlAuditConfig.getLogLevel().name()));
    }
    assertThat(v2YsqlAuditConfig.getLogParameter(), is(dbYsqlAuditConfig.isLogParameter()));
    assertThat(
        v2YsqlAuditConfig.getLogParameterMaxSize(), is(dbYsqlAuditConfig.getLogParameterMaxSize()));
    assertThat(v2YsqlAuditConfig.getLogRelation(), is(dbYsqlAuditConfig.isLogRelation()));
    assertThat(v2YsqlAuditConfig.getLogRow(), is(dbYsqlAuditConfig.isLogRow()));
    assertThat(v2YsqlAuditConfig.getLogStatement(), is(dbYsqlAuditConfig.isLogStatement()));
    assertThat(v2YsqlAuditConfig.getLogStatementOnce(), is(dbYsqlAuditConfig.isLogStatementOnce()));
  }

  private void validateYcqlAuditConfig(
      YCQLAuditConfig v2YcqlAuditConfig,
      com.yugabyte.yw.models.helpers.audit.YCQLAuditConfig dbYcqlAuditConfig) {
    if (v2YcqlAuditConfig == null) {
      assertThat(dbYcqlAuditConfig, is(nullValue()));
      return;
    }
    assertThat(v2YcqlAuditConfig.getEnabled(), is(dbYcqlAuditConfig.isEnabled()));
    Set<String> v2ExcludedCategories =
        v2YcqlAuditConfig.getExcludedCategories() != null
            ? v2YcqlAuditConfig.getExcludedCategories().stream()
                .map(e -> e.getValue())
                .collect(Collectors.toSet())
            : Set.of();
    Set<String> dbExcludedCategories =
        dbYcqlAuditConfig.getExcludedCategories() != null
            ? dbYcqlAuditConfig.getExcludedCategories().stream()
                .map(e -> e.name())
                .collect(Collectors.toSet())
            : Set.of();
    assertThat(v2ExcludedCategories, containsInAnyOrder(dbExcludedCategories.toArray()));
    if (v2YcqlAuditConfig.getExcludedKeyspaces() == null) {
      assertThat(dbYcqlAuditConfig.getExcludedKeyspaces(), is(nullValue()));
    } else {
      assertThat(
          v2YcqlAuditConfig.getExcludedKeyspaces(),
          containsInAnyOrder(dbYcqlAuditConfig.getExcludedKeyspaces().toArray()));
    }
    if (v2YcqlAuditConfig.getExcludedUsers() == null) {
      assertThat(dbYcqlAuditConfig.getExcludedUsers(), is(nullValue()));
    } else {
      assertThat(
          v2YcqlAuditConfig.getExcludedUsers(),
          containsInAnyOrder(dbYcqlAuditConfig.getExcludedUsers().toArray()));
    }
    Set<String> v2IncludedCategories =
        v2YcqlAuditConfig.getIncludedCategories() != null
            ? v2YcqlAuditConfig.getIncludedCategories().stream()
                .map(e -> e.getValue())
                .collect(Collectors.toSet())
            : Set.of();
    Set<String> dbIncludedCategories =
        dbYcqlAuditConfig.getIncludedCategories() != null
            ? dbYcqlAuditConfig.getIncludedCategories().stream()
                .map(e -> e.name())
                .collect(Collectors.toSet())
            : Set.of();
    assertThat(v2IncludedCategories, containsInAnyOrder(dbIncludedCategories.toArray()));
    if (v2YcqlAuditConfig.getIncludedKeyspaces() == null) {
      assertThat(dbYcqlAuditConfig.getIncludedKeyspaces(), is(nullValue()));
    } else {
      assertThat(
          v2YcqlAuditConfig.getIncludedKeyspaces(),
          containsInAnyOrder(dbYcqlAuditConfig.getIncludedKeyspaces().toArray()));
    }
    if (v2YcqlAuditConfig.getIncludedUsers() == null) {
      assertThat(dbYcqlAuditConfig.getIncludedUsers(), is(nullValue()));
    } else {
      assertThat(
          v2YcqlAuditConfig.getIncludedUsers(),
          containsInAnyOrder(dbYcqlAuditConfig.getIncludedUsers().toArray()));
    }
    if (v2YcqlAuditConfig.getLogLevel() == null) {
      assertThat(dbYcqlAuditConfig.getLogLevel(), is(nullValue()));
    } else {
      assertThat(
          v2YcqlAuditConfig.getLogLevel().getValue(), is(dbYcqlAuditConfig.getLogLevel().name()));
    }
  }

  protected void validateUniverseInfo(UniverseInfo v2UnivInfo, Universe dbUniverse) {
    UniverseDefinitionTaskParams dbUniv = dbUniverse.getUniverseDetails();
    if (v2UnivInfo.getArch() == null) {
      // default image bundle arch used in cloud provider in this test is aarch64
      assertThat(dbUniv.arch, is(PublicCloudConstants.Architecture.aarch64));
    } else {
      assertThat(v2UnivInfo.getArch().getValue(), is(dbUniv.arch.name()));
    }
    validateClustersInfo(v2UnivInfo.getClusters(), dbUniverse);
    OffsetDateTime dbCreationDate =
        dbUniverse
            .getCreationDate()
            .toInstant()
            .truncatedTo(ChronoUnit.SECONDS)
            .atOffset(ZoneOffset.UTC);
    assertThat(v2UnivInfo.getCreationDate().compareTo(dbCreationDate), is(0));
    com.yugabyte.yw.forms.UniverseResp v1UniverseResp =
        new com.yugabyte.yw.forms.UniverseResp(dbUniverse);
    assertThat(
        v2UnivInfo.getDnsName(),
        is(v1UniverseResp.getDnsName(customer, Provider.getOrBadRequest(providerUuid))));
    assertThat(
        v2UnivInfo.getIsKubernetesOperatorControlled(), is(dbUniv.isKubernetesOperatorControlled));
    assertThat(v2UnivInfo.getIsSoftwareRollbackAllowed(), is(dbUniv.isSoftwareRollbackAllowed));
    validateNodeDetailsSet(v2UnivInfo.getNodeDetailsSet(), dbUniv.nodeDetailsSet);
    assertThat(v2UnivInfo.getNodePrefix(), is(dbUniv.nodePrefix));
    assertThat(v2UnivInfo.getNodesResizeAvailable(), is(dbUniv.nodesResizeAvailable));
    assertThat(v2UnivInfo.getOtelCollectorEnabled(), is(dbUniv.otelCollectorEnabled));
    assertThat(
        v2UnivInfo.getPlacementModificationTaskUuid(), is(dbUniv.placementModificationTaskUuid));
    assertThat(v2UnivInfo.getPlatformUrl(), is(dbUniv.platformUrl));
    assertThat(
        v2UnivInfo.getSoftwareUpgradeState().getValue(), is(dbUniv.softwareUpgradeState.name()));
    assertThat(v2UnivInfo.getUniversePaused(), is(dbUniv.universePaused));
    assertThat(v2UnivInfo.getUniverseUuid(), is(dbUniverse.getUniverseUUID()));
    assertThat(v2UnivInfo.getUpdateInProgress(), is(dbUniv.updateInProgress));
    assertThat(v2UnivInfo.getUpdateSucceeded(), is(dbUniv.updateSucceeded));
    if (v2UnivInfo.getUpdatingTask() == null) {
      assertThat(dbUniv.updatingTask, is(nullValue()));
    } else {
      assertThat(v2UnivInfo.getUpdatingTask(), is(dbUniv.updatingTask.name()));
    }
    assertThat(v2UnivInfo.getUpdatingTaskUuid(), is(dbUniv.updatingTaskUUID));
    assertThat(v2UnivInfo.getVersion(), is(dbUniverse.getVersion()));
    assertThat(v2UnivInfo.getYbcSoftwareVersion(), is(dbUniv.getYbcSoftwareVersion()));
    validateXClusterInfo(v2UnivInfo.getxClusterInfo(), dbUniv.xClusterInfo);
  }

  private void validateClustersInfo(List<ClusterInfo> v2Clusters, Universe dbUniverse) {
    UniverseDefinitionTaskParams dbUniv = dbUniverse.getUniverseDetails();
    assertThat(v2Clusters.size(), is(dbUniv.clusters.size()));
    v2Clusters.forEach(
        v2Cluster -> {
          Cluster dbCluster =
              dbUniv.clusters.stream()
                  .filter(dbCls -> dbCls.uuid.equals(v2Cluster.getUuid()))
                  .findFirst()
                  .orElseThrow();
          validateClusterInfo(v2Cluster, dbCluster);
        });
  }

  private void validateClusterInfo(ClusterInfo v2Cluster, Cluster dbCluster) {
    assertThat(v2Cluster.getUuid(), is(dbCluster.uuid));
    assertThat(v2Cluster.getSpotPrice(), is(dbCluster.userIntent.spotPrice));
  }

  private void verifyPlacementRegion(
      List<PlacementRegion> v2RegionList,
      List<com.yugabyte.yw.models.helpers.PlacementInfo.PlacementRegion> dbRegionList) {
    if (v2RegionList == null) {
      assertThat(dbRegionList, is(nullValue()));
      return;
    }
    for (PlacementRegion v2Region : v2RegionList) {
      // find corresponding region in db region list
      com.yugabyte.yw.models.helpers.PlacementInfo.PlacementRegion dbRegion =
          dbRegionList.stream()
              .filter(r -> r.uuid.equals(v2Region.getUuid()))
              .findFirst()
              .orElseThrow();
      assertThat(v2Region.getCode(), is(dbRegion.code));
      assertThat(v2Region.getName(), is(dbRegion.name));
      assertThat(v2Region.getLbFqdn(), is(dbRegion.lbFQDN));
      verifyPlacementZone(v2Region.getAzList(), dbRegion.azList);
    }
  }

  private void verifyPlacementZone(
      List<PlacementAZ> v2AzList,
      List<com.yugabyte.yw.models.helpers.PlacementInfo.PlacementAZ> dbAzList) {
    if (v2AzList == null) {
      assertThat(dbAzList, is(nullValue()));
      return;
    }
    for (PlacementAZ v2Az : v2AzList) {
      // find corresponding az in db az list
      com.yugabyte.yw.models.helpers.PlacementInfo.PlacementAZ dbAz =
          dbAzList.stream().filter(a -> a.uuid.equals(v2Az.getUuid())).findFirst().orElseThrow();
      assertThat(v2Az.getLeaderAffinity(), is(dbAz.isAffinitized));
      assertThat(v2Az.getLbName(), is(dbAz.lbName));
      assertThat(v2Az.getName(), is(dbAz.name));
      assertThat(v2Az.getNumNodesInAz(), is(dbAz.numNodesInAZ));
      assertThat(v2Az.getReplicationFactor(), is(dbAz.replicationFactor));
      assertThat(v2Az.getSecondarySubnet(), is(dbAz.secondarySubnet));
      assertThat(v2Az.getSubnet(), is(dbAz.subnet));
    }
  }

  private void validateNodeDetailsSet(
      List<NodeDetails> v2NodeDetailsSet,
      Set<com.yugabyte.yw.models.helpers.NodeDetails> dbNodeDetailsSet) {
    if (v2NodeDetailsSet == null || v2NodeDetailsSet.size() == 0) {
      assertThat(dbNodeDetailsSet, is(either(nullValue()).or(is(Set.of()))));
      return;
    }
    for (NodeDetails v2NodeDetail : v2NodeDetailsSet) {
      // find the corresponding NodeDetail in dbNodeDetailSet
      com.yugabyte.yw.models.helpers.NodeDetails dbNodeDetail =
          dbNodeDetailsSet.stream()
              .filter(n -> v2NodeDetail.getNodeIdx().equals(n.nodeIdx))
              .findAny()
              .orElseThrow();
      validateNodeDetail(v2NodeDetail, dbNodeDetail);
    }
  }

  private void validateNodeDetail(
      NodeDetails v2NodeDetail, com.yugabyte.yw.models.helpers.NodeDetails dbNodeDetail) {
    if (v2NodeDetail == null) {
      assertThat(dbNodeDetail, is(nullValue()));
      return;
    }
    assertThat(v2NodeDetail.getAzUuid(), is(dbNodeDetail.azUuid));
    validateCloudSpecificInfo(v2NodeDetail.getCloudInfo(), dbNodeDetail.cloudInfo);
    assertThat(v2NodeDetail.getCronsActive(), is(dbNodeDetail.cronsActive));
    if (v2NodeDetail.getDedicatedTo() == null) {
      assertThat(dbNodeDetail.dedicatedTo, is(nullValue()));
    } else {
      assertThat(v2NodeDetail.getDedicatedTo().getValue(), is(dbNodeDetail.dedicatedTo.name()));
    }
    assertThat(v2NodeDetail.getDisksAreMountedByUuid(), is(dbNodeDetail.disksAreMountedByUUID));
    assertThat(v2NodeDetail.getIsMaster(), is(dbNodeDetail.isMaster));
    assertThat(v2NodeDetail.getIsRedisServer(), is(dbNodeDetail.isRedisServer));
    assertThat(v2NodeDetail.getIsTserver(), is(dbNodeDetail.isTserver));
    assertThat(v2NodeDetail.getIsYqlServer(), is(dbNodeDetail.isYqlServer));
    assertThat(v2NodeDetail.getIsYsqlServer(), is(dbNodeDetail.isYsqlServer));
    if (v2NodeDetail.getLastVolumeUpdateTime() == null) {
      assertThat(dbNodeDetail.lastVolumeUpdateTime, is(nullValue()));
    } else {
      // might need to use compareTo() method instead?
      assertThat(v2NodeDetail.getLastVolumeUpdateTime(), is(dbNodeDetail.lastVolumeUpdateTime));
    }
    assertThat(v2NodeDetail.getMachineImage(), is(dbNodeDetail.machineImage));
    if (v2NodeDetail.getMasterState() == null) {
      assertThat(dbNodeDetail.masterState, is(nullValue()));
    } else {
      assertThat(v2NodeDetail.getMasterState().getValue(), is(dbNodeDetail.masterState.name()));
    }
    assertThat(v2NodeDetail.getNodeIdx(), is(dbNodeDetail.nodeIdx));
    assertThat(v2NodeDetail.getNodeName(), is(dbNodeDetail.nodeName));
    assertThat(v2NodeDetail.getPlacementUuid(), is(dbNodeDetail.placementUuid));
    assertThat(v2NodeDetail.getSshPortOverride(), is(dbNodeDetail.sshPortOverride));
    assertThat(v2NodeDetail.getSshUserOverride(), is(dbNodeDetail.sshUserOverride));
    if (v2NodeDetail.getState() == null) {
      assertThat(dbNodeDetail.state, is(nullValue()));
    } else {
      assertThat(v2NodeDetail.getState().getValue(), is(dbNodeDetail.state.name()));
    }
    assertThat(v2NodeDetail.getYbPrebuiltAmi(), is(dbNodeDetail.ybPrebuiltAmi));
  }

  private void validateCloudSpecificInfo(
      CloudSpecificInfo v2CloudInfo, com.yugabyte.yw.models.helpers.CloudSpecificInfo dbCloudInfo) {
    if (v2CloudInfo == null) {
      assertThat(dbCloudInfo, is(nullValue()));
      return;
    }
    if (v2CloudInfo.getAssignPublicIp() == null) {
      assertThat(dbCloudInfo.assignPublicIP, is(true));
    } else {
      assertThat(v2CloudInfo.getAssignPublicIp(), is(dbCloudInfo.assignPublicIP));
    }
    assertThat(v2CloudInfo.getAz(), is(dbCloudInfo.az));
    assertThat(v2CloudInfo.getCloud(), is(dbCloudInfo.cloud));
    assertThat(v2CloudInfo.getInstanceType(), is(dbCloudInfo.instance_type));
    assertThat(v2CloudInfo.getKubernetesNamespace(), is(dbCloudInfo.kubernetesNamespace));
    assertThat(v2CloudInfo.getKubernetesPodName(), is(dbCloudInfo.kubernetesPodName));
    if (v2CloudInfo.getLunIndexes() != null) {
      assertThat(v2CloudInfo.getLunIndexes(), containsInAnyOrder(dbCloudInfo.lun_indexes));
    }
    assertThat(v2CloudInfo.getMountRoots(), is(dbCloudInfo.mount_roots));
    assertThat(v2CloudInfo.getPrivateDns(), is(dbCloudInfo.private_dns));
    assertThat(v2CloudInfo.getPrivateIp(), is(dbCloudInfo.private_ip));
    assertThat(v2CloudInfo.getPublicDns(), is(dbCloudInfo.public_dns));
    assertThat(v2CloudInfo.getPublicIp(), is(dbCloudInfo.public_ip));
    assertThat(v2CloudInfo.getRegion(), is(dbCloudInfo.region));
    assertThat(v2CloudInfo.getRootVolume(), is(dbCloudInfo.root_volume));
    assertThat(v2CloudInfo.getSecondaryPrivateIp(), is(dbCloudInfo.secondary_private_ip));
    assertThat(v2CloudInfo.getSecondarySubnetId(), is(dbCloudInfo.secondary_subnet_id));
    assertThat(v2CloudInfo.getSubnetId(), is(dbCloudInfo.subnet_id));
    assertThat(v2CloudInfo.getUseTimeSync(), is(dbCloudInfo.useTimeSync));
  }

  private void validateXClusterInfo(
      XClusterInfo getxClusterInfo,
      com.yugabyte.yw.forms.UniverseDefinitionTaskParams.XClusterInfo xClusterInfo) {
    // TODO Auto-generated method stub
  }

  protected void validateUniverseEditSpec(
      UniverseEditSpec universeEditSpec, UniverseConfigureTaskParams v1EditParams) {
    if (universeEditSpec.getExpectedUniverseVersion() == null) {
      assertThat(v1EditParams.expectedUniverseVersion, is(-1));
    } else {
      assertThat(
          universeEditSpec.getExpectedUniverseVersion(), is(v1EditParams.expectedUniverseVersion));
    }
    validateClustersEditSpec(universeEditSpec.getClusters(), v1EditParams.clusters);
  }

  protected void validateClustersEditSpec(
      List<ClusterEditSpec> v2Clusters, List<Cluster> dbClusters) {
    assertThat(v2Clusters.size(), is(dbClusters.size()));
    for (ClusterEditSpec v2Cluster : v2Clusters) {
      Cluster dbCluster =
          dbClusters.stream()
              .filter(c -> c.uuid.equals(v2Cluster.getUuid()))
              .findAny()
              .orElseThrow();
      validateClusterEditSpec(v2Cluster, dbCluster);
    }
  }

  protected void validateClusterEditSpec(ClusterEditSpec v2Cluster, Cluster dbCluster) {
    if (v2Cluster.getNumNodes() != null) {
      assertThat(v2Cluster.getNumNodes(), is(dbCluster.userIntent.numNodes));
    }
    if (v2Cluster.getInstanceType() != null) {
      assertThat(v2Cluster.getInstanceType(), is(dbCluster.userIntent.instanceType));
    }
    if (v2Cluster.getProviderSpec() != null) {
      validateProviderEditSpec(v2Cluster.getProviderSpec(), dbCluster);
    }
    if (v2Cluster.getPlacementSpec() != null) {
      validatePlacementSpec(v2Cluster.getPlacementSpec(), dbCluster.placementInfo);
    }
    if (v2Cluster.getStorageSpec() != null) {
      validateStorageSpec(v2Cluster.getStorageSpec(), dbCluster);
    }
    if (v2Cluster.getInstanceTags() != null) {
      validateInstanceTags(v2Cluster.getInstanceTags(), dbCluster.userIntent.instanceTags);
    }
  }

  protected void validateProviderEditSpec(
      ClusterProviderEditSpec v2ProviderEditSpec, Cluster dbCluster) {
    if (v2ProviderEditSpec.getRegionList() != null) {
      assertThat(v2ProviderEditSpec.getRegionList(), is(dbCluster.userIntent.regionList));
    }
  }
}
