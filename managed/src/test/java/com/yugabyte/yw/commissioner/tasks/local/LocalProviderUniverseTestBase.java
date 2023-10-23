// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.local;

import static com.yugabyte.yw.commissioner.tasks.CommissionerBaseTest.waitForTask;
import static com.yugabyte.yw.common.TestHelper.testDatabase;
import static com.yugabyte.yw.common.Util.YUGABYTE_DB;
import static com.yugabyte.yw.forms.UniverseConfigureTaskParams.ClusterOperationType.CREATE;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.cloud.PublicCloudConstants;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.LocalNodeManager;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.NodeUIApiHelper;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.common.PlatformGuiceApplicationBaseTest;
import com.yugabyte.yw.common.ReleaseManager;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.gflags.SpecificGFlags;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.controllers.handlers.UniverseCRUDHandler;
import com.yugabyte.yw.controllers.handlers.UpgradeUniverseHandler;
import com.yugabyte.yw.forms.RestartTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseResp;
import com.yugabyte.yw.forms.UpgradeTaskParams;
import com.yugabyte.yw.models.AccessKey;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.InstanceType;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.ProviderDetails;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.YugawareProperty;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.provider.LocalCloudInfo;
import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.net.URL;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.nio.file.StandardCopyOption;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.Date;
import java.util.HashSet;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.function.Consumer;
import java.util.stream.Collectors;
import kamon.instrumentation.play.GuiceModule;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.compress.archivers.tar.TarArchiveEntry;
import org.apache.commons.compress.archivers.tar.TarArchiveInputStream;
import org.apache.commons.compress.compressors.gzip.GzipCompressorInputStream;
import org.apache.commons.io.FileUtils;
import org.junit.After;
import org.junit.AfterClass;
import org.junit.Before;
import org.junit.BeforeClass;
import org.yb.client.GetMasterClusterConfigResponse;
import org.yb.client.YBClient;
import org.yb.master.CatalogEntityInfo;
import play.Application;
import play.inject.guice.GuiceApplicationBuilder;
import play.libs.Json;

@Slf4j
public abstract class LocalProviderUniverseTestBase extends PlatformGuiceApplicationBaseTest {
  private static final boolean IS_LINUX = System.getProperty("os.name").equalsIgnoreCase("linux");
  private static final Set<String> CONTROL_FILES =
      Set.of(LocalNodeManager.MASTER_EXECUTABLE, LocalNodeManager.TSERVER_EXECUTABLE);
  private static final SimpleDateFormat DATE_FORMAT = new SimpleDateFormat("yyyyMMdd'T'HHmmss.SSS");

  protected static final String INSTANCE_TYPE_CODE = "c3.xlarge";
  protected static final String INSTANCE_TYPE_CODE_2 = "c5.xlarge";

  private static final String YB_PATH_ENV_KEY = "YB_PATH";
  private static final String BASE_DIR_ENV_KEY = "TEST_BASE_DIR";

  private static final String DEFAULT_BASE_DIR = "/tmp/testing";
  private static final String YBC_VERSION = "2.0.0.0-b10"; // TODO
  private static final String DOWNLOAD_URL =
      "https://downloads.yugabyte.com/releases/2.19.2.0/" + "yugabyte-2.19.2.0-b121-%s-%s.tar.gz";
  private static final boolean KEEP_UNIVERSE = false;

  protected Customer customer;
  protected Provider provider;
  protected Users user;
  protected AccessKey accessKey;
  protected Region region;
  protected AvailabilityZone az1;
  protected AvailabilityZone az2;
  protected AvailabilityZone az3;
  protected AvailabilityZone az4;
  protected InstanceType instanceType;
  protected InstanceType instanceType2;

  protected static String ybVersion;
  protected static String ybBinPath;
  protected static String baseDir;
  protected static String arch;
  protected static String os;
  protected static String subDir;

  protected LocalNodeManager localNodeManager;
  protected UniverseCRUDHandler universeCRUDHandler;
  protected UpgradeUniverseHandler upgradeUniverseHandler;
  protected NodeUIApiHelper nodeUIApiHelper;
  protected YBClientService ybClientService;

