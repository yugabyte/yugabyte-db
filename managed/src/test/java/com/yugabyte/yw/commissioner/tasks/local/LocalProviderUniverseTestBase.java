// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.local;

import static com.yugabyte.yw.common.TestHelper.testDatabase;
import static com.yugabyte.yw.common.Util.YUGABYTE_DB;
import static com.yugabyte.yw.forms.UniverseConfigureTaskParams.ClusterOperationType.CREATE;
import static org.hamcrest.MatcherAssert.assertThat;
import static org.hamcrest.Matchers.lessThan;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.api.client.util.Throwables;
import com.google.common.base.Stopwatch;
import com.google.common.net.HostAndPort;
import com.yugabyte.yw.cloud.PublicCloudConstants;
import com.yugabyte.yw.commissioner.AutoMasterFailoverScheduler;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.tasks.CommissionerBaseTest;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType;
import com.yugabyte.yw.commissioner.tasks.subtasks.CheckClusterConsistency;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.ConfigHelper;
import com.yugabyte.yw.common.CustomerTaskManager;
import com.yugabyte.yw.common.LocalNodeManager;
import com.yugabyte.yw.common.LocalNodeUniverseManager;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.NodeUIApiHelper;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.common.ReleaseManager;
import com.yugabyte.yw.common.RetryTaskUntilCondition;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.UnrecoverableException;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.YcqlQueryExecutor;
import com.yugabyte.yw.common.backuprestore.BackupHelper;
import com.yugabyte.yw.common.certmgmt.CertificateHelper;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.config.RuntimeConfService;
import com.yugabyte.yw.common.config.impl.SettableRuntimeConfigFactory;
import com.yugabyte.yw.common.gflags.GFlagsUtil;
import com.yugabyte.yw.common.gflags.SpecificGFlags;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.common.utils.Pair;
import com.yugabyte.yw.controllers.apiModels.MasterLBStateResponse;
import com.yugabyte.yw.controllers.handlers.MetaMasterHandler;
import com.yugabyte.yw.controllers.handlers.UniverseCRUDHandler;
import com.yugabyte.yw.controllers.handlers.UniverseTableHandler;
import com.yugabyte.yw.controllers.handlers.UpgradeUniverseHandler;
import com.yugabyte.yw.forms.RestartTaskParams;
import com.yugabyte.yw.forms.RunQueryFormData;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseResp;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.forms.UpgradeTaskParams;
import com.yugabyte.yw.models.AccessKey;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.InstanceType;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.ProviderDetails;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.RuntimeConfigEntry;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.YugawareProperty;
import com.yugabyte.yw.models.helpers.CloudInfoInterface;
import com.yugabyte.yw.models.helpers.CommonUtils;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import com.yugabyte.yw.models.helpers.TaskType;
import com.yugabyte.yw.models.helpers.provider.LocalCloudInfo;
import com.yugabyte.yw.scheduler.JobScheduler;
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
import java.time.Duration;
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
import java.util.concurrent.TimeUnit;
import java.util.function.Consumer;
import java.util.function.Function;
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
import org.junit.rules.Timeout;
import org.junit.runner.Description;
import org.yb.CommonTypes.TableType;
import org.yb.client.GetMasterClusterConfigResponse;
import org.yb.client.YBClient;
import org.yb.master.CatalogEntityInfo;
import play.Application;
import play.inject.guice.GuiceApplicationBuilder;
import play.libs.Json;

@Slf4j
public abstract class LocalProviderUniverseTestBase extends CommissionerBaseTest {
  private static final boolean IS_LINUX = System.getProperty("os.name").equalsIgnoreCase("linux");
  private static final Set<String> CONTROL_FILES =
      Set.of(LocalNodeManager.MASTER_EXECUTABLE, LocalNodeManager.TSERVER_EXECUTABLE);
  private static final SimpleDateFormat DATE_FORMAT = new SimpleDateFormat("yyMMdd'T'HHmmss");

  protected static final String INSTANCE_TYPE_CODE = "c3.xlarge";
  protected static final String INSTANCE_TYPE_CODE_2 = "c5.xlarge";

