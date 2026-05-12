package com.yugabyte.yw.commissioner.tasks.subtasks;

import static com.yugabyte.yw.common.Util.isIpAddress;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.tasks.UniverseDefinitionTaskBase;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.RedactingService;
import com.yugabyte.yw.common.ShellProcessContext;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.gflags.GFlagDetails;
import com.yugabyte.yw.common.gflags.GFlagsUtil;
import com.yugabyte.yw.common.gflags.GFlagsValidation;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.forms.UpgradeTaskParams;
import com.yugabyte.yw.forms.UpgradeTaskParams.UpgradeTaskType;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CloudInfoInterface;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.provider.LocalCloudInfo;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;
import org.apache.commons.lang3.exception.ExceptionUtils;
import org.yb.client.YBClient;

@Slf4j
public class ValidateGFlags extends UniverseDefinitionTaskBase {

  private final YBClientService ybClientService;

  @Inject
  protected ValidateGFlags(
      BaseTaskDependencies baseTaskDependencies, YBClientService ybClientService) {
    super(baseTaskDependencies);
    this.ybClientService = ybClientService;
  }

  public static class Params extends UniverseDefinitionTaskParams {
    public List<UniverseDefinitionTaskParams.Cluster> newClusters;
    public String ybSoftwareVersion;
    public boolean useCLIBinary;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public void run() {
    Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
    List<UniverseDefinitionTaskParams.Cluster> clusters = universe.getUniverseDetails().clusters;

    if (clusters == null || clusters.isEmpty()) {
      log.info("No clusters found for validation, skipping validation");
      return;
    }

    GFlagsValidation.GFlagsValidationErrors gFlagsValidationErrors =
        new GFlagsValidation.GFlagsValidationErrors();

    for (UniverseDefinitionTaskParams.Cluster cluster : clusters) {
      List<NodeDetails> nodes =
          universe.getNodes().stream()
              .filter(n -> n.placementUuid.equals(cluster.uuid))
              .collect(Collectors.toList());

      if (nodes.isEmpty()) {
        log.debug("No nodes found for cluster {}, skipping validation", cluster.uuid);
        continue;
      }

      Map<UUID, List<NodeDetails>> nodesGroupedByAZs =
          nodes.stream().collect(Collectors.groupingBy(node -> node.azUuid));

      UniverseDefinitionTaskParams.Cluster newCluster = null;
      if (taskParams().newClusters != null) {
        newCluster =
            taskParams().newClusters.stream()
                .filter(c -> c.uuid.equals(cluster.uuid))
                .findFirst()
                .orElse(null);
      }
      validateClusterGFlags(
          cluster, newCluster, universe, nodesGroupedByAZs, gFlagsValidationErrors);
    }
    for (GFlagsValidation.GFlagsValidationErrorsPerAZ gFlagsValidationErrorsPerAZ :
        gFlagsValidationErrors.gFlagsValidationErrorsPerAZs) {
      if (!gFlagsValidationErrorsPerAZ.masterGFlagsErrors.isEmpty()
          || !gFlagsValidationErrorsPerAZ.tserverGFlagsErrors.isEmpty()) {
        throw new PlatformServiceException(
            BAD_REQUEST, String.format("GFlags validation failed %s", gFlagsValidationErrors));
      }
    }
  }