  @BeforeClass
  public static void setUpEnv() {
    log.debug("Setting up environment");
    os = IS_LINUX ? "linux" : "darwin";
    String systemArch = System.getProperty("os.arch");

    if (systemArch.equalsIgnoreCase("aarch64")) {
      arch = "arm64";
    } else {
      arch = "x86_64";
    }

    String downloadURL = String.format(DOWNLOAD_URL, os, arch);
    if (System.getenv(BASE_DIR_ENV_KEY) != null) {
      baseDir = System.getenv(BASE_DIR_ENV_KEY);
    } else {
      baseDir = DEFAULT_BASE_DIR;
    }
    log.debug("Using base dir {}", baseDir);
    if (System.getenv(YB_PATH_ENV_KEY) != null) {
      ybBinPath = System.getenv(YB_PATH_ENV_KEY) + "/bin";
      File binFile = new File(ybBinPath);
      ybVersion = binFile.getParentFile().getName().replaceAll("yugabyte-", "");
    } else {
      log.debug("Downloading and extracting " + downloadURL);
      String baseDownloadPath = baseDir + "/yugabyte";
      File downloadPathFile = new File(baseDownloadPath);
      boolean alreadyDownloaded = false;
      if (downloadPathFile.exists()) {
        for (File child : downloadPathFile.listFiles()) {
          if (child.getName().startsWith("yugabyte-")) {
            File binFolder = new File(child, "bin");
            if (binFolder.exists()) {
              Set<String> neededExecutables = new HashSet<>(CONTROL_FILES);
              for (File file : binFolder.listFiles()) {
                neededExecutables.remove(file.getName());
              }
              if (neededExecutables.isEmpty()) {
                log.debug("Already downloaded!");
                alreadyDownloaded = true;
                ybVersion = extractVersionFromFolder(child.getName());
                ybBinPath = binFolder.getPath();
              }
            }
          }
        }
      }
      if (!alreadyDownloaded) {
        downloadFromUrl(baseDownloadPath, downloadURL);
      }
    }
    log.debug("YB version {} bin path {}", ybVersion, ybBinPath);
    subDir = DATE_FORMAT.format(new Date());
  }

  private static void downloadFromUrl(String baseDownloadPath, String downloadURL) {
    String fileName = baseDownloadPath + "/yugabyte.tar.gz";
    File downloadPathDir = new File(baseDownloadPath);
    try (InputStream in = new URL(downloadURL).openStream()) {
      downloadPathDir.mkdirs();
      Files.copy(in, Paths.get(fileName), StandardCopyOption.REPLACE_EXISTING);
      log.debug("downloaded to {}", fileName);
      Path destination = downloadPathDir.toPath();
      try (TarArchiveInputStream tarInput =
          new TarArchiveInputStream(new GzipCompressorInputStream(new FileInputStream(fileName)))) {
        TarArchiveEntry currentEntry;
        while ((currentEntry = tarInput.getNextTarEntry()) != null) {
          Path extractTo = destination.resolve(currentEntry.getName());
          if (currentEntry.isDirectory()) {
            Files.createDirectories(extractTo);
          } else if (currentEntry.isSymbolicLink()) {
            Files.createSymbolicLink(extractTo, Path.of(currentEntry.getLinkName()));
          } else {
            Files.copy(tarInput, extractTo, StandardCopyOption.REPLACE_EXISTING);
            int mode = currentEntry.getMode();
            if ((mode & 0100) != 0) {
              extractTo.toFile().setExecutable(true);
            }
          }
        }
      }
      Files.delete(Paths.get(fileName));
      ybVersion = extractVersionFromURL(downloadURL);
      ybBinPath = baseDownloadPath + "/yugabyte-" + ybVersion + "/bin";
      ProcessBuilder pb = new ProcessBuilder("./post_install.sh").directory(new File(ybBinPath));
      Process proc = pb.start();
      int res = proc.waitFor();
      if (res != 0) {
        log.error(
            "post_install exited with {} and output \r\n {}",
            res,
            new String(proc.getErrorStream().readAllBytes()));
        throw new IllegalStateException();
      }
    } catch (InterruptedException | IOException e) {
      throw new RuntimeException("Failed to download package", e);
    }
  }

  private static String extractVersionFromFolder(String folder) {
    return folder.substring(9); // yugabyte-2.18.3.0 for example
  }

  private static String extractVersionFromURL(String URL) {
    return URL.substring(40, 48);
  }

  @AfterClass
  public static void tearDownEnv() {}

