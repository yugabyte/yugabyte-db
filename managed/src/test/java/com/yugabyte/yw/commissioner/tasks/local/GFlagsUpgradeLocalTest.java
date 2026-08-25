// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.local;

import static com.yugabyte.yw.common.Util.YUGABYTE_DB;
import static com.yugabyte.yw.forms.UniverseConfigureTaskParams.ClusterOperationType.CREATE;
import static org.hamcrest.MatcherAssert.assertThat;
import static org.hamcrest.Matchers.containsString;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;

import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.commissioner.tasks.CommissionerBaseTest;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleConfigureServers;
import com.yugabyte.yw.commissioner.tasks.subtasks.CreateTableSpaces;
import com.yugabyte.yw.common.LocalNodeManager;
import com.yugabyte.yw.common.NodeManager;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.common.ReleaseManager;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.TableSpaceStructures;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.common.gflags.GFlagGroup;
import com.yugabyte.yw.common.gflags.GFlagsUtil;
import com.yugabyte.yw.common.gflags.SpecificGFlags;
import com.yugabyte.yw.controllers.UniverseControllerRequestBinder;
import com.yugabyte.yw.forms.GFlagsUpgradeParams;
import com.yugabyte.yw.forms.ResizeNodeParams;
import com.yugabyte.yw.forms.RollMaxBatchSize;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseResp;
import com.yugabyte.yw.forms.UpgradeTaskParams;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.ProviderDetails;
import com.yugabyte.yw.models.ScopedRuntimeConfig;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.YugawareProperty;
import com.yugabyte.yw.models.helpers.CloudInfoInterface;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.TaskType;
import com.yugabyte.yw.models.helpers.provider.LocalCloudInfo;
import java.io.IOException;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.function.Consumer;
import java.util.regex.Pattern;
import java.util.stream.Collectors;
import lombok.extern.slf4j.Slf4j;
import org.junit.Test;

@Slf4j
public class GFlagsUpgradeLocalTest extends LocalProviderUniverseTestBase {

  private static final String CLI_VALIDATION_DB_VERSION = "2025.2.3.0-b149";

  private static final String CLI_VALIDATION_DB_VERSION_URL =
      "https://s3.us-west-2.amazonaws.com/uploads.dev.yugabyte.com/"
          + "local-provider-test/2025.2.3.0-b149/yugabyte-2025.2.3.0-b149-%s-%s.tar.gz";

  private static final String BATCH_RPC_VALIDATION_DB_VERSION = "2.31.0.0-b54";

  private static final String BATCH_RPC_VALIDATION_DB_VERSION_URL =
      "https://s3.us-west-2.amazonaws.com/uploads.dev.yugabyte.com/"
          + "local-provider-test/2.31.0.0-b54/yugabyte-2.31.0.0-b54-%s-%s.tar.gz";

  private static final String INVALID_PG_CONF_CSV_MALFORMED = "a,,,,b=1";
  private static final String INVALID_PG_CONF_CSV_GUC = "enable_seqscan=onn";
  private static final String VALID_PG_CONF_CSV_GUC = "enable_seqscan=on";

  private static final String YSQL_HBA_CONF_CSV_FLAG = "ysql_hba_conf_csv";
  private static final String YSQL_PG_CONF_CSV_FLAG = "ysql_pg_conf_csv";
  private static final String YSQL_IDENT_CONF_CSV_FLAG = "ysql_ident_conf_csv";

  private static final String INVALID_VMODULE = "invalid_vmodule_setting_for_test";
  private static final String MALFORMED_CSV_ERROR_STRING = "Malformed CSV";

  private static final List<String> MASTER_INVALID_GFLAGS_CAUGHT =
      List.of("vector_index_backend", "limit_auto_flag_promote_for_new_universe");
  private static final List<String> TSERVER_INVALID_GFLAGS_CAUGHT_BOTH_PATHS =
      List.of("rpc_throttle_threshold_bytes", "vmodule");
  // CLI uses old DB version, so these flag validations not caught.
  private static final List<String> TSERVER_INVALID_GFLAGS_CAUGHT_RPC_ONLY =
      List.of("enable_object_locking_for_table_locks", "ysql_yb_ddl_transaction_block_enabled");

  // ConfValidationCase structure:
  // (hba_content, guc_content, ident_content, hba_error_expected, guc_error_expected,
  // ident_error_expected)
  // Empty content means "skip this config type". Empty error means "expect success".
  private record ConfValidationCase(
      String hba, String guc, String ident, String hbaErr, String gucErr, String identErr) {
    boolean expectSuccess() {
      return hbaErr.isEmpty() && gucErr.isEmpty() && identErr.isEmpty();
    }
  }

  private static final List<ConfValidationCase> CONF_VALIDATION_CASES =
      List.of(
          new ConfValidationCase("", "enable_seqscan=on", "", "", "", ""),
          new ConfValidationCase(
              "", "enable_seqscan=onn", "", "", "enable_seqscan.*requires a boolean value", ""),
          new ConfValidationCase(
              "", "work_mem='not_a_number'", "", "", "invalid value.*work_mem", ""),
          new ConfValidationCase("", "= bad_syntax", "", "", "syntax error", ""),
          new ConfValidationCase("", "nonexistent_param=42", "", "", "nonexistent_param", ""),
          new ConfValidationCase(
              "",
              "log_min_messages='foo'",
              "",
              "",
              "invalid value.*log_min_messages.*"
                  + "Available values:.* "
                  + "info, notice, warning, error, log, fatal, panic",
              ""),
          new ConfValidationCase("host all all 0.0.0.0/0 trust", "", "", "", "", ""),
          new ConfValidationCase(
              "host all all 0.0.0.0/0 bogus_method",
              "",
              "",
              "invalid authentication method \"bogus_method\"",
              "",
              ""),
          new ConfValidationCase(
              "host all all 999.999.999.999/32 trust",
              "",
              "",
              "specifying both host name and CIDR mask is invalid",
              "",
              ""),
          new ConfValidationCase(
              "host all all 0.0.0.0/0", "", "", "end-of-line before authentication method", "", ""),
          new ConfValidationCase("", "", "mymap system_user pg_user", "", "", ""),
          new ConfValidationCase("", "", "only_one_field", "", "", "missing entry"),
          new ConfValidationCase(
              "", "", "mymap /[invalid pg_user", "", "", "invalid regular expression"),
          new ConfValidationCase(
              "host all all 0.0.0.0/0 bad_auth",
              "work_mem='not_a_number'",
              "",
              "invalid authentication method",
              "work_mem",
              ""));

