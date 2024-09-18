// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.gflags;

import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.google.common.collect.ImmutableSet;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CommonUtils;
import java.io.IOException;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashSet;
import java.util.Iterator;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.Set;
import java.util.function.Function;
import java.util.stream.Collectors;
import lombok.extern.slf4j.Slf4j;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.WireProtocol;
import org.yb.WireProtocol.PromotedFlagsPerProcessPB;
import org.yb.client.YBClient;

@Singleton
@Slf4j
public class AutoFlagUtil {

  private final YBClientService ybClientService;
  private final GFlagsValidation gFlagsValidation;

  // Info about auto flags class can be found here
  // https://github.com/yugabyte/yugabyte-db/blob/master/architecture/design/auto_flags.md
  public static int LOCAL_VOLATILE_AUTO_FLAG_CLASS = 1;
  public static String LOCAL_VOLATILE_AUTO_FLAG_CLASS_NAME = "kLocalVolatile";

  public static int LOCAL_PERSISTED_AUTO_FLAG_CLASS = 2;
  public static String LOCAL_PERSISTED_AUTO_FLAG_CLASS_NAME = "kLocalPersisted";

  public static int EXTERNAL_AUTO_FLAG_CLASS = 3;
  public static String EXTERNAL_AUTO_FLAG_CLASS_NAME = "kExternal";

  public static final Logger LOG = LoggerFactory.getLogger(AutoFlagUtil.class);

  @Inject
  public AutoFlagUtil(GFlagsValidation gFlagsValidation, YBClientService ybClientService) {
    this.gFlagsValidation = gFlagsValidation;
    this.ybClientService = ybClientService;
  }

  /**
   * Gets auto flag config of a universe.
   *
   * @param universe
   * @return autoFlagConfig
   */
  private WireProtocol.AutoFlagsConfigPB getAutoFlagConfigForUniverse(Universe universe) {
    String masterAddresses = universe.getMasterAddresses();
    String certificate = universe.getCertificateNodetoNode();
    try (YBClient client = ybClientService.getClient(masterAddresses, certificate)) {
      return client.autoFlagsConfig().getAutoFlagsConfig();
    } catch (Exception e) {
      LOG.error(
          "Error occurred while fetching auto flags config for universe " + universe + ": ", e);
      throw new PlatformServiceException(INTERNAL_SERVER_ERROR, e.getMessage());
    }
  }

  /**
   * Returns set of string containing promoted and modified auto flags having class higher than
   * skipLowerAutoFlagClass for a server(master/tserver).
   *
   * @param universe
   * @param serverType
   * @param skipLowerAutoFlagClass
   * @return
   * @throws IOException
   */
  public Set<String> getPromotedAutoFlags(
      Universe universe, UniverseTaskBase.ServerType serverType, int skipLowerAutoFlagClass)
      throws IOException {
    // Fetch promoted auto flags list from DB itself.
    WireProtocol.AutoFlagsConfigPB autoFlagsConfigPB = getAutoFlagConfigForUniverse(universe);

    Optional<PromotedFlagsPerProcessPB> promotedFlagsPerProcessPB =
        autoFlagsConfigPB.getPromotedFlagsList().stream()
            .filter(
                flagsPerProcessPB -> {
                  return flagsPerProcessPB
                      .getProcessName()
                      .equals(
                          UniverseTaskBase.ServerType.MASTER.equals(serverType)
                              ? "yb-master"
                              : "yb-tserver");
                })
            .findFirst();

    if (!promotedFlagsPerProcessPB.isPresent()) {
      return new HashSet<>();
    }

    Set<String> autoFlags =
        promotedFlagsPerProcessPB.get().getFlagsList().stream()
            .filter(flag -> !GFlagsValidation.TEST_AUTO_FLAGS.contains(flag))
            .collect(Collectors.toSet());

    // Add auto flags which are modified through gflags override in the promoted auto flags list.
    String version = universe.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion;
    Map<String, GFlagsValidation.AutoFlagDetails> autoFlagDetails =
        gFlagsValidation.extractAutoFlags(version, serverType).autoFlagDetails.stream()
            .collect(Collectors.toMap(flag -> flag.name, Function.identity()));
    for (Map.Entry<String, String> entry :
        GFlagsUtil.getBaseGFlags(
                serverType,
                universe.getUniverseDetails().getPrimaryCluster(),
                universe.getUniverseDetails().clusters)
            .entrySet()) {
      if (autoFlagDetails.containsKey(entry.getKey())) {
        autoFlags.add(entry.getKey());
      }
    }

    // Remove auto flags having class lower or equal to skipLowerAutoFlagClass
    Iterator<String> itr = autoFlags.iterator();
    while (itr.hasNext()) {
      String autoFlag = itr.next();
      if (autoFlagDetails.containsKey(autoFlag)
          && autoFlagDetails.get(autoFlag).flagClass <= skipLowerAutoFlagClass) {
        itr.remove();
      }
    }
    return autoFlags;
  }