  @Before
  public void setUp() {
    universeCRUDHandler = app.injector().instanceOf(UniverseCRUDHandler.class);
    upgradeUniverseHandler = app.injector().instanceOf(UpgradeUniverseHandler.class);
    nodeUIApiHelper = app.injector().instanceOf(NodeUIApiHelper.class);
    localNodeManager = app.injector().instanceOf(LocalNodeManager.class);
    ybClientService = app.injector().instanceOf(YBClientService.class);

    File baseDirFile = new File(baseDir);
    File curDir = new File(baseDirFile, subDir);
    if (baseDirFile.exists()) {
      for (File child : baseDirFile.listFiles()) {
        if (child.getName().equals(subDir) && !KEEP_UNIVERSE) {
          try {
            FileUtils.deleteDirectory(child);
          } catch (IOException ignored) {
          }
        }
      }
    }
    curDir.mkdirs();
    if (!KEEP_UNIVERSE) {
      curDir.deleteOnExit();
    }

    YugawareProperty.addConfigProperty(
        ReleaseManager.CONFIG_TYPE.name(), getMetadataJson(ybVersion), "release");
    YugawareProperty.addConfigProperty(
        ReleaseManager.YBC_CONFIG_TYPE.name(),
        getMetadataJson("ybc-" + YBC_VERSION + "-" + os + "-" + arch),
        "release");

    customer = ModelFactory.testCustomer();
    user = ModelFactory.testUser(customer);
    LocalCloudInfo localCloudInfo = new LocalCloudInfo();
    localCloudInfo.setDataHomeDir(curDir.toString());
    localCloudInfo.setYugabyteBinDir(ybBinPath);
    //    localCloudInfo.setYbcBinDir("TODO");
    ProviderDetails.CloudInfo cloudInfo = new ProviderDetails.CloudInfo();
    cloudInfo.setLocal(localCloudInfo);
    ProviderDetails providerDetails = new ProviderDetails();
    providerDetails.setCloudInfo(cloudInfo);
    provider = ModelFactory.newProvider(customer, Common.CloudType.local, providerDetails);
    AccessKey.KeyInfo keyInfo = new AccessKey.KeyInfo();
    String keyCode = "aws_access_code";
    keyInfo.sshUser = "ssh_user";
    keyInfo.sshPort = 22;
    accessKey = AccessKey.create(provider.getUuid(), keyCode, keyInfo);

    region = Region.create(provider, "region-1", "PlacementRegion 1", "default-image");
    az1 = AvailabilityZone.createOrThrow(region, "az-1", "PlacementAZ 1", "subnet-1");
    az2 = AvailabilityZone.createOrThrow(region, "az-2", "PlacementAZ 2", "subnet-2");
    az3 = AvailabilityZone.createOrThrow(region, "az-3", "PlacementAZ 3", "subnet-3");
    az4 = AvailabilityZone.createOrThrow(region, "az-4", "PlacementAZ 4", "subnet-4");

    instanceType =
        InstanceType.upsert(
            provider.getUuid(),
            INSTANCE_TYPE_CODE,
            10,
            5.5,
            new InstanceType.InstanceTypeDetails());
    instanceType2 =
        InstanceType.upsert(
            provider.getUuid(),
            INSTANCE_TYPE_CODE_2,
            12,
            5.5,
            new InstanceType.InstanceTypeDetails());
  }

  private JsonNode getMetadataJson(String release) {
    ReleaseManager.ReleaseMetadata releaseMetadata = new ReleaseManager.ReleaseMetadata();
    releaseMetadata.state = ReleaseManager.ReleaseState.ACTIVE;
    releaseMetadata.filePath = "/tmp" + release + ".tar.gz";
    ReleaseManager.ReleaseMetadata.Package pkg = new ReleaseManager.ReleaseMetadata.Package();
    pkg.arch = PublicCloudConstants.Architecture.valueOf(arch);
    pkg.path = releaseMetadata.filePath;
    releaseMetadata.packages = Collections.singletonList(pkg);
    ObjectNode object = Json.newObject();
    object.set(release, Json.toJson(releaseMetadata));
    return object;
  }

  @After
  public void tearDown() {
    if (!KEEP_UNIVERSE) {
      localNodeManager.shutdown();
      try {
        FileUtils.deleteDirectory(new File(new File(baseDir), subDir));
      } catch (Exception ignored) {
      }
    }
  }

  @Override
  protected Application provideApplication() {
    return configureApplication(
            new GuiceApplicationBuilder().disable(GuiceModule.class).configure(testDatabase()))
        .build();
  }

  protected Universe createUniverse(int numNodes, int replicationFactor)
      throws InterruptedException {
    return createUniverse(
        userIntent -> {
          userIntent.replicationFactor = replicationFactor;
          userIntent.numNodes = numNodes;
        });
  }