  @Test
  public void testGFlagsValidationPrecheckCliBinary() throws InterruptedException {
    setupGFlagsValidationPrecheckReleases();
    Universe universe = createUniverseForGFlagsValidationPrecheck(CLI_VALIDATION_DB_VERSION);
    runInvalidGFlagsPrecheckOnUniverse(
        universe,
        CLI_VALIDATION_DB_VERSION,
        true /* expectUseCLIBinary */,
        false /* expectPgConfCsvGucCaught */,
        true /* expectPgConfCsvMalformedCaught */);
  }

  @Test
  public void testGFlagsValidationPrecheckBatchRpc() throws InterruptedException {
    setupGFlagsValidationPrecheckReleases();
    Universe universe = createUniverseForGFlagsValidationPrecheck(BATCH_RPC_VALIDATION_DB_VERSION);
    runInvalidGFlagsPrecheckOnUniverse(
        universe,
        BATCH_RPC_VALIDATION_DB_VERSION,
        false /* expectUseCLIBinary */,
        true /* expectPgConfCsvGucCaught */,
        // Passing a proper CSV with invalid fields in it. Hence false
        false /* expectPgConfCsvMalformedCaught */);
    runValidGFlagsPrecheckOnUniverse(universe);
    runConfValidationPrecheckOnUniverse(universe);
  }

  // A real released version >= 2024.2.0.0/2.25.0.0, the threshold below which
  // use_memory_defaults_optimized_for_ysql is not defaulted to true for new universes.
  private static final String ELIGIBLE_DB_VERSION = "2024.2.3.0-b116";
  private static final String ELIGIBLE_DB_VERSION_URL =
      "https://software.yugabyte.com/releases/2024.2.3.0/yugabyte-2024.2.3.0-b116-%s-%s.tar.gz";

  @Test
  public void testNonRestartAndNonRollingUpgrade() throws InterruptedException {
    UniverseDefinitionTaskParams.UserIntent userIntent = getDefaultUserIntent();
    userIntent.specificGFlags = new SpecificGFlags();
    Universe universe = createUniverse(userIntent);
    initYSQL(universe);
    initAndStartPayload(universe);
    SpecificGFlags specificGFlags =
        SpecificGFlags.construct(
            Collections.singletonMap("max_log_size", "1805"),
            Collections.singletonMap("log_max_seconds_to_retain", "86333"));
    universe.getUniverseDetails().getPrimaryCluster().userIntent.specificGFlags = specificGFlags;
    TaskInfo taskInfo =
        doGflagsUpgrade(
            universe, UpgradeTaskParams.UpgradeOption.NON_RESTART_UPGRADE, specificGFlags, null);
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    compareGFlags(universe);
    verifyYSQL(universe);
    verifyPayload();
    specificGFlags =
        SpecificGFlags.construct(
            Collections.singletonMap("max_log_size", "1806"),
            Collections.singletonMap("log_max_seconds_to_retain", "86313"));
    universe.getUniverseDetails().getPrimaryCluster().userIntent.specificGFlags = specificGFlags;
    taskInfo =
        doGflagsUpgrade(
            universe, UpgradeTaskParams.UpgradeOption.NON_ROLLING_UPGRADE, specificGFlags, null);
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    compareGFlags(universe);
    verifyYSQL(universe);
  }

  // PLAT-21736: Master and TServer must agree on use_memory_defaults_optimized_for_ysql, since
  // they use it to negotiate available memory on a node. This gflag is only defaulted to true
  // for new universes on DB versions >= 2024.2.0.0/2.25.0.0, so pin an eligible version here
  // rather than relying on whichever build the local test harness happens to run.
  @Test
  public void testNewUniverseSetsMemoryDefaultsOptimizedForYsql() throws InterruptedException {
    addRelease(ELIGIBLE_DB_VERSION, ELIGIBLE_DB_VERSION_URL);
    localNodeManager.addVersionBinPath(
        ELIGIBLE_DB_VERSION, baseDir + "/yugabyte/yugabyte-" + ELIGIBLE_DB_VERSION + "/bin");

    UniverseDefinitionTaskParams.UserIntent userIntent = getDefaultUserIntent();
    userIntent.ybSoftwareVersion = ELIGIBLE_DB_VERSION;
    Universe universe = createUniverse(userIntent);
    UniverseDefinitionTaskParams.Cluster primaryCluster =
        universe.getUniverseDetails().getPrimaryCluster();
    NodeDetails node = universe.getNodesByCluster(primaryCluster.uuid).get(0);

    assertEquals(
        "true",
        getVarz(node, universe, UniverseTaskBase.ServerType.MASTER)
            .get("use_memory_defaults_optimized_for_ysql"));
    assertEquals(
        "true",
        getVarz(node, universe, UniverseTaskBase.ServerType.TSERVER)
            .get("use_memory_defaults_optimized_for_ysql"));
  }

  @Test
  public void testRollingUpgradeWithRRInherited() throws InterruptedException {
    Universe universe = createUniverse(getDefaultUserIntent());
    doAddReadReplica(universe, getDefaultUserIntent());
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    initYSQL(universe);
    initAndStartPayload(universe);
    SpecificGFlags specificGFlags =
        SpecificGFlags.construct(
            Collections.singletonMap("max_log_size", "1805"),
            Collections.singletonMap("log_max_seconds_to_retain", "86333"));
    SpecificGFlags.PerProcessFlags perProcessFlags = new SpecificGFlags.PerProcessFlags();
    perProcessFlags.value =
        Collections.singletonMap(
            UniverseTaskBase.ServerType.MASTER, Collections.singletonMap("max_log_size", "2205"));
    specificGFlags.setPerAZ(Collections.singletonMap(az2.getUuid(), perProcessFlags));

    TaskInfo taskInfo =
        doGflagsUpgrade(
            universe,
            UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE,
            specificGFlags,
            SpecificGFlags.constructInherited());
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    compareGFlags(universe);
    verifyYSQL(universe);
    verifyPayload();
  }