  private static final String YB_PATH_ENV_KEY = "YB_PATH";
  private static final String BASE_DIR_ENV_KEY = "TEST_BASE_DIR";
  private static final String SKIP_WAIT_FOR_CLUSTER_ENV_KEY = "YB_SKIP_WAIT_FOR_CLUSTER";

  private static final String DEFAULT_BASE_DIR = "/tmp/local";
  protected static String YBC_VERSION;
  public static String DB_VERSION = "2024.1.0.0-b129";
  private static final String DOWNLOAD_URL =
      "https://downloads.yugabyte.com/releases/"
          + DB_VERSION.split("-")[0]
          + "/yugabyte-"
          + DB_VERSION
          + "-%s-%s.tar.gz";

  private static final String YBC_BASE_S3_URL = "https://downloads.yugabyte.com/ybc/";
  private static final String YBC_BIN_ENV_KEY = "YBC_PATH";
  private static boolean KEEP_FAILED_UNIVERSE = true;
  private static final boolean KEEP_ALWAYS = false;

  public static Map<String, String> GFLAGS = new HashMap<>();

  private SimpleSqlPayload simpleSqlPayload;

  static {
    GFLAGS.put("load_balancer_max_over_replicated_tablets", "15");
    GFLAGS.put("load_balancer_max_concurrent_adds", "15");
    GFLAGS.put("load_balancer_max_concurrent_removals", "15");
    GFLAGS.put("transaction_table_num_tablets", "3");
    GFLAGS.put(GFlagsUtil.LOAD_BALANCER_INITIAL_DELAY_SECS, "120");
    GFLAGS.put(GFlagsUtil.TMP_DIRECTORY, "");
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
  // Whether to wait until all old tservers are removed from quorum and new ones are added.
  protected static boolean waitForClusterToStabilize;

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
  protected CertificateHelper certificateHelper;
  protected SettableRuntimeConfigFactory settableRuntimeConfigFactory;
  protected RuntimeConfService runtimeConfService;
  protected JobScheduler jobScheduler;
  protected AutoMasterFailoverScheduler autoMasterFailoverScheduler;

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

    waitForClusterToStabilize = System.getenv(SKIP_WAIT_FOR_CLUSTER_ENV_KEY) == null;
    setUpYBSoftware(os, arch);
    ybcBinPath = System.getenv(YBC_BIN_ENV_KEY);
    subDir = DATE_FORMAT.format(new Date());
  }