  protected UniverseDefinitionTaskParams.UserIntent getDefaultUserIntent() {
    UniverseDefinitionTaskParams.UserIntent userIntent =
        ApiUtils.getTestUserIntent(region, provider, instanceType, 3);
    userIntent.universeName = "test-universe";
    userIntent.ybSoftwareVersion = ybVersion;
    userIntent.accessKeyCode = accessKey.getKeyCode();
    userIntent.enableNodeToNodeEncrypt = true;
    userIntent.enableClientToNodeEncrypt = true;
    userIntent.specificGFlags =
        SpecificGFlags.construct(
            Map.of("transaction_table_num_tablets", "3"),
            Map.of("transaction_table_num_tablets", "3"));
    userIntent.deviceInfo.storageType = PublicCloudConstants.StorageType.Local;
    return userIntent;
  }

  protected Universe createUniverse(
      Consumer<UniverseDefinitionTaskParams.UserIntent> intentModifier)
      throws InterruptedException {
    UniverseDefinitionTaskParams.UserIntent userIntent = getDefaultUserIntent();
    intentModifier.accept(userIntent);
    return createUniverse(userIntent);
  }

  protected Universe createUniverse(UniverseDefinitionTaskParams.UserIntent userIntent)
      throws InterruptedException {
    return createUniverse(userIntent, (x) -> {});
  }

  protected Universe createUniverse(
      UniverseDefinitionTaskParams.UserIntent userIntent,
      Consumer<UniverseDefinitionTaskParams> paramsCustomizer)
      throws InterruptedException {
    UniverseDefinitionTaskParams taskParams = new UniverseDefinitionTaskParams();
    taskParams.nodePrefix = "univConfCreate";
    taskParams.upsertPrimaryCluster(userIntent, null);
    PlacementInfoUtil.updateUniverseDefinition(
        taskParams, customer.getId(), taskParams.getPrimaryCluster().uuid, CREATE);
    taskParams.expectedUniverseVersion = -1;
    paramsCustomizer.accept(taskParams);
    // CREATE
    UniverseResp universeResp = universeCRUDHandler.createUniverse(customer, taskParams);
    TaskInfo taskInfo = waitForTask(universeResp.taskUUID);
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    return Universe.getOrBadRequest(universeResp.universeUUID);
  }

  protected void initYSQL(Universe universe) {
    NodeDetails nodeDetails = universe.getUniverseDetails().nodeDetailsSet.iterator().next();
    ShellResponse response =
        localNodeManager.runYsqlCommand(
            nodeDetails,
            universe,
            YUGABYTE_DB,
            "CREATE TABLE some_table (id int, name text, age int, PRIMARY KEY(id, name))",
            10);
    assertTrue(response.isSuccess());
    response =
        localNodeManager.runYsqlCommand(
            nodeDetails,
            universe,
            YUGABYTE_DB,
            "insert into some_table values (1, 'John', 20), "
                + "(2, 'Mary', 18), (10000, 'Stephen', 50)",
            10);
    assertTrue(response.isSuccess());
  }

  protected void verifyYSQL(Universe universe) {
    verifyYSQL(universe, false);
  }

  protected void verifyYSQL(Universe universe, boolean readFromRR) {
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    UUID cluserUUID =
        readFromRR
            ? universe.getUniverseDetails().getReadOnlyClusters().get(0).uuid
            : universe.getUniverseDetails().getPrimaryCluster().uuid;
    NodeDetails nodeDetails =
        universe.getUniverseDetails().nodeDetailsSet.stream()
            .filter(n -> n.isInPlacement(cluserUUID))
            .findFirst()
            .get();
    ShellResponse response =
        localNodeManager.runYsqlCommand(
            nodeDetails, universe, YUGABYTE_DB, "select count(*) from some_table", 10);
    assertTrue(response.isSuccess());
    assertEquals("3", LocalNodeManager.getRawCommandOutput(response.getMessage()));
  }

  protected TaskInfo doAddReadReplica(
      Universe universe, UniverseDefinitionTaskParams.UserIntent userIntent)
      throws InterruptedException {
    TaskInfo taskInfo = addReadReplica(universe, userIntent);
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    return taskInfo;
  }