  private void validateClusterGFlags(
      UniverseDefinitionTaskParams.Cluster cluster,
      UniverseDefinitionTaskParams.Cluster newCluster,
      Universe universe,
      Map<UUID, List<NodeDetails>> nodesGroupedByAZs,
      GFlagsValidation.GFlagsValidationErrors gFlagsValidationErrors) {

    // Load metadata once per server type for the whole cluster - it only depends on
    // ybSoftwareVersion and serverType, not on AZ or node, so there is no reason to
    // re-fetch it inside every per-AZ call to validateGFlagsWithYBServerBinary.
    Map<String, GFlagDetails> masterGflagMeta = Collections.emptyMap();
    Map<String, GFlagDetails> tserverGflagMeta = Collections.emptyMap();
    if (taskParams().useCLIBinary) {
      masterGflagMeta = loadGflagMeta(ServerType.MASTER);
      tserverGflagMeta = loadGflagMeta(ServerType.TSERVER);
    }

    boolean hasAnyFailure = false;

    for (Map.Entry<UUID, List<NodeDetails>> nodesMappedWithAZ : nodesGroupedByAZs.entrySet()) {
      UUID azUuid = nodesMappedWithAZ.getKey();
      List<NodeDetails> nodesInAZ = nodesMappedWithAZ.getValue();

      try {
        validateGFlagsForAZ(
            azUuid,
            nodesInAZ,
            cluster,
            newCluster,
            universe,
            gFlagsValidationErrors,
            masterGflagMeta,
            tserverGflagMeta);
        log.info("Completed gflags validation for AZ {}", azUuid);
      } catch (Exception e) {
        log.warn("Failed to validate gflags in AZ {} (going to next AZ)", azUuid, e);
        hasAnyFailure = true;
      }
    }

    // If any AZ failed validation, throw exception
    if (hasAnyFailure) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Failed to validate gflags in one or more AZs in cluster " + cluster.uuid);
    }

    log.info(String.format("Completed gflags validation for all AZs in cluster %s", cluster.uuid));
  }

  private void validateGFlagsForAZ(
      UUID azUuid,
      List<NodeDetails> nodesInAZ,
      UniverseDefinitionTaskParams.Cluster cluster,
      UniverseDefinitionTaskParams.Cluster newCluster,
      Universe universe,
      GFlagsValidation.GFlagsValidationErrors gFlagsValidationErrors,
      Map<String, GFlagDetails> masterGflagMeta,
      Map<String, GFlagDetails> tserverGflagMeta) {

    Map<String, String> masterGFlagsForAZ = new HashMap<>();
    Map<String, String> tserverGFlagsForAZ = new HashMap<>();
    GFlagsValidation.GFlagsValidationErrorsPerAZ gFlagsValidationErrorsPerAZ =
        new GFlagsValidation.GFlagsValidationErrorsPerAZ();
    gFlagsValidationErrorsPerAZ.azUuid = azUuid;
    gFlagsValidationErrorsPerAZ.clusterUuid = cluster.uuid;

    Map<String, String> masterGFlagsValidationErrors = new HashMap<>();
    Map<String, String> tserverGFlagsValidationErrors = new HashMap<>();

    NodeDetails masterNode =
        nodesInAZ.stream()
            .filter(node -> node.isMaster)
            .filter(node -> node.cloudInfo != null && node.cloudInfo.private_ip != null)
            .findFirst()
            .orElse(null);
    NodeDetails tserverNode =
        nodesInAZ.stream()
            .filter(node -> node.isTserver)
            .filter(node -> node.cloudInfo != null && node.cloudInfo.private_ip != null)
            .findFirst()
            .orElse(null);

    if (masterNode != null) {
      try {
        masterGFlagsForAZ =
            buildGFlagsForValidation(
                masterNode, cluster, newCluster, universe, ServerType.MASTER, azUuid);
      } catch (Exception e) {
        log.error(
            "Error in collecting master gflags from node {} in AZ {}",
            masterNode.nodeName,
            azUuid,
            e);
        throw new PlatformServiceException(
            BAD_REQUEST,
            String.format(
                "Failed to collect master gflags from node '%s' in AZ '%s': %s",
                masterNode.nodeName, azUuid, e.getMessage()));
      }
    }

    if (tserverNode != null) {
      try {
        tserverGFlagsForAZ =
            buildGFlagsForValidation(
                tserverNode, cluster, newCluster, universe, ServerType.TSERVER, azUuid);
      } catch (Exception e) {
        log.error(
            "Error in collecting tserver gflags from node {} in AZ {}",
            tserverNode.nodeName,
            azUuid,
            e);
        throw new PlatformServiceException(
            BAD_REQUEST,
            String.format(
                "Failed to collect tserver gflags from node '%s' in AZ '%s': %s",
                tserverNode.nodeName, azUuid, e.getMessage()));
      }
    }

    masterGFlagsForAZ = filterUndefokFlags(masterGFlagsForAZ);
    tserverGFlagsForAZ = filterUndefokFlags(tserverGFlagsForAZ);

    if (taskParams().useCLIBinary) {
      if (!masterGFlagsForAZ.isEmpty()) {
        masterGFlagsValidationErrors.putAll(
            validateGFlagsWithYBServerBinary(
                masterGFlagsForAZ, universe, ServerType.MASTER, masterNode, masterGflagMeta));
      }
      if (!tserverGFlagsForAZ.isEmpty()) {
        tserverGFlagsValidationErrors.putAll(
            validateGFlagsWithYBServerBinary(
                tserverGFlagsForAZ, universe, ServerType.TSERVER, tserverNode, tserverGflagMeta));
      }
    } else {
      if (!masterGFlagsForAZ.isEmpty()) {
        masterGFlagsValidationErrors.putAll(
            validateGFlagsWithYBClient(masterGFlagsForAZ, universe, ServerType.MASTER));
        log.info(
            "Completed validation for all master gflags for AZ {}: {}",
            azUuid,
            masterGFlagsForAZ.keySet());
      }

      if (!tserverGFlagsForAZ.isEmpty()) {
        tserverGFlagsValidationErrors.putAll(
            validateGFlagsWithYBClient(tserverGFlagsForAZ, universe, ServerType.TSERVER));
        log.info(
            "Completed validation for all tserver gflags for AZ {}: {}",
            azUuid,
            tserverGFlagsForAZ.keySet());
      }
    }

    if (masterGFlagsForAZ.isEmpty() && tserverGFlagsForAZ.isEmpty()) {
      log.warn("No gflags collected from any nodes in AZ {}", azUuid);
    }

    if (!masterGFlagsValidationErrors.isEmpty()) {
      gFlagsValidationErrorsPerAZ.masterGFlagsErrors.putAll(masterGFlagsValidationErrors);
    }
    if (!tserverGFlagsValidationErrors.isEmpty()) {
      gFlagsValidationErrorsPerAZ.tserverGFlagsErrors.putAll(tserverGFlagsValidationErrors);
    }

    gFlagsValidationErrors.gFlagsValidationErrorsPerAZs.add(gFlagsValidationErrorsPerAZ);
  }