  @Rule public Timeout globalTimeout = Timeout.seconds(900);

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
      downloadAndSetUpYBSoftware(os, arch, downloadURL, DB_VERSION);
      ybVersion = DB_VERSION;
      ybBinPath = deriveYBBinPath(ybVersion);
    }

    log.debug("YB version {} bin path {}", ybVersion, ybBinPath);
  }

  private void setUpYBCSoftware(String os, String arch) {
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

  public static String deriveYBBinPath(String version) {
    return baseDir + "/yugabyte/yugabyte-" + version + "/bin";
  }

  public static void downloadAndSetUpYBSoftware(
      String os, String arch, String downloadURL, String dbVersion) {
    String baseDownloadPath = baseDir + "/yugabyte";
    File downloadPathFile = new File(baseDownloadPath);

    for (File child : Optional.ofNullable(downloadPathFile.listFiles()).orElse(new File[0])) {
      if (child.getName().startsWith("yugabyte-" + dbVersion) && hasRequiredExecutables(child)) {
        log.debug(dbVersion + " already downloaded! ");
        return;
      }
    }

    try {
      String version = extractVersionFromBuild(dbVersion);
      String filePath = baseDownloadPath + "/yugabyte-" + dbVersion + ".tar.gz";
      downloadFromUrl(filePath, downloadURL);
      String ybReleasePath = baseDownloadPath + "/yugabyte-%s-%s-%s.tar.gz";
      ybReleasePath = String.format(ybReleasePath, dbVersion, os, arch);
      Files.move(Paths.get(filePath), Paths.get(ybReleasePath));
      File oldDbVersionPath = new File(baseDownloadPath + "/yugabyte-" + version);
      File newDBVersionPath = new File(baseDownloadPath + "/yugabyte-" + dbVersion);
      oldDbVersionPath.renameTo(newDBVersionPath);
      String ybBinPath = baseDownloadPath + "/yugabyte-" + dbVersion + "/bin";
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
      log.debug("downloaded from {} to {}", downloadURL, baseDownloadPath);
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

  private static String extractVersionFromBuild(String build) {
    return build.substring(0, build.indexOf("-"));
  }

  private void injectDependencies() {
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
    certificateHelper = app.injector().instanceOf(CertificateHelper.class);
    commissioner = app.injector().instanceOf(Commissioner.class);
    settableRuntimeConfigFactory = app.injector().instanceOf(SettableRuntimeConfigFactory.class);
    runtimeConfService = app.injector().instanceOf(RuntimeConfService.class);
    jobScheduler = app.injector().instanceOf(JobScheduler.class);
    autoMasterFailoverScheduler = app.injector().instanceOf(AutoMasterFailoverScheduler.class);
    customerTaskManager = app.injector().instanceOf(CustomerTaskManager.class);
  }

  @Before
  public void setUp() {
    injectDependencies();

    settableRuntimeConfigFactory.globalRuntimeConf().setValue("yb.releases.use_redesign", "false");
    settableRuntimeConfigFactory
        .globalRuntimeConf()
        .setValue(GlobalConfKeys.startMasterOnRemoveNode.getKey(), "true");
    settableRuntimeConfigFactory
        .globalRuntimeConf()
        .setValue(GlobalConfKeys.startMasterOnStopNode.getKey(), "true");

    settableRuntimeConfigFactory
        .globalRuntimeConf()
        .setValue("yb.universe.consistency_check.enabled", "true");
    Pair<Integer, Integer> ipRange = getIpRange();
    localNodeManager.setIpRangeStart(ipRange.getFirst());
    localNodeManager.setIpRangeEnd(ipRange.getSecond());

    setUpYBCSoftware(os, arch);
    File baseDirFile = new File(baseDir);
    File curDir = new File(baseDirFile, subDir);
    if (!baseDirFile.exists() || !curDir.exists()) {
      curDir.mkdirs();
      if (!KEEP_FAILED_UNIVERSE && !KEEP_ALWAYS) {
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
    ObjectNode ywMetadata = Json.newObject();
    ywMetadata.put("yugaware_uuid", UUID.randomUUID().toString());
    ywMetadata.put("version", ybVersion);
    YugawareProperty.addConfigProperty(
        ConfigHelper.ConfigType.YugawareMetadata.name(), ywMetadata, "Yugaware Metadata");

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

    region = Region.create(provider, "region-1", "region-1", "default-image");
    az1 = AvailabilityZone.createOrThrow(region, "az-1", "az-1", "subnet-1");
    az2 = AvailabilityZone.createOrThrow(region, "az-2", "az-2", "subnet-2");
    az3 = AvailabilityZone.createOrThrow(region, "az-3", "az-3", "subnet-3");
    az4 = AvailabilityZone.createOrThrow(region, "az-4", "az-4", "subnet-4");

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

    RuntimeConfigEntry.upsertGlobal("yb.task.verify_cluster_state", "true");
  }

  /**
   * Range of IPs to use. Two last bytes of each number are used to form 127.0.x.y ip
   *
   * @return
   */
  protected abstract Pair<Integer, Integer> getIpRange();

  protected JsonNode getMetadataJson(String release, boolean isYbc) {
    ReleaseManager.ReleaseMetadata releaseMetadata = new ReleaseManager.ReleaseMetadata();
    releaseMetadata.state = ReleaseManager.ReleaseState.ACTIVE;
    String parentDirectory = "/";
    if (!isYbc) {
      parentDirectory = "/yugabyte/";
    }
    releaseMetadata.filePath =
        baseDir + parentDirectory + "yugabyte-" + release + "-" + os + "-" + arch + ".tar.gz";
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
    log.info("tear down " + testName + " failed " + failed);
    if (simpleSqlPayload != null) {
      simpleSqlPayload.stop();
    }
    if ((!failed || !KEEP_FAILED_UNIVERSE) && !KEEP_ALWAYS) {
      try {
        FileUtils.deleteDirectory(new File(new File(new File(baseDir), subDir), testName));
      } catch (Exception ignored) {
      }
      localNodeManager.shutdown();
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
    return getDefaultUserIntent(univName, disableTls, 3, 3);
  }

  protected UniverseDefinitionTaskParams.UserIntent getDefaultUserIntent(
      String univName, boolean disableTls, int rf, int numNodes) {
    UniverseDefinitionTaskParams.UserIntent userIntent =
        ApiUtils.getTestUserIntent(region, provider, instanceType, numNodes);
    userIntent.replicationFactor = rf;
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
    TaskInfo taskInfo =
        waitForTask(universeResp.taskUUID, Universe.getOrBadRequest(universeResp.universeUUID));
    verifyUniverseTaskSuccess(taskInfo);
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
    initYSQL(universe, "some_table");
  }

  protected void initYSQL(Universe universe, String tableName) {
    initYSQL(
        universe,
        tableName,
        universe.getUniverseDetails().getPrimaryCluster().userIntent.isYSQLAuthEnabled());
  }

  protected void initYSQL(Universe universe, String tableName, boolean authEnabled) {
    NodeDetails nodeDetails =
        universe.getUniverseDetails().nodeDetailsSet.stream()
            .filter(n -> n.state.equals(NodeDetails.NodeState.Live))
            .findFirst()
            .orElse(null);
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
    boolean authEnabled =
        universe.getUniverseDetails().getPrimaryCluster().userIntent.isYSQLAuthEnabled();
    verifyYSQL(universe, readFromRR, dbName, tableName, authEnabled);
  }

  protected void verifyYSQL(
      Universe universe, boolean readFromRR, String dbName, String tableName, boolean authEnabled) {
    verifyYSQL(
        universe,
        readFromRR,
        dbName,
        tableName,
        authEnabled,
        universe.getUniverseDetails().getPrimaryCluster().userIntent.enableConnectionPooling);
  }

  protected void verifyYSQL(
      Universe universe,
      boolean readFromRR,
      String dbName,
      String tableName,
      boolean authEnabled,
      boolean cpEnabled) {
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
            .filter(n -> n.isInPlacement(cluserUUID) && n.state.equals(NodeDetails.NodeState.Live))
            .findFirst()
            .get();
    ShellResponse response =
        localNodeUniverseManager.runYsqlCommand(
            nodeDetails,
            universe,
            dbName,
            String.format("select count(*) from %s", tableName),
            10,
            authEnabled,
            cpEnabled);
    assertTrue(response.isSuccess());
    assertEquals("3", CommonUtils.extractJsonisedSqlResponse(response).trim());
    // Check universe sequence and DB sequence number
  }

  protected void initYCQL(Universe universe) {
    initYCQL(universe, false, "");
  }

  protected void initYCQL(Universe universe, boolean authEnabled, String password) {
    RunQueryFormData formData = new RunQueryFormData();
    // Create `yugabyte` keyspace.
    formData.setQuery("CREATE KEYSPACE IF NOT EXISTS yugabyte;");
    formData.setTableType(TableType.YQL_TABLE_TYPE);
    JsonNode response =
        ycqlQueryExecutor.executeQuery(
            universe, formData, true, Util.DEFAULT_YCQL_USERNAME, password);
    assertFalse(response.has("error"));

    // Create table.
    formData.setQuery(
        "CREATE TABLE yugabyte.some_table (id int, name text, age int, PRIMARY KEY((id, name)));");
    response =
        ycqlQueryExecutor.executeQuery(
            universe, formData, true, Util.DEFAULT_YCQL_USERNAME, password);
    assertFalse(response.has("error"));

    // Insert Data.
    formData.setQuery("INSERT INTO yugabyte.some_table (id, name, age) VALUES (1, 'John', 20);");
    response =
        ycqlQueryExecutor.executeQuery(
            universe, formData, true, Util.DEFAULT_YCQL_USERNAME, password);
    assertFalse(response.has("error"));

    formData.setQuery("INSERT INTO yugabyte.some_table (id, name, age) VALUES (2, 'Mary', 18);");
    response =
        ycqlQueryExecutor.executeQuery(
            universe, formData, true, Util.DEFAULT_YCQL_USERNAME, password);
    assertFalse(response.has("error"));

    formData.setQuery(
        "INSERT INTO yugabyte.some_table (id, name, age) VALUES (10000, 'Stephen', 50);");
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
    formData.setQuery("select count(*) from yugabyte.some_table;");
    formData.setTableType(TableType.YQL_TABLE_TYPE);

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
    verifyUniverseTaskSuccess(taskInfo);
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
                customer, Universe.getOrBadRequest(universe.getUniverseUUID()), taskParams),
            Universe.getOrBadRequest(universe.getUniverseUUID()));
    return taskInfo;
  }

  protected TaskInfo destroyUniverse(Universe universe, Customer customer)
      throws InterruptedException {
    UUID taskID = universeCRUDHandler.destroy(customer, universe, true, false, false);
    TaskInfo taskInfo = waitForTask(taskID, universe);
    return taskInfo;
  }

  protected void verifyUniverseState(Universe universe) {
    log.info("universe definition json {}", universe.getUniverseDetailsJson());
    String certificate = universe.getCertificateNodetoNode();
    try (YBClient client = ybClientService.getClient(universe.getMasterAddresses(), certificate)) {
      GetMasterClusterConfigResponse masterClusterConfig = client.getMasterClusterConfig();
      CatalogEntityInfo.SysClusterConfigEntryPB config = masterClusterConfig.getConfig();
      UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
      UniverseDefinitionTaskParams.Cluster primaryCluster = universeDetails.getPrimaryCluster();
      CatalogEntityInfo.ReplicationInfoPB replicationInfo = config.getReplicationInfo();
      CatalogEntityInfo.PlacementInfoPB liveReplicas = replicationInfo.getLiveReplicas();
      verifyCluster(universe, primaryCluster, liveReplicas);
      verifyMasterAddresses(universe);
      if (!universeDetails.getReadOnlyClusters().isEmpty()) {
        UniverseDefinitionTaskParams.Cluster asyncCluster =
            universeDetails.getReadOnlyClusters().get(0);
        CatalogEntityInfo.PlacementInfoPB readReplicas = replicationInfo.getReadReplicas(0);
        verifyCluster(universe, asyncCluster, readReplicas);
      }
      if (waitForClusterToStabilize) {
        RetryTaskUntilCondition condition =
            new RetryTaskUntilCondition<>(
                () -> {
                  try {
                    return CheckClusterConsistency.checkCurrentServers(
                            client, universe, null, true, false)
                        .getErrors();
                  } catch (Exception e) {
                    return Collections.singletonList("Got error: " + e.getMessage());
                  }
                },
                List::isEmpty);
        condition.retryUntilCond(10, TimeUnit.MINUTES.toSeconds(2));
      }
    } catch (Exception e) {
      throw new RuntimeException(e);
    }
  }

  protected void verifyCluster(
      Universe universe,
      UniverseDefinitionTaskParams.Cluster cluster,
      CatalogEntityInfo.PlacementInfoPB replicas) {
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

    // verify node details matches cluster user intent
    verifyNodeDetails(universe, cluster);
  }

  protected void verifyMasterAddresses(Universe universe) {
    List<String> masterAddresses = new ArrayList<>();
    for (NodeDetails node : universe.getNodes()) {
      if (node.state.equals(NodeDetails.NodeState.Live) && node.isMaster) {
        masterAddresses.add(node.cloudInfo.private_ip + ":7100");
      }
    }

    for (NodeDetails node : universe.getNodes()) {
      if (node.state.equals(NodeDetails.NodeState.Live)) {
        Map<String, String> varz = getVarz(node, universe, UniverseTaskBase.ServerType.TSERVER);
        String masterAddress = varz.getOrDefault("tserver_master_addrs", "");
        String[] gFlagMasterAddress = masterAddress.split(",");
        assertEquals(gFlagMasterAddress.length, masterAddresses.size());
      }
    }
  }

  protected Map<String, String> getVarz(
      NodeDetails nodeDetails, Universe universe, UniverseTaskBase.ServerType serverType) {
    UniverseTaskParams.CommunicationPorts ports = universe.getUniverseDetails().communicationPorts;
    int port =
        serverType == UniverseTaskBase.ServerType.MASTER
            ? ports.masterHttpPort
            : ports.tserverHttpPort;
    JsonNode varz =
        nodeUIApiHelper.getRequest(
            "http://" + nodeDetails.cloudInfo.private_ip + ":" + port + "/api/v1/varz");
    Map<String, String> result = new HashMap<>();
    for (JsonNode flag : varz.get("flags")) {
      result.put(flag.get("name").asText(), flag.get("value").asText());
    }
    return result;
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
    TaskInfo taskInfo = waitForTask(taskUUID, universe);
    verifyUniverseTaskSuccess(taskInfo);
  }

  protected void verifyUniverseTaskSuccess(TaskInfo taskInfo) {
    Universe universe =
        Universe.getOrBadRequest(
            UUID.fromString(taskInfo.getTaskParams().get("universeUUID").textValue()));
    String separator = System.getProperty("line.separator");
    StringBuilder errorBuilder = new StringBuilder();
    if (taskInfo.getTaskState() != TaskInfo.State.Success) {
      errorBuilder.append("Base task error: " + taskInfo.getErrorMessage());
      List<String> failedTasksMessages = new ArrayList<>();
      for (TaskInfo subTask : taskInfo.getSubTasks()) {
        if (subTask.getTaskState() == TaskInfo.State.Failure) {
          if (subTask.getTaskType() == TaskType.WaitForServer
              || subTask.getTaskType() == TaskType.WaitForServerReady) {
            String nodeName = subTask.getTaskParams().get("nodeName").textValue();
            UniverseTaskBase.ServerType serverType =
                UniverseTaskBase.ServerType.valueOf(
                    subTask.getTaskParams().get("serverType").asText());
            localNodeManager.dumpProcessOutput(universe, nodeName, serverType);
          } else {
            failedTasksMessages.add(
                CommissionerBaseTest.getBriefTaskInfo(subTask) + ":" + subTask.getErrorMessage());
          }
        }
      }
      failedTasksMessages.forEach(t -> errorBuilder.append(separator).append(t));
    }
    if (taskInfo.getTaskState() != TaskInfo.State.Success) {
      try {
        log.debug("Dumping to log");
        dumpToLog(universe);
      } catch (InterruptedException e) {
      }
    }
    assertEquals(errorBuilder.toString(), TaskInfo.State.Success, taskInfo.getTaskState());
  }

  protected String getMasterLeader(Universe universe) {
    try (YBClient client =
        ybClientService.getClient(
            universe.getMasterAddresses(), universe.getCertificateNodetoNode())) {
      HostAndPort leaderMasterHostAndPort = client.getLeaderMasterHostAndPort();
      return leaderMasterHostAndPort.getHost();
    } catch (Exception e) {
      throw new RuntimeException(e);
    }
  }

  protected void initAndStartPayload(Universe universe) {
    simpleSqlPayload = new SimpleSqlPayload(5, 2, 200, universe);
    simpleSqlPayload.init();
    simpleSqlPayload.start();
  }

  protected void verifyPayload() {
    simpleSqlPayload.stop();
    assertThat("Low percent errors", simpleSqlPayload.getErrorPercent(), lessThan(0.3d));
  }

  protected SpecificGFlags getGFlags(String... additional) {
    Map<String, String> gflags = new HashMap<>(GFLAGS);
    for (int i = 0; i < additional.length / 2; i++) {
      gflags.put(additional[i], additional[i + 1]);
    }
    return SpecificGFlags.construct(gflags, gflags);
  }

  public static String getAllErrorsStr(TaskInfo taskInfo) {
    StringBuilder sb = new StringBuilder(taskInfo.getErrorMessage());
    for (TaskInfo subTask : taskInfo.getSubTasks()) {
      if (!StringUtils.isEmpty(subTask.getErrorMessage())) {
        sb.append("\n").append(subTask.getErrorMessage());
      }
    }
    return sb.toString();
  }

  protected TaskInfo waitForTask(UUID taskUUID, Universe... universes) throws InterruptedException {
    try {
      return CommissionerBaseTest.waitForTask(taskUUID);
    } catch (Exception e) {
      dumpToLog(universes);
      throw new RuntimeException(e);
    }
  }

  private void dumpToLog(Universe... universes) throws InterruptedException {
    localNodeManager.checkAllProcessesAlive();
    for (Universe universe : universes) {
      Universe u = Universe.getOrBadRequest(universe.getUniverseUUID());
      for (NodeDetails node : u.getNodes()) {
        for (UniverseTaskBase.ServerType serverType : node.getAllProcesses()) {
          localNodeManager.dumpProcessOutput(u, node.getNodeName(), serverType);
        }
      }
    }
    Thread.sleep(1000);
  }

  protected String getBackupBaseDirectory() {
    return String.format("%s/%s/%s", baseDir, subDir, testName);
  }

  protected void killProcessesOnNode(UUID universeUuid, String nodeName)
      throws IOException, InterruptedException {
    Universe universe = Universe.getOrBadRequest(universeUuid);
    NodeDetails node = universe.getNode(nodeName);
    if (node.isTserver) {
      localNodeManager.killProcess(nodeName, ServerType.TSERVER);
    }
    if (node.isMaster) {
      localNodeManager.killProcess(nodeName, ServerType.MASTER);
    }
  }

  protected void startProcessesOnNode(UUID universeUuid, String nodeName)
      throws IOException, InterruptedException {
    Universe universe = Universe.getOrBadRequest(universeUuid);
    NodeDetails node = universe.getNode(nodeName);
    if (node.isTserver) {
      localNodeManager.startProcess(universeUuid, nodeName, ServerType.TSERVER);
    }
    if (node.isMaster) {
      localNodeManager.startProcess(universeUuid, nodeName, ServerType.MASTER);
    }
  }

  protected void killProcessOnNode(UUID universeUuid, String nodeName, ServerType serverType)
      throws IOException, InterruptedException {
    Universe universe = Universe.getOrBadRequest(universeUuid);
    NodeDetails node = universe.getNode(nodeName);
    if (serverType == ServerType.TSERVER) {
      if (!node.isTserver) {
        throw new IllegalArgumentException("Server type " + serverType + " is not running");
      }
    } else if (serverType == ServerType.MASTER) {
      if (!node.isMaster) {
        throw new IllegalArgumentException("Server type " + serverType + " is not running");
      }
    } else {
      throw new IllegalArgumentException("Server type " + serverType + " is not supported");
    }
    localNodeManager.killProcess(nodeName, serverType);
  }

  protected void startProcessesOnNode(
      UUID universeUuid, NodeDetails node, UniverseTaskBase.ServerType serverType)
      throws IOException, InterruptedException {
    localNodeManager.startProcess(universeUuid, node.getNodeName(), serverType);
  }

  protected boolean isMasterProcessRunning(String nodeName) {
    return localNodeManager.isProcessRunning(nodeName, ServerType.MASTER);
  }

  protected void waitTillNumOfTservers(YBClient ybClient, int expected) {
    RetryTaskUntilCondition<Integer> condition =
        new RetryTaskUntilCondition<>(
            () -> getNumberOfTservers(ybClient), (num) -> num == expected);
    boolean success = condition.retryUntilCond(500, TimeUnit.SECONDS.toMillis(60));
    if (!success) {
      throw new RuntimeException("Failed to wait till expected number of tservers");
    }
  }

  protected Integer getNumberOfTservers(YBClient ybClient) {
    try {
      return ybClient.listTabletServers().getTabletServersCount();
    } catch (Exception e) {
      return 0;
    }
  }

  // This method waits for the next task to complete.
  protected TaskInfo waitForNextTask(UUID universeUuid, UUID lastTaskUuid, Duration timeout)
      throws InterruptedException {
    Stopwatch stopwatch = Stopwatch.createStarted();
    do {
      Universe universe = Universe.getOrBadRequest(universeUuid);
      UniverseDefinitionTaskParams details = universe.getUniverseDetails();
      if (details.placementModificationTaskUuid != null
          && !lastTaskUuid.equals(details.placementModificationTaskUuid)) {
        // A new task has already started, wait for it to complete.
        TaskInfo taskInfo = TaskInfo.getOrBadRequest(details.placementModificationTaskUuid);
        return CommissionerBaseTest.waitForTask(taskInfo.getUuid());
      }
      CustomerTask customerTask = CustomerTask.getLastTaskByTargetUuid(universeUuid);
      if (!lastTaskUuid.equals(customerTask.getTaskUUID())) {
        // Last task has already completed.
        return TaskInfo.getOrBadRequest(customerTask.getTaskUUID());
      }
      Thread.sleep(1000);
    } while (stopwatch.elapsed().compareTo(timeout) < 0);
    throw new RuntimeException("Timed-out waiting for next task to start");
  }

  protected void verifyNodeModifications(Universe universe, int added, int removed) {
    assertEquals(
        added,
        universe.getUniverseDetails().nodeDetailsSet.stream()
            .filter(n -> n.state == NodeDetails.NodeState.ToBeAdded)
            .count());
    assertEquals(
        removed,
        universe.getUniverseDetails().nodeDetailsSet.stream()
            .filter(n -> n.state == NodeDetails.NodeState.ToBeRemoved)
            .count());
  }

  protected Pair<Long, Long> verifyNodeDetails(Universe universe, Cluster cluster) {
    // (numMasters, numTservers)
    Pair<Long, Long> nodeCounts =
        new Pair(
            universe.getNodes().stream()
                .filter(n -> n.placementUuid.equals(cluster.uuid) && n.isMaster)
                .count(),
            universe.getNodes().stream()
                .filter(n -> n.placementUuid.equals(cluster.uuid) && n.isTserver)
                .count());
    boolean allNodesLive = universe.getNodes().stream().allMatch(n -> n.state == NodeState.Live);
    log.info(
        "Verifying node details for cluster {}, numMasters {}, numTservers {}, allNodesLive {}",
        cluster.uuid,
        nodeCounts.getFirst(),
        nodeCounts.getSecond(),
        allNodesLive);

    if (allNodesLive) {
      assertEquals(nodeCounts.getSecond().intValue(), cluster.userIntent.numNodes);

      if (cluster.clusterType == UniverseDefinitionTaskParams.ClusterType.PRIMARY
          && cluster.userIntent.dedicatedNodes) {
        assertEquals(nodeCounts.getFirst().intValue(), cluster.userIntent.replicationFactor);
      }
    }
    return nodeCounts;
  }

  protected void verifyMasterLBStatus(
      Customer customer, Universe universe, boolean isEnabled, boolean isLoadBalancerIdle) {
    MetaMasterHandler metaMasterHandler = app.injector().instanceOf(MetaMasterHandler.class);
    MasterLBStateResponse resp =
        metaMasterHandler.getMasterLBState(customer.getUuid(), universe.getUniverseUUID());
    assertEquals(resp.isEnabled, isEnabled);
    assertEquals(resp.isIdle, isLoadBalancerIdle);
  }

  public void doWithRetry(
      Function<Duration, Duration> waitBeforeRetryFunct, Duration timeout, Runnable funct)
      throws RuntimeException {
    long currentDelayMs = 0;
    long timeoutMs = timeout.toMillis();
    long startTime = System.currentTimeMillis();
    while (true) {
      currentDelayMs = waitBeforeRetryFunct.apply(Duration.ofMillis(currentDelayMs)).toMillis();
      try {
        funct.run();
        return;
      } catch (UnrecoverableException e) {
        log.error(
            "Won't retry; Unrecoverable error while running the function: {}", e.getMessage());
        throw e;
      } catch (Exception e) {
        if (System.currentTimeMillis() < startTime + timeoutMs - currentDelayMs) {
          log.warn("Will retry; Error while running the function: {}", e.getMessage());
        } else {
          log.error("Retry timed out; Error while running the function: {}", e.getMessage());
          Throwables.propagate(e);
        }
      }
      log.debug(
          "Waiting for {} ms between retry, total delay remaining {} ms",
          currentDelayMs,
          timeoutMs - (System.currentTimeMillis() - startTime));
      try {
        // Busy waiting is okay here since this is being used in tests.
        Thread.sleep(currentDelayMs);
      } catch (InterruptedException e) {
        log.error("Interrupted while waiting for retry", e);
        throw new RuntimeException(e);
      }
    }
  }

  protected void doWithRetry(Duration waitBeforeRetry, Duration timeout, Runnable funct) {
    doWithRetry((prevDelay) -> waitBeforeRetry, timeout, funct);
  }
}
