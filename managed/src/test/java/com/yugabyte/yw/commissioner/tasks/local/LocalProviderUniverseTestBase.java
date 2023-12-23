// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.local;

import static com.yugabyte.yw.commissioner.tasks.CommissionerBaseTest.waitForTask;
import static com.yugabyte.yw.common.TestHelper.testDatabase;
import static com.yugabyte.yw.common.Util.YUGABYTE_DB;
import static com.yugabyte.yw.forms.UniverseConfigureTaskParams.ClusterOperationType.CREATE;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.collect.ImmutableList;
import com.yugabyte.yw.cloud.PublicCloudConstants;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.LocalNodeManager;
import com.yugabyte.yw.common.LocalNodeUniverseManager;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.NodeUIApiHelper;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.common.PlatformGuiceApplicationBaseTest;
import com.yugabyte.yw.common.ReleaseManager;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.YcqlQueryExecutor;
import com.yugabyte.yw.common.backuprestore.BackupHelper;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.gflags.GFlagsUtil;
import com.yugabyte.yw.common.gflags.SpecificGFlags;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.common.utils.Pair;
import com.yugabyte.yw.controllers.handlers.UniverseCRUDHandler;
import com.yugabyte.yw.controllers.handlers.UniverseTableHandler;
import com.yugabyte.yw.controllers.handlers.UpgradeUniverseHandler;
import com.yugabyte.yw.forms.RestartTaskParams;
import com.yugabyte.yw.forms.RunQueryFormData;
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
import com.yugabyte.yw.models.helpers.CloudInfoInterface;
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
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Optional;
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
import org.apache.commons.lang3.StringUtils;
import org.junit.Before;
import org.junit.BeforeClass;
import org.junit.Rule;
import org.junit.rules.TestWatcher;
import org.junit.runner.Description;
import org.yb.CommonTypes.TableType;
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
  protected static String YBC_VERSION;
  private static final String DOWNLOAD_URL =
      "https://downloads.yugabyte.com/releases/2.20.0.1/" + "yugabyte-2.20.0.1-b1-%s-%s.tar.gz";
  private static final String YBC_BASE_S3_URL = "https://downloads.yugabyte.com/ybc/";
  private static final String YBC_BIN_ENV_KEY = "YBC_PATH";
  private static final boolean KEEP_FAILED_UNIVERSE = false;
  private static List<String> toCleanDirectories = ImmutableList.of("yugabyte_backup");

  public static Map<String, String> GFLAGS = new HashMap<>();

  static {
    GFLAGS.put("load_balancer_max_over_replicated_tablets", "15");
    GFLAGS.put("load_balancer_max_concurrent_adds", "15");
    GFLAGS.put("load_balancer_max_concurrent_removals", "15");
    GFLAGS.put("transaction_table_num_tablets", "3");
    GFLAGS.put(GFlagsUtil.LOAD_BALANCER_INITIAL_DELAY_SECS, "120");
    GFLAGS.put("tmp_dir", "/tmp/testing");
  }

  public Map<String, String> getYbcGFlags(UniverseDefinitionTaskParams.UserIntent userIntent) {
    Map<String, String> ybcGFlags = new HashMap<>();
    Provider provider = Provider.getOrBadRequest(UUID.fromString(userIntent.provider));
    LocalCloudInfo cloudInfo = CloudInfoInterface.get(provider);
    String baseBinDir = cloudInfo.getYugabyteBinDir();
    File binDirectory = new File(baseBinDir);

    ybcGFlags.put("ysqlsh", baseBinDir + "/ysqlsh");
    ybcGFlags.put("ycqlsh", baseBinDir + "/ycqlsh");
    ybcGFlags.put("yb_admin", baseBinDir + "/yb-admin");
    ybcGFlags.put("yb_ctl", baseBinDir + "/yb-ctl");
    ybcGFlags.put("ysql_dump", binDirectory.getParent() + "/postgres/bin/ysql_dump");
    ybcGFlags.put("ysql_dumpall", binDirectory.getParent() + "/postgres/bin/ysql_dumpall");

    return ybcGFlags;
  }

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
  protected static String ybcBinPath;
  protected static String baseDir;
  protected static String arch;
  protected static String os;
  protected static String subDir;
  protected static String testName;

  protected LocalNodeManager localNodeManager;
  protected LocalNodeUniverseManager localNodeUniverseManager;
  protected UniverseCRUDHandler universeCRUDHandler;
  protected UpgradeUniverseHandler upgradeUniverseHandler;
  protected NodeUIApiHelper nodeUIApiHelper;
  protected YBClientService ybClientService;
  protected RuntimeConfGetter confGetter;
  protected BackupHelper backupHelper;
  protected YcqlQueryExecutor ycqlQueryExecutor;
  protected UniverseTableHandler tableHandler;

  @BeforeClass
  public static void setUpEnv() {
    log.debug("Setting up environment");
    os = IS_LINUX ? "linux" : "darwin";
    String systemArch = System.getProperty("os.arch");
    setUpBaseDir();

    if (systemArch.equalsIgnoreCase("aarch64")) {
      arch = "arm64";
    } else {
      arch = "x86_64";
    }

    setUpYBSoftware(os, arch);
    subDir = DATE_FORMAT.format(new Date());
  }

  private static void setUpBaseDir() {
    if (System.getenv(BASE_DIR_ENV_KEY) != null) {
      baseDir = System.getenv(BASE_DIR_ENV_KEY);
    } else {
      baseDir = DEFAULT_BASE_DIR;
    }
  }

  private static void setUpYBSoftware(String os, String arch) {
    String downloadURL = String.format(DOWNLOAD_URL, os, arch);
    log.debug("Using base dir {}", baseDir);

    ybBinPath = determineYBBinPath();
    if (ybBinPath == null) {
      downloadAndSetUpYBSoftware(os, arch, downloadURL);
    }

    log.debug("YB version {} bin path {}", ybVersion, ybBinPath);
  }

  private void setUpYBCSoftware(String os, String arch) {
    ybcBinPath = System.getenv(YBC_BIN_ENV_KEY);
    if (ybcBinPath == null) {
      String ybcVersion = confGetter.getGlobalConf(GlobalConfKeys.ybcStableVersion);
      YBC_VERSION = ybcVersion;
      validateYBCVersion(ybcVersion);
      log.debug("ybc Version to use {}", ybcVersion);
      downloadAndSetUpYBCSoftware(os, arch, ybcVersion);
    }

    log.debug("Using ybc binaries from path {}", ybcBinPath);
  }

  private static String determineYBBinPath() {
    if (System.getenv(YB_PATH_ENV_KEY) != null) {
      File binFile = new File(System.getenv(YB_PATH_ENV_KEY), "bin");
      if (binFile.exists()) {
        ybVersion = binFile.getParentFile().getName().replaceAll("yugabyte-", "");
        return binFile.getPath();
      }
    }
    return null;
  }

  private static void downloadAndSetUpYBSoftware(String os, String arch, String downloadURL) {
    String baseDownloadPath = baseDir + "/yugabyte";
    File downloadPathFile = new File(baseDownloadPath);

    for (File child : Optional.ofNullable(downloadPathFile.listFiles()).orElse(new File[0])) {
      if (child.getName().startsWith("yugabyte-") && hasRequiredExecutables(child)) {
        ybVersion = extractVersionFromFolder(child.getName());
        ybBinPath = new File(child, "bin").getPath();
        log.debug("Already downloaded!");
        return;
      }
    }

    String filePath = baseDownloadPath + "/yugabyte.tar.gz";
    try {
      downloadFromUrl(filePath, downloadURL);
      ybVersion = extractVersionFromURL(downloadURL);
      ybBinPath = baseDownloadPath + "/yugabyte-" + ybVersion + "/bin";
      String ybReleasePath = baseDownloadPath + "/%s-%s-%s.tar.gz";
      ybReleasePath = String.format(ybReleasePath, ybVersion, os, arch);
      Files.move(Paths.get(filePath), Paths.get(ybReleasePath));
      runPostInstallScript(ybBinPath);
    } catch (IOException | InterruptedException e) {
      throw new RuntimeException("Failed to download package", e);
    }
  }

  private static boolean hasRequiredExecutables(File folder) {
    File binFolder = new File(folder, "bin");
    if (binFolder.exists()) {
      Set<String> neededExecutables = new HashSet<>(CONTROL_FILES);
      for (File file : Optional.ofNullable(binFolder.listFiles()).orElse(new File[0])) {
        neededExecutables.remove(file.getName());
      }
      return neededExecutables.isEmpty();
    }
    return false;
  }

  private static void runPostInstallScript(String binPath)
      throws IOException, InterruptedException {
    ProcessBuilder pb = new ProcessBuilder("./post_install.sh").directory(new File(binPath));
    Process proc = pb.start();
    int res = proc.waitFor();
    if (res != 0) {
      log.error(
          "post_install exited with {} and output \r\n {}",
          res,
          new String(proc.getErrorStream().readAllBytes()));
      throw new IllegalStateException();
    }
  }

  private void downloadAndSetUpYBCSoftware(String os, String arch, String ybcVersion) {
    String ybcS3URL = YBC_BASE_S3_URL + ybcVersion + "/ybc-" + ybcVersion + "-%s-%s.tar.gz";
    String ybcDownloadURL = String.format(ybcS3URL, os, arch);
    String ybcBaseDir = baseDir + "/ybc/ybc-" + ybcVersion + "-%s-%s.tar.gz";
    String ybaBaseDownloadDir = String.format(ybcBaseDir, os, arch);
    try {
      downloadFromUrl(ybaBaseDownloadDir, ybcDownloadURL);
      String ybcLibPath = String.format("/ybc-%s-%s-%s", ybcVersion, os, arch);
      ybcBinPath = baseDir + "/ybc" + ybcLibPath + "/bin";
      Files.delete(Paths.get(ybaBaseDownloadDir));
      log.info("YBC extracted successfully.");
    } catch (IOException e) {
      throw new RuntimeException("Failed to download package", e);
    }
  }

  private static void downloadFromUrl(String baseDownloadPath, String downloadURL) {
    File downloadPathDir = new File(baseDownloadPath).getParentFile();
    try (InputStream in = new URL(downloadURL).openStream()) {
      downloadPathDir.mkdirs();
      Files.copy(in, Paths.get(baseDownloadPath), StandardCopyOption.REPLACE_EXISTING);
      log.debug("downloaded to {}", baseDownloadPath);
      Path destination = downloadPathDir.toPath();
      try (TarArchiveInputStream tarInput =
          new TarArchiveInputStream(
              new GzipCompressorInputStream(new FileInputStream(baseDownloadPath)))) {
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
    } catch (IOException e) {
      throw new RuntimeException("Failed to download package", e);
    }
  }

  private static void validateYBCVersion(String ybcVersion) {
    if (StringUtils.isEmpty(ybcVersion)) {
      throw new RuntimeException("YBC version is not configured. Can't continue");
    }
  }

  private static String extractVersionFromFolder(String folder) {
    return folder.substring(9); // yugabyte-2.18.3.0 for example
  }

  private static String extractVersionFromURL(String URL) {
    return URL.substring(40, 48);
  }

  @Before
  public void setUp() {
    universeCRUDHandler = app.injector().instanceOf(UniverseCRUDHandler.class);
    upgradeUniverseHandler = app.injector().instanceOf(UpgradeUniverseHandler.class);
    nodeUIApiHelper = app.injector().instanceOf(NodeUIApiHelper.class);
    localNodeManager = app.injector().instanceOf(LocalNodeManager.class);
    localNodeUniverseManager = app.injector().instanceOf(LocalNodeUniverseManager.class);
    ybClientService = app.injector().instanceOf(YBClientService.class);
    confGetter = app.injector().instanceOf(RuntimeConfGetter.class);
    backupHelper = app.injector().instanceOf(BackupHelper.class);
    ycqlQueryExecutor = app.injector().instanceOf(YcqlQueryExecutor.class);
    tableHandler = app.injector().instanceOf(UniverseTableHandler.class);

    Pair<Integer, Integer> ipRange = getIpRange();
    localNodeManager.setIpRangeStart(ipRange.getFirst());
    localNodeManager.setIpRangeEnd(ipRange.getSecond());

    setUpYBCSoftware(os, arch);
    File baseDirFile = new File(baseDir);
    File curDir = new File(baseDirFile, subDir);
    if (!baseDirFile.exists() || !curDir.exists()) {
      curDir.mkdirs();
      if (!KEEP_FAILED_UNIVERSE) {
        curDir.deleteOnExit();
      }
    }
    File testDir = new File(curDir, testName);
    testDir.mkdirs();

    YugawareProperty.addConfigProperty(
        ReleaseManager.CONFIG_TYPE.name(), getMetadataJson(ybVersion, false), "release");
    YugawareProperty.addConfigProperty(
        ReleaseManager.YBC_CONFIG_TYPE.name(),
        getMetadataJson("ybc-" + YBC_VERSION, true),
        "release");

    customer = ModelFactory.testCustomer();
    user = ModelFactory.testUser(customer);
    LocalCloudInfo localCloudInfo = new LocalCloudInfo();
    localCloudInfo.setDataHomeDir(testDir.toString());
    localCloudInfo.setYugabyteBinDir(ybBinPath);
    localCloudInfo.setYbcBinDir(ybcBinPath);
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

  /**
   * Range of IPs to use. Two last bytes of each number are used to form 127.0.x.y ip
   *
   * @return
   */
  protected abstract Pair<Integer, Integer> getIpRange();

  private JsonNode getMetadataJson(String release, boolean isYbc) {
    ReleaseManager.ReleaseMetadata releaseMetadata = new ReleaseManager.ReleaseMetadata();
    releaseMetadata.state = ReleaseManager.ReleaseState.ACTIVE;
    String parentDirectory = "/";
    if (!isYbc) {
      parentDirectory = "/yugabyte/";
    }
    releaseMetadata.filePath =
        baseDir + parentDirectory + release + "-" + os + "-" + arch + ".tar.gz";
    ReleaseManager.ReleaseMetadata.Package pkg = new ReleaseManager.ReleaseMetadata.Package();
    pkg.arch = PublicCloudConstants.Architecture.valueOf(arch);
    pkg.path = releaseMetadata.filePath;
    releaseMetadata.packages = Collections.singletonList(pkg);
    ObjectNode object = Json.newObject();
    if (isYbc) {
      release += "-" + os + "-" + arch;
    }
    object.set(release, Json.toJson(releaseMetadata));
    return object;
  }

  @Rule(order = Integer.MIN_VALUE)
  public TestWatcher testWatcher =
      new TestWatcher() {

        @Override
        protected void starting(Description description) {
          testName =
              description.getClassName().replaceAll(".*\\.", "")
                  + "_"
                  + description.getMethodName();
        }

        @Override
        protected void succeeded(Description description) {
          tearDown(false);
        }

        @Override
        protected void failed(Throwable e, Description description) {
          tearDown(true);
        }
      };

  private void tearDown(boolean failed) {
    log.error("tear down " + testName + " failed " + failed);
    if (!failed || !KEEP_FAILED_UNIVERSE) {
      localNodeManager.shutdown();
      try {
        for (String dirName : toCleanDirectories) {
          String path = baseDir + "/" + dirName;
          File directory = new File(path);
          if (directory.exists()) {
            FileUtils.deleteDirectory(directory);
          }
        }
        FileUtils.deleteDirectory(new File(new File(new File(baseDir), subDir), testName));
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
    return getDefaultUserIntent(null, false);
  }

  protected UniverseDefinitionTaskParams.UserIntent getDefaultUserIntent(
      String univName, boolean disableTls) {
    UniverseDefinitionTaskParams.UserIntent userIntent =
        ApiUtils.getTestUserIntent(region, provider, instanceType, 3);
    userIntent.universeName = "test-universe";
    if (univName != null) {
      userIntent.universeName = univName;
    }
    userIntent.ybSoftwareVersion = ybVersion;
    userIntent.accessKeyCode = accessKey.getKeyCode();
    if (!disableTls) {
      userIntent.enableNodeToNodeEncrypt = true;
      userIntent.enableClientToNodeEncrypt = true;
    }
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

  protected Universe createUniverseWithYbc(UniverseDefinitionTaskParams.UserIntent userIntent)
      throws InterruptedException {
    return createUniverse(
        userIntent,
        (x) -> {
          x.setEnableYbc(true);
          x.setYbcSoftwareVersion(LocalProviderUniverseTestBase.YBC_VERSION);
        });
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
    Universe result = Universe.getOrBadRequest(universeResp.universeUUID);
    assertEquals(
        result.getUniverseDetails().getPrimaryCluster().userIntent.masterGFlags,
        GFlagsUtil.getBaseGFlags(
            UniverseTaskBase.ServerType.MASTER,
            result.getUniverseDetails().getPrimaryCluster(),
            result.getUniverseDetails().clusters));
    assertEquals(
        result.getUniverseDetails().getPrimaryCluster().userIntent.tserverGFlags,
        GFlagsUtil.getBaseGFlags(
            UniverseTaskBase.ServerType.TSERVER,
            result.getUniverseDetails().getPrimaryCluster(),
            result.getUniverseDetails().clusters));
    return result;
  }

  protected void initYSQL(Universe universe) {
    initYSQL(universe, "some_table", false);
  }

  protected void initYSQL(Universe universe, String tableName) {
    initYSQL(universe, tableName, false);
  }

  protected void initYSQL(Universe universe, String tableName, boolean authEnabled) {
    NodeDetails nodeDetails = universe.getUniverseDetails().nodeDetailsSet.iterator().next();
    if (StringUtils.isBlank(tableName)) {
      tableName = "some_table";
    }
    ShellResponse response =
        localNodeUniverseManager.runYsqlCommand(
            nodeDetails,
            universe,
            YUGABYTE_DB,
            String.format(
                "CREATE TABLE %s (id int, name text, age int, PRIMARY KEY(id, name))", tableName),
            10,
            authEnabled);
    assertTrue(response.isSuccess());
    response =
        localNodeUniverseManager.runYsqlCommand(
            nodeDetails,
            universe,
            YUGABYTE_DB,
            String.format(
                "insert into %s values (1, 'John', 20), "
                    + "(2, 'Mary', 18), (10000, 'Stephen', 50)",
                tableName),
            10,
            authEnabled);
    assertTrue(response.isSuccess());
  }

  protected void verifyYSQL(Universe universe) {
    verifyYSQL(universe, false);
  }

  protected void verifyYSQL(Universe universe, boolean readFromRR) {
    verifyYSQL(universe, readFromRR, YUGABYTE_DB);
  }

  protected void verifyYSQL(Universe universe, boolean readFromRR, String dbName) {
    verifyYSQL(universe, readFromRR, dbName, "some_table");
  }

  protected void verifyYSQL(
      Universe universe, boolean readFromRR, String dbName, String tableName) {
    verifyYSQL(universe, readFromRR, dbName, tableName, false);
  }

  protected void verifyYSQL(
      Universe universe, boolean readFromRR, String dbName, String tableName, boolean authEnabled) {
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    if (StringUtils.isBlank(tableName)) {
      tableName = "some_table";
    }
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
        localNodeUniverseManager.runYsqlCommand(
            nodeDetails,
            universe,
            dbName,
            String.format("select count(*) from %s", tableName),
            10,
            authEnabled);
    assertTrue(response.isSuccess());
    assertEquals("3", LocalNodeManager.getRawCommandOutput(response.getMessage()));
  }

  protected void initYCQL(Universe universe) {
    initYCQL(universe, false, "");
  }

  protected void initYCQL(Universe universe, boolean authEnabled, String password) {
    RunQueryFormData formData = new RunQueryFormData();
    // Create `yugabyte` keyspace.
    formData.query = "CREATE KEYSPACE IF NOT EXISTS yugabyte;";
    formData.tableType = TableType.YQL_TABLE_TYPE;
    JsonNode response =
        ycqlQueryExecutor.executeQuery(
            universe, formData, true, Util.DEFAULT_YCQL_USERNAME, password);
    assertFalse(response.has("error"));

    // Create table.
    formData.query =
        "CREATE TABLE yugabyte.some_table (id int, name text, age int, PRIMARY KEY((id, name)));";
    response =
        ycqlQueryExecutor.executeQuery(
            universe, formData, true, Util.DEFAULT_YCQL_USERNAME, password);
    assertFalse(response.has("error"));

    // Insert Data.
    formData.query = "INSERT INTO yugabyte.some_table (id, name, age) VALUES (1, 'John', 20);";
    response =
        ycqlQueryExecutor.executeQuery(
            universe, formData, true, Util.DEFAULT_YCQL_USERNAME, password);
    assertFalse(response.has("error"));

    formData.query = "INSERT INTO yugabyte.some_table (id, name, age) VALUES (2, 'Mary', 18);";
    response =
        ycqlQueryExecutor.executeQuery(
            universe, formData, true, Util.DEFAULT_YCQL_USERNAME, password);
    assertFalse(response.has("error"));

    formData.query =
        "INSERT INTO yugabyte.some_table (id, name, age) VALUES (10000, 'Stephen', 50);";
    response =
        ycqlQueryExecutor.executeQuery(
            universe, formData, true, Util.DEFAULT_YCQL_USERNAME, password);
    assertFalse(response.has("error"));
  }

  protected void verifyYCQL(Universe universe) {
    verifyYCQL(universe, false, "");
  }

  protected void verifyYCQL(Universe universe, boolean authEnabled, String password) {
    RunQueryFormData formData = new RunQueryFormData();
    formData.query = "select count(*) from yugabyte.some_table;";
    formData.tableType = TableType.YQL_TABLE_TYPE;

    JsonNode response =
        ycqlQueryExecutor.executeQuery(
            universe, formData, true, Util.DEFAULT_YCQL_USERNAME, password);
    assertFalse(response.has("error"));
    assertEquals("3", response.get("result").get(0).get("count").asText());
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