  private Map<String, String> buildGFlagsForValidation(
      NodeDetails node,
      UniverseDefinitionTaskParams.Cluster cluster,
      UniverseDefinitionTaskParams.Cluster newCluster,
      Universe universe,
      ServerType serverType,
      UUID azUuid) {

    UniverseDefinitionTaskParams.Cluster targetCluster = newCluster != null ? newCluster : cluster;
    UserIntent userIntent = targetCluster.userIntent;

    boolean useHostname =
        universe.getUniverseDetails().getPrimaryCluster().userIntent.useHostname
            || (node.cloudInfo != null
                && node.cloudInfo.private_ip != null
                && !isIpAddress(node.cloudInfo.private_ip));

    // GFlags set by platform
    Map<String, String> gflags =
        new HashMap<>(
            GFlagsUtil.getAllDefaultGFlags(
                getAnsibleConfigureServerParams(
                    node,
                    serverType,
                    UpgradeTaskType.GFlags,
                    UpgradeTaskParams.UpgradeTaskSubType.None),
                universe,
                userIntent,
                useHostname,
                confGetter));

    // GFlags set by user previously
    if (userIntent.specificGFlags == null) {
      Map<String, String> oldUserSetGFlags =
          GFlagsUtil.getGFlagsForNode(
              node, serverType, targetCluster, universe.getUniverseDetails().clusters);
      if (oldUserSetGFlags != null && !oldUserSetGFlags.isEmpty()) {
        gflags.putAll(oldUserSetGFlags);
      }
    }

    // GFlags to be set for this AZ
    if (userIntent.specificGFlags != null) {
      Map<String, String> newGFlagsForAZ = userIntent.specificGFlags.getGFlags(azUuid, serverType);
      if (newGFlagsForAZ != null && !newGFlagsForAZ.isEmpty()) {
        gflags.putAll(newGFlagsForAZ);
      }
    }

    try {
      nodeManager.processGFlags(
          config,
          universe,
          node,
          getAnsibleConfigureServerParams(
              node, serverType, UpgradeTaskType.GFlags, UpgradeTaskParams.UpgradeTaskSubType.None),
          gflags,
          useHostname);
    } catch (Exception e) {
      // At this point - already caught an exception, now checking if CSV related.
      if (e.getMessage() != null && e.getMessage().contains("CSV")) {
        List<String> csvGflags =
            gflags.keySet().stream()
                .filter(key -> key.endsWith("_csv"))
                .collect(Collectors.toList());

        if (!csvGflags.isEmpty()) {
          String csvGflagDetails =
              csvGflags.stream()
                  .map(
                      key ->
                          key
                              + "='"
                              + RedactingService.redactSensitiveInfoInString(
                                  gflags.get(key), taskParams().ybSoftwareVersion, gFlagsValidation)
                              + "'")
                  .collect(Collectors.joining(", "));

          log.error(
              "Failed to process CSV gflag(s) for {} on node {} in AZ {}. CSV gflags: [{}]",
              serverType,
              node.nodeName,
              azUuid,
              csvGflagDetails,
              e);
          throw new PlatformServiceException(
              BAD_REQUEST,
              String.format(
                  "Failed to process CSV gflag(s) for %s on node '%s': %s. CSV gflag(s) present:"
                      + " [%s]. Please check for malformed CSV values (e.g., unclosed quotes,"
                      + " invalid format).",
                  serverType, node.nodeName, e.getMessage(), csvGflagDetails));
        }
      }
      log.error(
          "Failed to process gflags for {} on node {} in AZ {}",
          serverType,
          node.nodeName,
          azUuid,
          e);
      throw e;
    }

    log.info(
        "Combined gflags for server type {} on node {} in AZ {}: {}",
        serverType,
        node.nodeName,
        azUuid,
        RedactingService.redactSensitiveInfoInString(
            gflags.toString(), taskParams().ybSoftwareVersion, gFlagsValidation));

    return gflags;
  }

