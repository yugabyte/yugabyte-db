// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks.check;

import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.inject.Inject;
import com.typesafe.config.Config;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType;
import com.yugabyte.yw.commissioner.tasks.params.ServerSubTaskParams;
import com.yugabyte.yw.commissioner.tasks.subtasks.ServerSubTaskBase;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.ReleaseManager;
import com.yugabyte.yw.common.SoftwareUpgradeHelper;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.audit.AuditService;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.common.gflags.AutoFlagUtil;
import com.yugabyte.yw.common.gflags.GFlagsUtil;
import com.yugabyte.yw.common.gflags.GFlagsValidation;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.Audit;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CommonUtils;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.io.InputStream;
import java.util.Collections;
import java.util.Map;
import java.util.Set;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import java.util.stream.Collectors;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections4.CollectionUtils;
import org.apache.commons.lang3.StringUtils;
import play.mvc.Http.Status;

@Slf4j
public class CheckUpgrade extends ServerSubTaskBase {

  private final Config appConfig;
  private final AuditService auditService;
  private final SoftwareUpgradeHelper softwareUpgradeHelper;

  @Inject
  protected CheckUpgrade(
      BaseTaskDependencies baseTaskDependencies,
      Config appConfig,
      GFlagsValidation gFlagsValidation,
      ReleaseManager releaseManager,
      AutoFlagUtil autoFlagUtil,
      SoftwareUpgradeHelper softwareUpgradeHelper,
      AuditService auditService) {
    super(baseTaskDependencies);
    this.appConfig = appConfig;
    this.softwareUpgradeHelper = softwareUpgradeHelper;
    this.auditService = auditService;
  }

  public static class Params extends ServerSubTaskParams {
    public String ybSoftwareVersion;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public void run() {
    Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
    String oldVersion =
        universe.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion;
    String newVersion = taskParams().ybSoftwareVersion;
    if (CommonUtils.isAutoFlagSupported(oldVersion)) {
      if (!CommonUtils.isAutoFlagSupported(newVersion)) {
        throw new PlatformServiceException(
            BAD_REQUEST, "Cannot upgrade DB to version which does not contains auto flags");
      }
    } else {
      log.info(
          "Skipping auto flags check as it was not enabled earlier on version: {}", oldVersion);
      return;
    }

    if (confGetter.getConfForScope(
        universe, UniverseConfKeys.skipAutoflagsAndYsqlMigrationFilesValidation)) {
      log.info("Skipping auto flags and YSQL migration files validation");
      return;
    }
    // Check if autoflags are compatible with the new version.
    validateAutoflag(universe, newVersion);

    // Check if YSQL migration files are present in the new version.
    validateYsqlMigrationFilesPresent(oldVersion, newVersion);

    // Check if YSQL major version upgrade is allowed.
    validateYSQLMajorUpgrade(universe, oldVersion, newVersion);

    // Update the audit details with upgrade info.
    updateAuditDetails(oldVersion, newVersion);
  }

  private void updateAuditDetails(String oldVersion, String newVersion) {
    Audit audit = auditService.getFromTaskUUID(getUserTaskUUID());
    if (audit == null) {
      log.info("Audit not found for task UUID: {}", getUserTaskUUID());
      return;
    }
    JsonNode auditDetails = audit.getAdditionalDetails();
    ObjectNode modifiedNode;
    if (auditDetails != null) {
      modifiedNode = auditDetails.deepCopy();
    } else {
      ObjectMapper mapper = new ObjectMapper();
      modifiedNode = mapper.createObjectNode();
    }
    modifiedNode.put(
        "finalizeRequired",
        String.valueOf(softwareUpgradeHelper.checkUpgradeRequireFinalize(oldVersion, newVersion)));
    modifiedNode.put(
        "ysqlMajorVersionUpgrade",
        String.valueOf(gFlagsValidation.ysqlMajorVersionUpgrade(oldVersion, newVersion)));

    auditService.updateAdditionalDetails(getUserTaskUUID(), modifiedNode);
  }

  private void validateAutoflag(Universe universe, String newVersion) {
    try {
      // Extract auto flag file from db package if not exists earlier.
      String releasePath = appConfig.getString(Util.YB_RELEASES_PATH);
      if (!gFlagsValidation.checkGFlagFileExists(
          releasePath, newVersion, Util.AUTO_FLAG_FILENAME)) {
        try (InputStream inputStream = releaseManager.getTarGZipDBPackageInputStream(newVersion)) {
          gFlagsValidation.fetchGFlagFilesFromTarGZipInputStream(
              inputStream,
              newVersion,
              Collections.singletonList(Util.AUTO_FLAG_FILENAME),
              releasePath);
        }
      }

      // Make sure there are no new auto flags missing from the modified version,
      // which could occur during a downgrade.
      Set<String> oldMasterAutoFlags =
          autoFlagUtil.getPromotedAutoFlags(universe, ServerType.MASTER, -1);
      if (CollectionUtils.isNotEmpty(oldMasterAutoFlags)) {
        Set<String> newMasterAutoFlags =
            gFlagsValidation.extractAutoFlags(newVersion, "yb-master").autoFlagDetails.stream()
                .map(flag -> flag.name)
                .collect(Collectors.toSet());
        checkAutoFlagsAvailability(oldMasterAutoFlags, newMasterAutoFlags, newVersion);
      }

      Set<String> oldTserverAutoFlags =
          autoFlagUtil.getPromotedAutoFlags(universe, ServerType.TSERVER, -1);
      if (!CollectionUtils.isNotEmpty(oldTserverAutoFlags)) {
        Set<String> newTserverAutoFlags =
            gFlagsValidation.extractAutoFlags(newVersion, "yb-tserver").autoFlagDetails.stream()
                .map(flag -> flag.name)
                .collect(Collectors.toSet());
        checkAutoFlagsAvailability(oldTserverAutoFlags, newTserverAutoFlags, newVersion);
      }
    } catch (PlatformServiceException e) {
      throw e;
    } catch (Exception e) {
      log.error("Error occurred while performing upgrade check: ", e);
      throw new PlatformServiceException(INTERNAL_SERVER_ERROR, e.getMessage());
    }
  }