  public boolean upgradeRequireFinalize(String oldVersion, String newVersion) throws IOException {
    for (ServerType serverType : ImmutableSet.of(ServerType.MASTER, ServerType.TSERVER)) {
      Set<GFlagsValidation.AutoFlagDetails> oldAutoFlags =
          new HashSet<>(gFlagsValidation.extractAutoFlags(oldVersion, serverType).autoFlagDetails);
      Set<GFlagsValidation.AutoFlagDetails> newAutoFlags =
          new HashSet<>(gFlagsValidation.extractAutoFlags(newVersion, serverType).autoFlagDetails);
      Set<String> newFlags =
          newAutoFlags.stream()
              .filter(
                  flag ->
                      flag.flagClass != LOCAL_VOLATILE_AUTO_FLAG_CLASS
                          && !oldAutoFlags.contains(flag))
              .map(flag -> flag.name)
              .collect(Collectors.toSet());
      if (newFlags.size() != 0) {
        LOG.debug(
            "Upgrade from {} to {} will require finalize as new auto flags for {} are added {}.",
            oldVersion,
            newVersion,
            serverType,
            newFlags);
        return true;
      }
    }

    Set<String> oldMigrationFiles =
        new HashSet<>(gFlagsValidation.getYsqlMigrationFilesList(oldVersion));
    Set<String> newMigrationFiles =
        new HashSet<>(gFlagsValidation.getYsqlMigrationFilesList(newVersion));
    newMigrationFiles.removeAll(oldMigrationFiles);
    if (newMigrationFiles.size() != 0) {
      LOG.debug(
          "Upgrade from {} to {} will require finalize as new migration files are added {}.",
          oldVersion,
          newVersion,
          newMigrationFiles);
      return true;
    }

    return false;
  }

  /**
   * Validates that every promoted auto flag on the source universe is also promoted on the target
   * universe. i.e. The set of promoted autoflags on the source universe is a subset of the promoted
   * autoflags on the target universe.
   *
   * @param sourceUniverse
   * @param targetUniverse
   * @param serverType
   * @throws IOException
   */
  public void checkSourcePromotedAutoFlagsPromotedOnTarget(
      Universe sourceUniverse, Universe targetUniverse) {

    String sourceUniverseSoftwareVersion =
        sourceUniverse.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion;
    boolean isSourceUniverseAFCompatible =
        CommonUtils.isAutoFlagSupported(sourceUniverseSoftwareVersion);

    String targetUniverseSoftwareVersion =
        targetUniverse.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion;
    boolean isTargetUniverseAFCompatible =
        CommonUtils.isAutoFlagSupported(targetUniverseSoftwareVersion);

    if (!isSourceUniverseAFCompatible && !isTargetUniverseAFCompatible) {
      log.warn("Skipping auto flags compatibility check as both universe are not AF compatible.");
      return;
    }

    List<ServerType> serverTypes =
        new ArrayList<>(Arrays.asList(ServerType.MASTER, ServerType.TSERVER));
    try {
      for (ServerType serverType : serverTypes) {
        Set<String> sourcePromotedAndModifiedAutoFlags = new HashSet<>();
        Set<String> targetPromotedAndModifiedAutoFlags = new HashSet<>();

        if (isSourceUniverseAFCompatible) {
          sourcePromotedAndModifiedAutoFlags =
              getPromotedAutoFlags(sourceUniverse, serverType, LOCAL_PERSISTED_AUTO_FLAG_CLASS);
        }
        if (isTargetUniverseAFCompatible) {
          targetPromotedAndModifiedAutoFlags =
              getPromotedAutoFlags(targetUniverse, serverType, LOCAL_PERSISTED_AUTO_FLAG_CLASS);
        }
        sourcePromotedAndModifiedAutoFlags.removeAll(targetPromotedAndModifiedAutoFlags);
        if (!sourcePromotedAndModifiedAutoFlags.isEmpty()) {
          throw new PlatformServiceException(
              BAD_REQUEST,
              "Auto Flags: "
                  + sourcePromotedAndModifiedAutoFlags
                  + " set on universe "
                  + sourceUniverse.getName()
                  + " are not set on universe "
                  + targetUniverse.getName());
        }
      }
    } catch (IOException e) {
      log.error("Error checking auto flags: ", e);
      throw new PlatformServiceException(INTERNAL_SERVER_ERROR, e.getMessage());
    }
  }

  /**
   * Validates that all promoted/modified autoflags on universe1 are equal to the promoted/modified
   * autoflags on universe2.
   */
  public void checkPromotedAutoFlagsEquality(Universe universe1, Universe universe2) {
    checkSourcePromotedAutoFlagsPromotedOnTarget(universe1, universe2);
    checkSourcePromotedAutoFlagsPromotedOnTarget(universe2, universe1);
  }
}