  private Map<String, String> filterUndefokFlags(Map<String, String> gflags) {
    Set<String> undefokFlags = GFlagsUtil.extractUndefokFlags(gflags);

    if (undefokFlags.isEmpty()) {
      return gflags;
    }

    Map<String, String> filteredGFlags = new HashMap<>(gflags);
    for (String undefokFlag : undefokFlags) {
      if (filteredGFlags.containsKey(undefokFlag)) {
        filteredGFlags.remove(undefokFlag);
      }
    }

    return filteredGFlags;
  }

  private Map<String, GFlagDetails> loadGflagMeta(ServerType serverType) {
    Map<String, GFlagDetails> meta = new HashMap<>();
    try {
      for (GFlagDetails d :
          gFlagsValidation.extractGFlags(
              taskParams().ybSoftwareVersion, serverType.name(), false)) {
        meta.put(d.name, d);
      }
    } catch (Exception e) {
      log.error(
          "Got error while fetching gflags metadata {} {}: {}",
          serverType,
          taskParams().ybSoftwareVersion,
          e);
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR,
          String.format(
              "Failed to fetch gflags metadata for %s %s: %s",
              serverType, taskParams().ybSoftwareVersion, e.getMessage()));
    }
    return meta;
  }

  private Map<String, String> validateGFlagsWithYBClient(
      Map<String, String> gflags, Universe universe, ServerType serverType) {
    Map<String, String> serverGFlagsValidationErrors = new HashMap<String, String>();
    try (YBClient client = ybClientService.getUniverseClient(universe)) {
      serverGFlagsValidationErrors = gFlagsValidation.validateGFlags(client, gflags, serverType);
      return serverGFlagsValidationErrors;
    } catch (Exception e) {
      log.error("Error in validating gflags with YBClient", e);
      throw new PlatformServiceException(
          BAD_REQUEST,
          String.format("Failed to validate gflags with YBClient: %s", e.getMessage()));
    }
  }

  private Map<String, String> validateGFlagsWithYBServerBinary(
      Map<String, String> gflags,
      Universe universe,
      ServerType serverType,
      NodeDetails node,
      Map<String, GFlagDetails> gflagMeta) {
    Map<String, String> serverGFlagsValidationErrors = new HashMap<>();

    Provider provider = Util.getProviderForNode(node, universe);

    String cliPath;
    if (provider.getCloudCode() == CloudType.local) {
      LocalCloudInfo localCloudInfo = CloudInfoInterface.get(provider);
      String yugabyteBinDir = localCloudInfo.getYugabyteBinDir();
      if (serverType == ServerType.MASTER) {
        cliPath = yugabyteBinDir + "/yb-master";
      } else if (serverType == ServerType.TSERVER) {
        cliPath = yugabyteBinDir + "/yb-tserver";
      } else {
        return serverGFlagsValidationErrors;
      }
    } else {
      String ybHomeDir = nodeUniverseManager.getYbHomeDir(node, universe);
      if (serverType == ServerType.MASTER) {
        cliPath = ybHomeDir + "/master/bin/yb-master";
      } else if (serverType == ServerType.TSERVER) {
        cliPath = ybHomeDir + "/tserver/bin/yb-tserver";
      } else {
        return serverGFlagsValidationErrors;
      }
    }

    ShellProcessContext shellContext =
        ShellProcessContext.builder()
            .logCmdOutput(false)
            .traceLogging(true)
            .timeoutSecs(120)
            .build();

    List<String> command = new ArrayList<>();
    command.add(cliPath);
    command.add("--version");

    for (Map.Entry<String, String> gflag : gflags.entrySet()) {
      appendGflagCliArg(command, gflag.getKey(), gflag.getValue(), gflagMeta.get(gflag.getKey()));
    }

    log.debug(
        "About to run command: {} using yb-server binary.",
        RedactingService.redactSensitiveInfoInString(
            command.toString(), taskParams().ybSoftwareVersion, gFlagsValidation));

    try {
      // Not using bash since some gflag values may have complicated escaping needed for bash case
      ShellResponse response =
          nodeUniverseManager.runCommand(
              node, universe, command, shellContext, false /* use bash */);
      if (response.code != 0) {
        log.warn(
            "Shell response returned with non-zero exit code with message: {}",
            RedactingService.redactSensitiveInfoInString(
                response.message, taskParams().ybSoftwareVersion, gFlagsValidation));
        // All gflag validation errors from the binary start with "ERROR" prefix.
        if (response.message.contains("ERROR")) {
          serverGFlagsValidationErrors.put(
              serverType.toString(),
              "Invalid gflags detected. "
                  + RedactingService.redactSensitiveInfoInString(
                      response.message, taskParams().ybSoftwareVersion, gFlagsValidation));
        } else {
          throw new PlatformServiceException(
              INTERNAL_SERVER_ERROR,
              "Command to call RPC failed: "
                  + RedactingService.redactSensitiveInfoInString(
                      response.message, taskParams().ybSoftwareVersion, gFlagsValidation));
        }
      }
    } catch (Exception e) {
      String fullExceptionString = ExceptionUtils.getStackTrace(e);
      String redactedFullException =
          RedactingService.redactSensitiveInfoInString(
              fullExceptionString, taskParams().ybSoftwareVersion, gFlagsValidation);

      log.warn(
          "Error while validating gflags on node {} using yb-server CLI:\n{}",
          node.nodeName,
          redactedFullException);
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR,
          "Error validating flags on node '"
              + node.nodeName
              + "': "
              + RedactingService.redactSensitiveInfoInString(
                  e.getMessage(), taskParams().ybSoftwareVersion, gFlagsValidation));
    }
    return serverGFlagsValidationErrors;
  }

  /**
   * Non-bool: {@code --name=value}. Bool: {@code --name} / {@code --noname} or else {@code
   * --name=value}.
   */
  static void appendGflagCliArg(
      List<String> command, String name, String value, GFlagDetails meta) {
    boolean isBool = meta != null && meta.type != null && "bool".equalsIgnoreCase(meta.type.trim());
    // All incoming flags (YBA-set and user-set) are merged upstream in buildGFlagsForValidation
    // and every entry reaches this point.
    if (!isBool) {
      command.add("--" + name + "=" + StringUtils.defaultString(value));
      return;
    }
    String v = StringUtils.trimToEmpty(value);
    if (v.isEmpty() || v.equalsIgnoreCase("false") || v.equals("0")) {
      command.add("--no" + name);
    } else if (v.equalsIgnoreCase("true") || v.equals("1")) {
      command.add("--" + name);
    } else {
      command.add("--" + name + "=" + v);
    }
  }
}
