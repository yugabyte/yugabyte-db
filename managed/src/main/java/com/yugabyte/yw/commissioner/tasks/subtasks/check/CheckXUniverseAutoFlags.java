// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks.check;

import static com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.params.ServerSubTaskParams;
import com.yugabyte.yw.commissioner.tasks.subtasks.ServerSubTaskBase;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.gflags.GFlagDetails;
import com.yugabyte.yw.common.gflags.GFlagsUtil;
import com.yugabyte.yw.common.gflags.GFlagsValidation;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CommonUtils;
import java.io.IOException;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.function.Function;
import java.util.stream.Collectors;
import lombok.extern.slf4j.Slf4j;
import org.yb.WireProtocol.AutoFlagsConfigPB;
import org.yb.client.YBClient;

@Slf4j
public class CheckXUniverseAutoFlags extends ServerSubTaskBase {

  private final YBClientService ybClientService;
  private final GFlagsValidation gFlagsValidation;

  @Inject
  protected CheckXUniverseAutoFlags(
      BaseTaskDependencies baseTaskDependencies,
      YBClientService ybClientService,
      GFlagsValidation gFlagsValidation) {
    super(baseTaskDependencies);
    this.ybClientService = ybClientService;
    this.gFlagsValidation = gFlagsValidation;
  }

  public static class Params extends ServerSubTaskParams {
    public UUID sourceUniverseUUID;
    public UUID targetUniverseUUID;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public void run() {
    Universe sourceUniverse = Universe.getOrBadRequest(taskParams().sourceUniverseUUID);
    String sourceUniverseSoftwareVersion =
        sourceUniverse.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion;
    boolean isSourceUniverseAFCompatible =
        CommonUtils.isAutoFlagSupported(sourceUniverseSoftwareVersion);

    Universe targetUniverse = Universe.getOrBadRequest(taskParams().targetUniverseUUID);
    String targetUniverseSoftwareVersion =
        targetUniverse.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion;
    boolean isTargetUniverseAFCompatible =
        CommonUtils.isAutoFlagSupported(targetUniverseSoftwareVersion);

    if (!isSourceUniverseAFCompatible && !isTargetUniverseAFCompatible) {
      log.warn("Skipping auto flags compatibility check as both universe are not AF compatible.");
      return;
    }
    try {
      // Check the existence of promoted auto flags on the other universe.
      checkPromotedAutoFlagsOnTargetUniverse(sourceUniverse, targetUniverse, ServerType.MASTER);
      checkPromotedAutoFlagsOnTargetUniverse(sourceUniverse, targetUniverse, ServerType.TSERVER);
      checkPromotedAutoFlagsOnTargetUniverse(targetUniverse, sourceUniverse, ServerType.MASTER);
      checkPromotedAutoFlagsOnTargetUniverse(targetUniverse, sourceUniverse, ServerType.TSERVER);
    } catch (PlatformServiceException pe) {
      log.error("Error checking auto flags: ", pe);
      throw pe;
    } catch (IOException e) {
      log.error("Error checking auto flags: ", e);
      throw new PlatformServiceException(INTERNAL_SERVER_ERROR, e.getMessage());
    }
    log.info("Completed {}", getName());
  }

  /**
   * Gets auto flag config of a universe.
   *
   * @param universe
   * @return autoFlagConfig
   */
  private AutoFlagsConfigPB getAutoFlagConfigForUniverse(Universe universe) {
    String masterAddresses = universe.getMasterAddresses();
    String certificate = universe.getCertificateNodetoNode();
    try (YBClient client = ybClientService.getClient(masterAddresses, certificate)) {
      return client.autoFlagsConfig().getAutoFlagsConfig();
    } catch (Exception e) {
      log.error(
          "Error occurred while fetching auto flags config for universe " + universe + ": ", e);
      throw new PlatformServiceException(INTERNAL_SERVER_ERROR, e.getMessage());
    }
  }

  /**
   * Gets promoted auto flags map with target value on a universe.
   *
   * @param universe
   * @param serverType
   * @return map of string which contains flags and their values.
   * @throws IOException
   */
  private Map<String, String> getPromotedAutoFlagsWithTargetValues(
      Universe universe, ServerType serverType) throws IOException {
    AutoFlagsConfigPB autoFlagsConfigPB = getAutoFlagConfigForUniverse(universe);
    List<String> promotedAutoFlagsList =
        autoFlagsConfigPB
            .getPromotedFlagsList()
            .stream()
            .filter(
                promotedFlagsPerProcessPB -> {
                  return promotedFlagsPerProcessPB
                      .getProcessName()
                      .equals(ServerType.MASTER.equals(serverType) ? "yb-master" : "yb-tserver");
                })
            .findFirst()
            .get()
            .getFlagsList();
    String version = universe.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion;
    Map<String, GFlagDetails> autoFlagsMetadataMap =
        gFlagsValidation
            .listAllAutoFlags(version, serverType.name())
            .stream()
            .collect(Collectors.toMap(flagDetails -> flagDetails.name, Function.identity()));
    Map<String, String> promotedAutoFlagsWithValues = new HashMap<>();
    for (String flag : promotedAutoFlagsList) {
      promotedAutoFlagsWithValues.put(flag, autoFlagsMetadataMap.get(flag).target);
    }
    for (Map.Entry<String, String> entry :
        GFlagsUtil.getBaseGFlags(
                serverType,
                universe.getUniverseDetails().getPrimaryCluster(),
                universe.getUniverseDetails().clusters)
            .entrySet()) {
      if (autoFlagsMetadataMap.containsKey(entry.getKey())) {
        promotedAutoFlagsWithValues.put(entry.getKey(), entry.getValue());
      }
    }
    return promotedAutoFlagsWithValues;
  }

  /**
   * Validates that every promoted auto flags of the source universe exists in the target universe.
   *
   * @param sourceUniverse
   * @param targetUniverse
   * @param serverType
   * @throws IOException
   */
  private void checkPromotedAutoFlagsOnTargetUniverse(
      Universe sourceUniverse, Universe targetUniverse, ServerType serverType) throws IOException {
    String sourceUniverseVersion =
        sourceUniverse.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion;
    // Prepare map of promoted auto flags with user overridden auto flags.
    Map<String, String> promotedAutoFlags =
        getPromotedAutoFlagsWithTargetValues(sourceUniverse, serverType);
    promotedAutoFlags =
        gFlagsValidation.getFilteredAutoFlagsWithNonInitialValue(
            promotedAutoFlags, sourceUniverseVersion, serverType);
    // Fetch set of auto flags supported on target universe.
    String targetUniverseVersion =
        targetUniverse.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion;
    Set<String> supportedAutoFlags =
        gFlagsValidation
            .listAllAutoFlags(targetUniverseVersion, serverType.name())
            .stream()
            .map(flagDetails -> flagDetails.name)
            .collect(Collectors.toSet());
    // Compare.
    promotedAutoFlags.forEach(
        (flag, value) -> {
          if (!supportedAutoFlags.contains(flag)) {
            throw new PlatformServiceException(
                BAD_REQUEST,
                "Auto Flag "
                    + flag
                    + " set on universe "
                    + sourceUniverse.getUniverseUUID()
                    + " is not present on universe "
                    + targetUniverse.getUniverseUUID());
          }
        });
  }
}