  @Test
  public void testResizeWithRR() throws InterruptedException {
    UniverseDefinitionTaskParams.UserIntent userIntent = getDefaultUserIntent();
    userIntent.specificGFlags.setGflagGroups(
        Collections.singletonList(GFlagGroup.GroupName.ENHANCED_POSTGRES_COMPATIBILITY));
    Universe universe = createUniverse(userIntent);
    UniverseDefinitionTaskParams.UserIntent rrIntent = getDefaultUserIntent();
    rrIntent.numNodes = 1;
    rrIntent.replicationFactor = 1;
    rrIntent.specificGFlags =
        SpecificGFlags.construct(
            Collections.singletonMap("max_log_size", "1805"),
            Collections.singletonMap("log_max_seconds_to_retain", "86333"));
    addReadReplica(universe, rrIntent);
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    UniverseDefinitionTaskParams.Cluster rrCluster =
        universe.getUniverseDetails().getReadOnlyClusters().get(0);
    NodeDetails primary =
        universe.getNodesByCluster(universe.getUniverseDetails().getPrimaryCluster().uuid).get(0);
    NodeDetails rrNode = universe.getNodesByCluster(rrCluster.uuid).get(0);

    assertTrue(
        getVarz(primary, universe, UniverseTaskBase.ServerType.TSERVER)
            .containsKey("yb_enable_read_committed_isolation"));
    assertTrue(
        getVarz(rrNode, universe, UniverseTaskBase.ServerType.TSERVER)
            .containsKey("yb_enable_read_committed_isolation"));

    ResizeNodeParams resizeParams =
        getUpgradeParams(
            universe, UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE, ResizeNodeParams.class);
    resizeParams.clusters = Collections.singletonList(rrCluster);
    rrCluster.userIntent.instanceType = instanceType2.getInstanceTypeCode();
    GFlagsUtil.removeGFlag(
        rrCluster.userIntent, "log_max_seconds_to_retain", UniverseTaskBase.ServerType.TSERVER);

    TaskInfo taskInfo =
        waitForTask(
            upgradeUniverseHandler.resizeNode(
                resizeParams, customer, Universe.getOrBadRequest(universe.getUniverseUUID())),
            universe);
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    Map<String, String> newValues =
        getDiskFlags(rrNode, universe, UniverseTaskBase.ServerType.TSERVER);
    assertTrue(newValues.containsKey("yb_enable_read_committed_isolation"));
    assertFalse(newValues.containsKey("log_max_seconds_to_retain"));
  }

  @Test
  public void testRollingUpgradeWithRR() throws InterruptedException {
    Universe universe = createUniverse(getDefaultUserIntent());
    addReadReplica(universe, getDefaultUserIntent());
    initYSQL(universe);
    initAndStartPayload(universe);
    SpecificGFlags specificGFlags =
        SpecificGFlags.construct(
            Collections.singletonMap("max_log_size", "1805"),
            Collections.singletonMap("log_max_seconds_to_retain", "86333"));
    SpecificGFlags.PerProcessFlags perProcessFlags = new SpecificGFlags.PerProcessFlags();
    perProcessFlags.value =
        Collections.singletonMap(
            UniverseTaskBase.ServerType.MASTER, Collections.singletonMap("max_log_size", "2205"));
    specificGFlags.setPerAZ(Collections.singletonMap(az2.getUuid(), perProcessFlags));

    TaskInfo taskInfo =
        doGflagsUpgrade(
            universe,
            UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE,
            specificGFlags,
            SpecificGFlags.constructInherited());
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    compareGFlags(universe);
    verifyYSQL(universe);
    verifyPayload();
  }

  @Test
  public void testAddingGFlagGroups() throws InterruptedException {
    Universe universe = createUniverse(getDefaultUserIntent());
    initYSQL(universe);
    initAndStartPayload(universe);
    SpecificGFlags specificGFlags = new SpecificGFlags();
    List<GFlagGroup.GroupName> gflagGroups = new ArrayList<>();
    gflagGroups.add(GFlagGroup.GroupName.ENHANCED_POSTGRES_COMPATIBILITY);
    specificGFlags.setGflagGroups(gflagGroups);
    TaskInfo taskInfo =
        doGflagsUpgrade(
            universe, UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE, specificGFlags, null);
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    UniverseDefinitionTaskParams.Cluster primaryCluster =
        universe.getUniverseDetails().getPrimaryCluster();
    NodeDetails primary = universe.getNodesByCluster(primaryCluster.uuid).get(0);

    Map<String, String> newValues =
        getDiskFlags(primary, universe, UniverseTaskBase.ServerType.TSERVER);

    assertTrue(
        getVarz(primary, universe, UniverseTaskBase.ServerType.TSERVER)
            .containsKey("yb_enable_read_committed_isolation"));
    assertTrue(newValues.containsKey("yb_enable_read_committed_isolation"));

    assertTrue(
        getVarz(primary, universe, UniverseTaskBase.ServerType.TSERVER)
            .containsKey("ysql_enable_read_request_caching"));
    assertTrue(newValues.containsKey("ysql_enable_read_request_caching"));
    verifyYSQL(universe);
    verifyPayload();
  }

  @Test
  public void testRemovingGFlagGroups() throws InterruptedException {
    UniverseDefinitionTaskParams.UserIntent userIntent = getDefaultUserIntent();
    SpecificGFlags specificGFlags = new SpecificGFlags();
    List<GFlagGroup.GroupName> gflagGroups = new ArrayList<>();
    gflagGroups.add(GFlagGroup.GroupName.ENHANCED_POSTGRES_COMPATIBILITY);
    specificGFlags.setGflagGroups(gflagGroups);
    userIntent.specificGFlags = specificGFlags;

    Universe universe = createUniverse(userIntent);
    initYSQL(universe);
    initAndStartPayload(universe);

    UniverseDefinitionTaskParams.Cluster primaryCluster =
        universe.getUniverseDetails().getPrimaryCluster();
    NodeDetails primary = universe.getNodesByCluster(primaryCluster.uuid).get(0);

    NodeDetails primaryNode = universe.getNodesByCluster(primaryCluster.uuid).get(0);
    Map<String, String> newValues =
        getDiskFlags(primaryNode, universe, UniverseTaskBase.ServerType.TSERVER);

    assertTrue(
        getVarz(primary, universe, UniverseTaskBase.ServerType.TSERVER)
            .containsKey("yb_enable_read_committed_isolation"));
    assertTrue(newValues.containsKey("yb_enable_read_committed_isolation"));

    assertTrue(
        getVarz(primary, universe, UniverseTaskBase.ServerType.TSERVER)
            .containsKey("ysql_enable_read_request_caching"));
    assertTrue(newValues.containsKey("ysql_enable_read_request_caching"));

    universe = Universe.getOrBadRequest(universe.getUniverseUUID());

    gflagGroups = new ArrayList<>();
    specificGFlags.setGflagGroups(gflagGroups);
    userIntent.specificGFlags = specificGFlags;

    TaskInfo taskInfo =
        doGflagsUpgrade(
            universe, UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE, specificGFlags, null);
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    compareGFlags(universe);
    verifyYSQL(universe);
    verifyPayload();
  }