  private void checkAutoFlagsAvailability(
      Set<String> oldFlags, Set<String> newFlags, String version) {
    for (String oldFlag : oldFlags) {
      if (!newFlags.contains(oldFlag)) {
        throw new PlatformServiceException(
            BAD_REQUEST, oldFlag + " is not present in the requested db version " + version);
      }
    }
  }

  private void validateYsqlMigrationFilesPresent(String oldVersion, String newVersion) {
    try {
      if (newVersion.startsWith("2025.1.0")) {
        log.info("Skipping YSQL migration files validation for 2025.1.0 version");
        return;
      }
      Set<String> oldMigrationFiles = gFlagsValidation.getYsqlMigrationFilesList(oldVersion);
      Set<String> newMigrationFiles = gFlagsValidation.getYsqlMigrationFilesList(newVersion);
      Set<String> missingFiles = new java.util.HashSet<>(oldMigrationFiles);
      missingFiles.removeAll(newMigrationFiles);
      if (!missingFiles.isEmpty()) {
        throw new PlatformServiceException(
            BAD_REQUEST,
            "The following YSQL migration files present in the current DB version are missing in"
                + " the new version: "
                + missingFiles);
      }
    } catch (PlatformServiceException e) {
      throw e;
    } catch (Exception e) {
      log.error("Error occurred while validating YSQL migration files: ", e);
      throw new PlatformServiceException(INTERNAL_SERVER_ERROR, e.getMessage());
    }
  }

  private void validateYSQLMajorUpgrade(
      Universe universe, String currentVersion, String newVersion) {
    boolean isYsqlMajorVersionUpgrade =
        gFlagsValidation.ysqlMajorVersionUpgrade(currentVersion, newVersion);
    UserIntent currentIntent = universe.getUniverseDetails().getPrimaryCluster().userIntent;
    if (isYsqlMajorVersionUpgrade && currentIntent.enableYSQL) {
      if (Util.compareYBVersions(
              currentVersion, "2024.2.3.0-b1", "2.25.0.0-b1", true /* suppressFormatError */)
          < 0) {
        throw new PlatformServiceException(
            Status.BAD_REQUEST,
            "YSQL major version upgrade is only supported from 2024.2.3.0-b1. Please upgrade to a"
                + " version >= 2024.2.3.0-b1 before proceeding with the upgrade.");
      }

      for (Cluster cluster : universe.getUniverseDetails().clusters) {
        for (NodeDetails node : universe.getNodesInCluster(cluster.uuid)) {
          if (node.isMaster) {
            validateYSQLHBAConfEntriesForYSQLMajorUpgrade(
                universe, cluster, node, ServerType.MASTER);
          }
          if (node.isTserver) {
            validateYSQLHBAConfEntriesForYSQLMajorUpgrade(
                universe, cluster, node, ServerType.TSERVER);
          }
        }
      }
    }
  }

  private void validateYSQLHBAConfEntriesForYSQLMajorUpgrade(
      Universe universe, Cluster cluster, NodeDetails node, ServerType serverType) {
    Map<String, String> gflag =
        GFlagsUtil.getGFlagsForNode(
            node, serverType, cluster, universe.getUniverseDetails().clusters);
    if (gflag.containsKey(GFlagsUtil.YSQL_HBA_CONF_CSV)) {
      String hbaConfValue = gflag.get(GFlagsUtil.YSQL_HBA_CONF_CSV);
      if (StringUtils.isEmpty(hbaConfValue)) {
        return;
      }
      String regex = "clientcert\\s*=\\s*(\\d+)";
      Pattern pattern = Pattern.compile(regex, Pattern.CASE_INSENSITIVE);
      Matcher matcher = pattern.matcher(hbaConfValue);
      if (matcher.find()) {
        String value = matcher.group(1);
        if (value.equals("1")) {
          throw new PlatformServiceException(
              Status.BAD_REQUEST,
              "YSQL major version upgrade is not supported when clientcert=1 is present in the"
                  + " ysql_hba_conf_csv. Please update the clientcert=1 entry with equivalent PG-15"
                  + " value with before proceeding with the upgrade. Update the value to"
                  + " clientcert=verify-ca or clientcert=verify-full before proceeding.");
        }
      }
    }
  }
}