  protected TaskInfo addReadReplica(
      Universe universe, UniverseDefinitionTaskParams.UserIntent userIntent)
      throws InterruptedException {
    UniverseDefinitionTaskParams.Cluster asyncCluster =
        new UniverseDefinitionTaskParams.Cluster(
            UniverseDefinitionTaskParams.ClusterType.ASYNC, userIntent);

    UniverseDefinitionTaskParams taskParams = new UniverseDefinitionTaskParams();
    taskParams.setUniverseUUID(universe.getUniverseUUID());
    taskParams.clusters =
        new ArrayList<>(
            Arrays.asList(universe.getUniverseDetails().getPrimaryCluster(), asyncCluster));
    taskParams.nodeDetailsSet = universe.getUniverseDetails().nodeDetailsSet;
    taskParams.expectedUniverseVersion = -1;

    PlacementInfoUtil.updateUniverseDefinition(
        taskParams, customer.getId(), asyncCluster.uuid, CREATE);

    taskParams.clusters =
        new ArrayList<>(Collections.singletonList(taskParams.getClusterByUuid(asyncCluster.uuid)));
    taskParams.nodeDetailsSet =
        taskParams.nodeDetailsSet.stream()
            .filter(n -> n.isInPlacement(asyncCluster.uuid))
            .collect(Collectors.toSet());
    taskParams.rootCA = universe.getUniverseDetails().rootCA;
    TaskInfo taskInfo =
        waitForTask(
            universeCRUDHandler.createCluster(
                customer, Universe.getOrBadRequest(universe.getUniverseUUID()), taskParams));
    return taskInfo;
  }

  protected TaskInfo destroyUniverse(Universe universe, Customer customer)
      throws InterruptedException {
    UUID taskID = universeCRUDHandler.destroy(customer, universe, true, false, false);
    TaskInfo taskInfo = waitForTask(taskID);
    return taskInfo;
  }

  protected void verifyUniverseState(Universe universe) {
    String certificate = universe.getCertificateNodetoNode();
    try (YBClient client = ybClientService.getClient(universe.getMasterAddresses(), certificate)) {
      GetMasterClusterConfigResponse masterClusterConfig = client.getMasterClusterConfig();
      CatalogEntityInfo.SysClusterConfigEntryPB config = masterClusterConfig.getConfig();
      UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
      UniverseDefinitionTaskParams.Cluster primaryCluster = universeDetails.getPrimaryCluster();
      CatalogEntityInfo.ReplicationInfoPB replicationInfo = config.getReplicationInfo();
      CatalogEntityInfo.PlacementInfoPB liveReplicas = replicationInfo.getLiveReplicas();
      verifyCluster(primaryCluster, liveReplicas);
      if (!universeDetails.getReadOnlyClusters().isEmpty()) {
        UniverseDefinitionTaskParams.Cluster asyncCluster =
            universeDetails.getReadOnlyClusters().get(0);
        CatalogEntityInfo.PlacementInfoPB readReplicas = replicationInfo.getReadReplicas(0);
        verifyCluster(asyncCluster, readReplicas);
      }
    } catch (Exception e) {
      throw new RuntimeException(e);
    }
  }

  protected void verifyCluster(
      UniverseDefinitionTaskParams.Cluster cluster, CatalogEntityInfo.PlacementInfoPB replicas) {
    assertEquals(cluster.uuid.toString(), replicas.getPlacementUuid().toStringUtf8());
    assertEquals(cluster.userIntent.replicationFactor, replicas.getNumReplicas());
    Map<UUID, Integer> nodeCount = localNodeManager.getNodeCount(cluster.uuid);
    Map<UUID, Integer> placementNodeCount =
        PlacementInfoUtil.getAzUuidToNumNodes(cluster.placementInfo);
    assertEquals(placementNodeCount, nodeCount);
    Set<String> zones =
        replicas.getPlacementBlocksList().stream()
            .map(pb -> pb.getCloudInfo().getPlacementZone())
            .collect(Collectors.toSet());
    Set<String> placementZones =
        cluster
            .placementInfo
            .azStream()
            .map(az -> AvailabilityZone.getOrBadRequest(az.uuid).getCode())
            .collect(Collectors.toSet());
    assertEquals(zones, placementZones);
  }

  protected void restartUniverse(Universe universe, boolean rolling) throws InterruptedException {
    RestartTaskParams restartTaskParams = new RestartTaskParams();
    restartTaskParams.setUniverseUUID(universe.getUniverseUUID());
    restartTaskParams.expectedUniverseVersion = -1;
    restartTaskParams.upgradeOption =
        rolling
            ? UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE
            : UpgradeTaskParams.UpgradeOption.NON_ROLLING_UPGRADE;
    restartTaskParams.clusters = universe.getUniverseDetails().clusters;
    UUID taskUUID = upgradeUniverseHandler.restartUniverse(restartTaskParams, customer, universe);
    TaskInfo taskInfo = waitForTask(taskUUID);
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
  }
}