  @Test
  // Roll a 6 node cluster in batches of 2 per AZ
  public void testStopMultipleNodesInAZ() throws InterruptedException {
    UniverseDefinitionTaskParams.UserIntent userIntent = getDefaultUserIntent();
    userIntent.numNodes = 6;
    UniverseDefinitionTaskParams taskParams = new UniverseDefinitionTaskParams();
    taskParams.nodePrefix = "univConfCreate";
    taskParams.upsertPrimaryCluster(userIntent, null, null);
    PlacementInfoUtil.updateUniverseDefinition(
        taskParams, customer.getId(), taskParams.getPrimaryCluster().uuid, CREATE);

    taskParams.expectedUniverseVersion = -1;
    UniverseResp universeResp = universeCRUDHandler.createUniverse(customer, taskParams);
    TaskInfo taskInfo = waitForTask(universeResp.taskUUID);
    Universe universe = Universe.getOrBadRequest(universeResp.universeUUID);
    verifyUniverseTaskSuccess(taskInfo);

    settableRuntimeConfigFactory
        .globalRuntimeConf()
        .setValue(UniverseConfKeys.upgradeBatchRollEnabled.getKey(), "true");
    settableRuntimeConfigFactory
        .globalRuntimeConf()
        .setValue(UniverseConfKeys.nodesAreSafeToTakeDownCheckTimeout.getKey(), "30s");

    SpecificGFlags specificGFlags =
        SpecificGFlags.construct(
            Collections.singletonMap("max_log_size", "1805"),
            Collections.singletonMap("log_max_seconds_to_retain", "86333"));
    RollMaxBatchSize rollMaxBatchSize = new RollMaxBatchSize();
    rollMaxBatchSize.setPrimaryBatchSize(2);

    taskInfo =
        doGflagsUpgrade(
            universe,
            UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE,
            specificGFlags,
            null,
            TaskInfo.State.Success,
            null,
            params -> params.rollMaxBatchSize = rollMaxBatchSize);
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        taskInfo.getSubTasks().stream().collect(Collectors.groupingBy(TaskInfo::getPosition));
    boolean foundTasks = false;
    for (List<TaskInfo> group : subTasksByPosition.values()) {
      if (group.get(0).getTaskType() == TaskType.AnsibleClusterServerCtl) {
        String process = group.get(0).getTaskParams().get("process").asText();
        if (process.equals("tserver")) {
          // Verifying that there are 2 simultaneous stops/starts for tservers.
          assertEquals(2, group.size());
          foundTasks = true;
        }
      }
    }
    assertTrue(foundTasks);
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    compareGFlags(universe);
  }

  @Test
  public void testRetryPartlyUpdated() throws InterruptedException {
    settableRuntimeConfigFactory
        .globalRuntimeConf()
        .setValue(GlobalConfKeys.verifyGFlagsOnNodeDuringUpgrade.getKey(), "true");
    Universe universe = createUniverse(getDefaultUserIntent());
    initYSQL(universe);
    SpecificGFlags specificGFlags =
        SpecificGFlags.construct(
            Collections.singletonMap("max_log_size", "1805"),
            Collections.singletonMap("log_max_seconds_to_retain", "86333"));
    List<String> upgradedNodes = new ArrayList<>();
    localNodeManager.setFailureInjection(
        pair -> {
          if (pair.getFirst() != NodeManager.NodeCommandType.Configure) {
            return false;
          }
          AnsibleConfigureServers.Params params = (AnsibleConfigureServers.Params) pair.getSecond();
          if (params.type != UpgradeTaskParams.UpgradeTaskType.GFlags) {
            return false;
          }
          String processType = params.getProperty("processType");
          String node = params.nodeName + "_" + processType;
          if (upgradedNodes.size() > 1) {
            log.debug("Already upgraded {}", upgradedNodes);
            return true;
          }
          upgradedNodes.add(node);
          return false;
        });

    TaskInfo taskInfo =
        doGflagsUpgrade(
            universe,
            UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE,
            specificGFlags,
            null,
            TaskInfo.State.Failure,
            "Failure injected",
            null);
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());

    // Now will fail if we try to upgrade already upgraded node again.
    localNodeManager.setFailureInjection(
        pair -> {
          if (pair.getFirst() != NodeManager.NodeCommandType.Configure) {
            return false;
          }
          AnsibleConfigureServers.Params params = (AnsibleConfigureServers.Params) pair.getSecond();
          if (params.type != UpgradeTaskParams.UpgradeTaskType.GFlags) {
            return false;
          }
          String processType = params.getProperty("processType");
          String node = params.nodeName + "_" + processType;
          if (upgradedNodes.contains(node)) {
            log.debug("Should not upgrade {} already upgraded: {}", node, upgradedNodes);
            return true;
          }
          return false;
        });
    log.debug("Doing retry");
    CustomerTask customerTask =
        customerTaskManager.retryCustomerTask(customer.getUuid(), taskInfo.getUuid());
    taskInfo = waitForTask(customerTask.getTaskUUID());
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());

    compareGFlags(universe);
  }

  @Test
  // Roll a 6 node cluster in batches of 2 per AZ
  public void testStopMultipleNodesInAZRuntimeConf() throws InterruptedException {
    UniverseDefinitionTaskParams.UserIntent userIntent = getDefaultUserIntent();
    userIntent.numNodes = 6;
    UniverseDefinitionTaskParams taskParams = new UniverseDefinitionTaskParams();
    taskParams.nodePrefix = "univConfCreate";
    taskParams.upsertPrimaryCluster(userIntent, null, null);
    PlacementInfoUtil.updateUniverseDefinition(
        taskParams, customer.getId(), taskParams.getPrimaryCluster().uuid, CREATE);

    taskParams.expectedUniverseVersion = -1;
    UniverseResp universeResp = universeCRUDHandler.createUniverse(customer, taskParams);
    TaskInfo taskInfo = waitForTask(universeResp.taskUUID);
    Universe universe = Universe.getOrBadRequest(universeResp.universeUUID);
    verifyUniverseTaskSuccess(taskInfo);

    settableRuntimeConfigFactory
        .globalRuntimeConf()
        .setValue(UniverseConfKeys.upgradeBatchRollEnabled.getKey(), "true");
    settableRuntimeConfigFactory
        .globalRuntimeConf()
        .setValue(UniverseConfKeys.nodesAreSafeToTakeDownCheckTimeout.getKey(), "30s");

    SpecificGFlags specificGFlags =
        SpecificGFlags.construct(
            Collections.singletonMap("max_log_size", "1805"),
            Collections.singletonMap("log_max_seconds_to_retain", "86333"));
    settableRuntimeConfigFactory
        .globalRuntimeConf()
        .setValue(UniverseConfKeys.upgradeBatchRollAutoNumber.getKey(), "2");
    taskInfo =
        doGflagsUpgrade(
            universe,
            UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE,
            specificGFlags,
            null,
            TaskInfo.State.Success,
            null,
            null);
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        taskInfo.getSubTasks().stream().collect(Collectors.groupingBy(TaskInfo::getPosition));
    boolean foundTasks = false;
    for (List<TaskInfo> group : subTasksByPosition.values()) {
      if (group.get(0).getTaskType() == TaskType.AnsibleClusterServerCtl) {
        String process = group.get(0).getTaskParams().get("process").asText();
        if (process.equals("tserver")) {
          // Verifying that there are 2 simultaneous stops/starts for tservers.
          assertEquals(2, group.size());
          foundTasks = true;
        }
      }
    }
    assertTrue(foundTasks);
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    compareGFlags(universe);
  }

  @Test
  public void testStopMultipleNodesInAZFallback() throws InterruptedException {
    UniverseDefinitionTaskParams.UserIntent userIntent = getDefaultUserIntent();
    userIntent.numNodes = 6;
    Universe universe = createUniverse(userIntent);

    NodeDetails nodeDetails = universe.getNodes().iterator().next();

    TableSpaceStructures.TableSpaceInfo twoZoneTablespace =
        initTablespace("two_zone_tablespace", az1.getUuid(), 1, az2.getUuid(), 2);

    String createTblSpace = CreateTableSpaces.getTablespaceCreationQuery(twoZoneTablespace);

    ShellResponse response =
        localNodeUniverseManager.runYsqlCommand(
            nodeDetails,
            universe,
            YUGABYTE_DB,
            createTblSpace,
            20,
            userIntent.isYSQLAuthEnabled(),
            false);
    assertTrue("Message is " + response.getMessage(), response.isSuccess());

    response =
        localNodeUniverseManager.runYsqlCommand(
            nodeDetails,
            universe,
            YUGABYTE_DB,
            "CREATE TABLE two_zone_table (id INTEGER, field text)\n"
                + "  TABLESPACE two_zone_tablespace",
            20,
            userIntent.isYSQLAuthEnabled());
    assertTrue(response.isSuccess());

    response =
        localNodeUniverseManager.runYsqlCommand(
            nodeDetails,
            universe,
            YUGABYTE_DB,
            "INSERT INTO two_zone_table \n" + " values (1, 'some_value')",
            20,
            userIntent.isYSQLAuthEnabled());
    assertTrue(response.isSuccess());

    settableRuntimeConfigFactory
        .globalRuntimeConf()
        .setValue(UniverseConfKeys.upgradeBatchRollEnabled.getKey(), "true");
    settableRuntimeConfigFactory
        .globalRuntimeConf()
        .setValue(UniverseConfKeys.nodesAreSafeToTakeDownCheckTimeout.getKey(), "30s");

    SpecificGFlags specificGFlags =
        SpecificGFlags.construct(
            Collections.singletonMap("max_log_size", "1805"),
            Collections.singletonMap("log_max_seconds_to_retain", "86333"));

    UniverseDefinitionTaskParams.Cluster cluster =
        universe.getUniverseDetails().getPrimaryCluster();

    RollMaxBatchSize rollMaxBatchSize = new RollMaxBatchSize();
    // Since we have az-2 with 2 replicas, we cannot stop 2 nodes -> will have fallback.
    rollMaxBatchSize.setPrimaryBatchSize(2);

    TaskInfo taskInfo =
        doGflagsUpgrade(
            universe,
            UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE,
            specificGFlags,
            null,
            TaskInfo.State.Success,
            null,
            params -> params.rollMaxBatchSize = rollMaxBatchSize);
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        taskInfo.getSubTasks().stream().collect(Collectors.groupingBy(TaskInfo::getPosition));

    boolean foundTasks = false;
    for (List<TaskInfo> group : subTasksByPosition.values()) {
      if (group.get(0).getTaskType() == TaskType.AnsibleClusterServerCtl) {
        String process = group.get(0).getTaskParams().get("process").asText();
        if (process.equals("tserver")) {
          // Verifying that there are 1 simultaneous stops/starts for tservers.
          assertEquals(1, group.size());
          foundTasks = true;
        }
      }
    }
    assertTrue(foundTasks);

    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    compareGFlags(universe);
  }

  @Test
  public void testNodesAreSafeToTakeDownFails() throws InterruptedException, IOException {
    // So that we will do only one check.
    settableRuntimeConfigFactory
        .globalRuntimeConf()
        .setValue(UniverseConfKeys.nodesAreSafeToTakeDownCheckTimeout.getKey(), "65s");
    UniverseDefinitionTaskParams.UserIntent userIntent = getDefaultUserIntent();
    userIntent.specificGFlags = getGFlags("TEST_set_tablet_follower_lag_ms", "20000");
    Universe universe = createUniverse(userIntent);
    initYSQL(universe);
    SpecificGFlags specificGFlags =
        SpecificGFlags.construct(
            Collections.singletonMap("max_log_size", "1805"),
            Collections.singletonMap("log_max_seconds_to_retain", "86333"));
    universe.getUniverseDetails().getPrimaryCluster().userIntent.specificGFlags = specificGFlags;
    NodeDetails node = universe.getNodes().iterator().next();
    LocalNodeManager.NodeInfo nodeInfo = localNodeManager.getNodeInfo(node);
    localNodeManager.killProcess(node.getNodeName(), UniverseTaskBase.ServerType.MASTER);
    // Check that it fails (one master is absent)
    doGflagsUpgrade(
        universe,
        UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE,
        specificGFlags,
        null,
        TaskInfo.State.Failure,
        "Service(s) MASTER are not alive on node",
        null);
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    // Verify that it failed before locking
    assertNull(universe.getUniverseDetails().updatingTaskUUID);
    assertNull(universe.getUniverseDetails().placementModificationTaskUuid);
    localNodeManager.startProcessForNode(userIntent, UniverseTaskBase.ServerType.MASTER, nodeInfo);

    // Now kill tserver
    localNodeManager.killProcess(node.getNodeName(), UniverseTaskBase.ServerType.TSERVER);
    // Check that it fails
    doGflagsUpgrade(
        universe,
        UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE,
        specificGFlags,
        null,
        TaskInfo.State.Failure,
        "Service(s) TSERVER are not alive on node",
        null);
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    // Verify that it failed before locking
    assertNull(universe.getUniverseDetails().updatingTaskUUID);
    assertNull(universe.getUniverseDetails().placementModificationTaskUuid);
    localNodeManager.startProcessForNode(userIntent, UniverseTaskBase.ServerType.TSERVER, nodeInfo);

    // Too small max follower lag - might fail
    settableRuntimeConfigFactory
        .globalRuntimeConf()
        .setValue(UniverseConfKeys.followerLagMaxThreshold.getKey(), "10s");
    doGflagsUpgrade(
        universe,
        UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE,
        specificGFlags,
        null,
        TaskInfo.State.Failure,
        "Aborting because this operation can potentially "
            + "take down a majority of copies of some tablets",
        null);
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    // Verify that it failed before locking
    assertNull(universe.getUniverseDetails().updatingTaskUUID);
    assertNull(universe.getUniverseDetails().placementModificationTaskUuid);

    // Revert setting
    settableRuntimeConfigFactory
        .globalRuntimeConf()
        .setValue(UniverseConfKeys.followerLagMaxThreshold.getKey(), "60s");

    // Now it should be successful
    doGflagsUpgrade(
        universe, UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE, specificGFlags, null);
    compareGFlags(universe);

    verifyYSQL(universe);
  }

  @Test
  public void testUpgradeFailsDuringExecution() throws InterruptedException, IOException {
    // So that we will do only one check.
    settableRuntimeConfigFactory
        .globalRuntimeConf()
        .setValue(UniverseConfKeys.nodesAreSafeToTakeDownCheckTimeout.getKey(), "65s");
    UniverseDefinitionTaskParams.UserIntent userIntent = getDefaultUserIntent();
    userIntent.specificGFlags = SpecificGFlags.construct(new HashMap<>(), new HashMap<>());
    Universe universe = createUniverse(userIntent);
    initYSQL(universe);
    // Upgrading only tservers
    SpecificGFlags specificGFlags =
        SpecificGFlags.construct(
            new HashMap<>(), Collections.singletonMap("log_max_seconds_to_retain", "86333"));
    universe.getUniverseDetails().getPrimaryCluster().userIntent.specificGFlags = specificGFlags;

    CommissionerBaseTest.setPausePosition(25);
    GFlagsUpgradeParams gFlagsUpgradeParams =
        getUpgradeParams(
            universe, UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE, GFlagsUpgradeParams.class);
    gFlagsUpgradeParams.getPrimaryCluster().userIntent.specificGFlags = specificGFlags;
    UUID taskID =
        upgradeUniverseHandler.upgradeGFlags(
            gFlagsUpgradeParams, customer, Universe.getOrBadRequest(universe.getUniverseUUID()));
    CommissionerBaseTest.waitForTaskPaused(taskID, commissioner);
    TaskInfo taskInfo = TaskInfo.getOrBadRequest(taskID);
    String processedNodeName =
        taskInfo.getSubTasks().stream()
            .filter(t -> t.getTaskState() == TaskInfo.State.Success)
            .filter(t -> t.getTaskType() == TaskType.SetNodeState)
            .map(t -> t.getTaskParams().get("nodeName").asText())
            .findFirst()
            .get();

    localNodeManager.killProcess(processedNodeName, UniverseTaskBase.ServerType.TSERVER);
    CommissionerBaseTest.clearAbortOrPausePositions();

    commissioner.resumeTask(taskID);
    taskInfo = waitForTask(taskID);
    assertEquals(TaskInfo.State.Failure, taskInfo.getTaskState());
    assertThat(
        getAllErrorsStr(taskInfo),
        containsString("this operation can potentially take down a majority"));
  }

  @Test
  public void testRollingUpgradeTmpWithYSQLAuth() throws InterruptedException {
    UniverseDefinitionTaskParams.UserIntent userIntent = getDefaultUserIntent();
    userIntent.enableYSQL = true;
    userIntent.enableYSQLAuth = true;
    userIntent.ysqlPassword = "qqWER123!!";
    userIntent.specificGFlags =
        SpecificGFlags.construct(
            Collections.singletonMap(GFlagsUtil.TMP_DIRECTORY, "/tmp"),
            Collections.singletonMap(GFlagsUtil.TMP_DIRECTORY, "/tmp"));
    Universe universe = createUniverse(userIntent);
    initYSQL(universe);
    SpecificGFlags specificGFlags =
        SpecificGFlags.construct(
            Collections.singletonMap(GFlagsUtil.TMP_DIRECTORY, "/tmp2"),
            Collections.singletonMap(GFlagsUtil.TMP_DIRECTORY, "/tmp2"));
    universe.getUniverseDetails().getPrimaryCluster().userIntent.specificGFlags = specificGFlags;
    TaskInfo taskInfo =
        doGflagsUpgrade(
            universe, UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE, specificGFlags, null);
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    compareGFlags(universe);
    verifyYSQL(universe);
  }

  protected TaskInfo doGflagsUpgrade(
      Universe universe,
      UpgradeTaskParams.UpgradeOption upgradeOption,
      SpecificGFlags specificGFlags,
      SpecificGFlags rrGflags)
      throws InterruptedException {
    return doGflagsUpgrade(
        universe, upgradeOption, specificGFlags, rrGflags, TaskInfo.State.Success, null, null);
  }

  private void setupGFlagsValidationPrecheckReleases() {
    addRelease(CLI_VALIDATION_DB_VERSION, CLI_VALIDATION_DB_VERSION_URL);
    addRelease(BATCH_RPC_VALIDATION_DB_VERSION, BATCH_RPC_VALIDATION_DB_VERSION_URL);
    localNodeManager.addVersionBinPath(
        CLI_VALIDATION_DB_VERSION, deriveYBBinPath(CLI_VALIDATION_DB_VERSION));
    localNodeManager.addVersionBinPath(
        BATCH_RPC_VALIDATION_DB_VERSION, deriveYBBinPath(BATCH_RPC_VALIDATION_DB_VERSION));
    runtimeConfService.setKey(
        customer.getUuid(),
        ScopedRuntimeConfig.GLOBAL_SCOPE_UUID,
        GlobalConfKeys.skipVersionChecks.getKey(),
        "true",
        true);
  }

  private Universe createUniverseForGFlagsValidationPrecheck(String dbVersion)
      throws InterruptedException {
    updateProviderDetailsForCreateUniverse(dbVersion);
    UniverseDefinitionTaskParams.UserIntent userIntent = getDefaultUserIntent();
    userIntent.ybSoftwareVersion = dbVersion;
    userIntent.enableYSQL = true;
    userIntent.specificGFlags =
        SpecificGFlags.construct(
            buildMasterGFlagsForValidation(false /* includeInvalidGFlags */),
            buildTserverGFlagsForValidation(
                false /* includeInvalidGFlags */, false /* usePgConfCsvGucInvalidValue */));
    return createUniverse(userIntent);
  }

  private void runInvalidGFlagsPrecheckOnUniverse(
      Universe universe,
      String dbVersion,
      boolean expectUseCLIBinary,
      boolean expectPgConfCsvGucCaught,
      boolean expectPgConfCsvMalformedCaught)
      throws InterruptedException {
    TaskInfo taskInfo =
        runGFlagsPrecheck(
            universe,
            buildMasterGFlagsForValidation(true /* includeInvalidGFlags */),
            buildTserverGFlagsForValidation(
                true /* includeInvalidGFlags */, expectPgConfCsvGucCaught),
            TaskInfo.State.Failure,
            expectUseCLIBinary);

    String errors = getAllErrorsStr(taskInfo);
    for (String flagName : MASTER_INVALID_GFLAGS_CAUGHT) {
      assertThat(
          "Expected master validation error for " + flagName + " on version " + dbVersion,
          errors,
          containsString(flagName));
    }
    for (String flagName : TSERVER_INVALID_GFLAGS_CAUGHT_BOTH_PATHS) {
      assertThat(
          "Expected tserver validation error for " + flagName + " on version " + dbVersion,
          errors,
          containsString(flagName));
    }
    if (!expectUseCLIBinary) {
      for (String flagName : TSERVER_INVALID_GFLAGS_CAUGHT_RPC_ONLY) {
        assertThat(
            "Expected tserver validation error for "
                + flagName
                + " via batch RPC on version "
                + dbVersion,
            errors,
            containsString(flagName));
      }
    }
    if (expectPgConfCsvGucCaught) {
      assertThat(
          "Expected ysql_pg_conf_csv GUC validation via batch RPC on version " + dbVersion,
          errors,
          containsString(YSQL_PG_CONF_CSV_FLAG));
    }
    if (expectPgConfCsvMalformedCaught) {
      assertThat(
          "Expected ysql_pg_conf_csv malformed CSV validation on version " + dbVersion,
          errors,
          containsString(MALFORMED_CSV_ERROR_STRING));
    }
  }

  private void runValidGFlagsPrecheckOnUniverse(Universe universe) throws InterruptedException {
    runGFlagsPrecheck(
        universe,
        buildMasterGFlagsForValidation(false /* includeInvalidGFlags */),
        buildTserverConfGFlags("", VALID_PG_CONF_CSV_GUC, ""),
        TaskInfo.State.Success,
        false /* expectUseCLIBinary */);
  }

  private void runConfValidationPrecheckOnUniverse(Universe universe) throws InterruptedException {
    for (ConfValidationCase testCase : CONF_VALIDATION_CASES) {
      TaskInfo taskInfo =
          runGFlagsPrecheck(
              universe,
              buildMasterGFlagsForValidation(false /* includeInvalidGFlags */),
              buildTserverConfGFlags(testCase.hba(), testCase.guc(), testCase.ident()),
              testCase.expectSuccess() ? TaskInfo.State.Success : TaskInfo.State.Failure,
              false /* expectUseCLIBinary */);
      if (!testCase.expectSuccess()) {
        assertConfErrors(getAllErrorsStr(taskInfo), testCase);
      }
    }
  }

  private TaskInfo runGFlagsPrecheck(
      Universe universe,
      Map<String, String> masterGFlags,
      Map<String, String> tserverGFlags,
      TaskInfo.State expectedState,
      boolean expectUseCLIBinary)
      throws InterruptedException {
    TaskInfo taskInfo =
        doGflagsUpgrade(
            universe,
            UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE,
            SpecificGFlags.construct(masterGFlags, tserverGFlags),
            null,
            expectedState,
            expectedState == TaskInfo.State.Success ? null : "GFlags validation failed",
            params -> params.runOnlyPrechecks = true);
    assertValidateGFlags(taskInfo, expectUseCLIBinary);
    return taskInfo;
  }

  private static void assertValidateGFlags(
      TaskInfo gFlagsUpgradeTask, boolean expectedUseCLIBinary) {
    TaskInfo validateGFlags =
        gFlagsUpgradeTask.getSubTasks().stream()
            .filter(t -> t.getTaskType() == TaskType.ValidateGFlags)
            .findFirst()
            .orElseThrow(() -> new AssertionError("ValidateGFlags subtask missing"));
    assertEquals(
        "ValidateGFlags.useCLIBinary",
        expectedUseCLIBinary,
        validateGFlags.getTaskParams().get("useCLIBinary").asBoolean());
  }

  private static Map<String, String> buildMasterGFlagsForValidation(boolean includeInvalidGFlags) {
    Map<String, String> masterGFlags = new HashMap<>();
    masterGFlags.put("transaction_table_num_tablets", "3");
    if (includeInvalidGFlags) {
      masterGFlags.put("vector_index_backend", "invalid_backend");
      masterGFlags.put("limit_auto_flag_promote_for_new_universe", "99");
    }
    return masterGFlags;
  }

  private static Map<String, String> buildTserverGFlagsForValidation(
      boolean includeInvalidGFlags, boolean usePgConfCsvGucInvalidValue) {
    Map<String, String> tserverGFlags = new HashMap<>();
    if (includeInvalidGFlags) {
      tserverGFlags.put("rpc_throttle_threshold_bytes", "11");
      tserverGFlags.put("vmodule", INVALID_VMODULE);
      tserverGFlags.put("enable_object_locking_for_table_locks", "true");
      tserverGFlags.put("ysql_yb_ddl_transaction_block_enabled", "false");
      tserverGFlags.put(
          YSQL_PG_CONF_CSV_FLAG,
          usePgConfCsvGucInvalidValue ? INVALID_PG_CONF_CSV_GUC : INVALID_PG_CONF_CSV_MALFORMED);
    }
    return tserverGFlags;
  }

  private static Map<String, String> buildTserverConfGFlags(
      String hbaContent, String gucContent, String identContent) {
    Map<String, String> tserverGFlags = new HashMap<>();
    if (!hbaContent.isEmpty()) {
      tserverGFlags.put(YSQL_HBA_CONF_CSV_FLAG, hbaContent);
    }
    if (!gucContent.isEmpty()) {
      tserverGFlags.put(YSQL_PG_CONF_CSV_FLAG, gucContent);
    }
    if (!identContent.isEmpty()) {
      tserverGFlags.put(YSQL_IDENT_CONF_CSV_FLAG, identContent);
    }
    assertFalse("At least one conf flag must be set", tserverGFlags.isEmpty());
    return tserverGFlags;
  }

  private static void assertConfErrors(String errors, ConfValidationCase testCase) {
    checkConfErrorInPrecheck(errors, YSQL_HBA_CONF_CSV_FLAG, testCase.hba(), testCase.hbaErr());
    checkConfErrorInPrecheck(errors, YSQL_PG_CONF_CSV_FLAG, testCase.guc(), testCase.gucErr());
    checkConfErrorInPrecheck(
        errors, YSQL_IDENT_CONF_CSV_FLAG, /*testCase.ident()*/ testCase.guc(), testCase.identErr());
  }

  private static void checkConfErrorInPrecheck(
      String errors, String flagName, String content, String expectedError) {
    if (content.isEmpty() || expectedError.isEmpty()) {
      return;
    }
    assertThat(
        "Expected validation error for " + flagName + " on content [" + content + "]",
        errors,
        containsString(flagName));
    assertTrue(
        flagName + " error did not match pattern [" + expectedError + "] in: " + errors,
        Pattern.compile(expectedError, Pattern.CASE_INSENSITIVE | Pattern.DOTALL)
            .matcher(errors)
            .find());
  }

  private void addRelease(String dbVersion, String dbVersionUrl) {
    String downloadURL = String.format(dbVersionUrl, os, arch);
    downloadAndSetUpYBSoftware(os, arch, downloadURL, dbVersion);
    ObjectNode releases =
        (ObjectNode) YugawareProperty.get(ReleaseManager.CONFIG_TYPE.name()).getValue();
    releases.set(dbVersion, getMetadataJson(dbVersion, false).get(dbVersion));
    YugawareProperty.addConfigProperty(ReleaseManager.CONFIG_TYPE.name(), releases, "release");
  }

  private void updateProviderDetailsForCreateUniverse(String dbVersion) {
    String binPath = deriveYBBinPath(dbVersion);
    LocalCloudInfo localCloudInfo = new LocalCloudInfo();
    localCloudInfo.setDataHomeDir(
        ((LocalCloudInfo) CloudInfoInterface.get(provider)).getDataHomeDir());
    localCloudInfo.setYugabyteBinDir(binPath);
    localCloudInfo.setYbcBinDir(ybcBinPath);
    ProviderDetails.CloudInfo cloudInfo = new ProviderDetails.CloudInfo();
    cloudInfo.setLocal(localCloudInfo);
    ProviderDetails providerDetails = new ProviderDetails();
    providerDetails.setCloudInfo(cloudInfo);
    provider.setDetails(providerDetails);
    provider.update();
  }

  protected TaskInfo doGflagsUpgrade(
      Universe universe,
      UpgradeTaskParams.UpgradeOption upgradeOption,
      SpecificGFlags specificGFlags,
      SpecificGFlags rrGflags,
      TaskInfo.State expectedState,
      String expectedFailMessage,
      Consumer<GFlagsUpgradeParams> upgradeParamsCustomizer)
      throws InterruptedException {
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    GFlagsUpgradeParams gFlagsUpgradeParams =
        getUpgradeParams(universe, upgradeOption, GFlagsUpgradeParams.class);
    gFlagsUpgradeParams.getPrimaryCluster().userIntent.specificGFlags = specificGFlags;
    if (rrGflags != null) {
      gFlagsUpgradeParams.getReadOnlyClusters().get(0).userIntent.specificGFlags = rrGflags;
    }
    if (upgradeParamsCustomizer != null) {
      upgradeParamsCustomizer.accept(gFlagsUpgradeParams);
    }
    TaskInfo taskInfo =
        waitForTask(
            upgradeUniverseHandler.upgradeGFlags(
                gFlagsUpgradeParams,
                customer,
                Universe.getOrBadRequest(universe.getUniverseUUID())),
            universe);
    assertEquals(expectedState, taskInfo.getTaskState());
    if (expectedFailMessage != null) {
      assertThat(getAllErrorsStr(taskInfo), containsString(expectedFailMessage));
    }
    return taskInfo;
  }

  private <T extends UpgradeTaskParams> T getUpgradeParams(
      Universe universe, UpgradeTaskParams.UpgradeOption upgradeOption, Class<T> clazz) {
    T upgadeParams = UniverseControllerRequestBinder.deepCopy(universe.getUniverseDetails(), clazz);
    upgadeParams.setUniverseUUID(universe.getUniverseUUID());
    upgadeParams.upgradeOption = upgradeOption;
    upgadeParams.expectedUniverseVersion = universe.getVersion();
    upgadeParams.clusters = universe.getUniverseDetails().clusters;
    upgadeParams.sleepAfterTServerRestartMillis = 10000;
    upgadeParams.sleepAfterMasterRestartMillis = 10000;
    return upgadeParams;
  }

  private void compareGFlags(Universe universe) {
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    UniverseDefinitionTaskParams.UserIntent userIntent =
        universe.getUniverseDetails().getPrimaryCluster().userIntent;
    for (NodeDetails node : universe.getNodes()) {
      UniverseDefinitionTaskParams.Cluster cluster = universe.getCluster(node.placementUuid);
      for (UniverseTaskBase.ServerType serverType : node.getAllProcesses()) {
        Map<String, String> varz = getVarz(node, universe, serverType);
        Map<String, String> gflags =
            GFlagsUtil.getGFlagsForNode(
                node, serverType, cluster, universe.getUniverseDetails().clusters);
        Map<String, String> newInstallGflags = universe.getNewInstallGFlags(serverType);
        gflags.putAll(newInstallGflags);
        log.info(
            "expected gflags for node {} server type {} are {}", node.nodeName, serverType, gflags);
        Map<String, String> gflagsOnDisk = getDiskFlags(node, universe, serverType);
        gflags.forEach(
            (k, v) -> {
              String expectedValue = v;
              if (k.equals(GFlagsUtil.TMP_DIRECTORY)) {
                expectedValue = localNodeManager.getTmpDir(gflags, node.getNodeName(), userIntent);
              }
              String actual = varz.getOrDefault(k, "?????");
              assertEquals("Compare in memory gflag " + k, expectedValue, actual);
              String onDisk = gflagsOnDisk.getOrDefault(k, "?????");
              assertEquals("Compare on disk gflag " + k, expectedValue, onDisk);
            });
      }
    }
  }
}
