// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common;

import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.cloud.PublicCloudConstants.Architecture;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.NodeAgent;
import com.yugabyte.yw.models.NodeAgent.ArchType;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import javax.annotation.Nullable;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;

/**
 * Resolves the CPU architecture for a universe using node-agent metadata and, when needed, by
 * probing a live DB node.
 */
@Slf4j
@Singleton
public class UniverseArchitectureResolver {

  private static final int MAX_LIVE_NODE_PROBE_ATTEMPTS = 3;

  private final NodeUniverseManager nodeUniverseManager;

  @Inject
  public UniverseArchitectureResolver(NodeUniverseManager nodeUniverseManager) {
    this.nodeUniverseManager = nodeUniverseManager;
  }

  /**
   * Attempts to resolve and persist architecture from node-agent metadata for each universe.
   * Returns the universe UUIDs where arch could not be updated.
   */
  public List<UUID> resolveFromMetadata(List<UUID> universeUuids) {
    List<UUID> unresolved = new ArrayList<>();
    for (UUID universeUuid : universeUuids) {
      try {
        Universe universe = Universe.maybeGet(universeUuid).orElse(null);
        if (universe == null || universe.getUniverseDetails().arch != null) {
          continue;
        }
        Optional<Architecture> resolved = resolveFromNodeAgent(universe);
        if (resolved.isPresent()) {
          saveArchIfNull(universeUuid, resolved.get());
        } else {
          unresolved.add(universeUuid);
        }
      } catch (Exception e) {
        log.warn(
            "Failed to resolve architecture from node agent metadata for universe {}: {}",
            universeUuid,
            e.getMessage());
        unresolved.add(universeUuid);
      }
    }
    return unresolved;
  }

  private Optional<Architecture> resolveFromNodeAgent(Universe universe) {
    Customer customer = Customer.get(universe.getCustomerId());
    Set<String> nodeIps =
        universe.getNodes().stream()
            .filter(n -> n.cloudInfo != null && StringUtils.isNotBlank(n.cloudInfo.private_ip))
            .map(n -> n.cloudInfo.private_ip)
            .collect(Collectors.toSet());
    if (nodeIps.isEmpty()) {
      return Optional.empty();
    }
    for (NodeAgent nodeAgent : NodeAgent.getByIps(customer.getUuid(), nodeIps)) {
      if (nodeAgent.getArchType() == null) {
        continue;
      }
      Optional<Architecture> arch = toArchitecture(nodeAgent.getArchType());
      if (arch.isPresent()) {
        return arch;
      }
      log.debug(
          "Unsupported node agent arch type {} for universe {}",
          nodeAgent.getArchType(),
          universe.getUniverseUUID());
    }
    return Optional.empty();
  }

  private static Optional<Architecture> toArchitecture(ArchType archType) {
    switch (archType) {
      case AMD64:
        return Optional.of(Architecture.x86_64);
      case ARM64:
        return Optional.of(Architecture.aarch64);
      default:
        return Optional.empty();
    }
  }

  private void saveArchIfNull(UUID universeUuid, Architecture arch) {
    Universe.saveDetails(
        universeUuid,
        u -> {
          UniverseDefinitionTaskParams universeDetails = u.getUniverseDetails();
          if (universeDetails.arch == null) {
            universeDetails.arch = arch;
            u.setUniverseDetails(universeDetails);
            log.info("Set architecture {} on universe {} ({})", arch, universeUuid, u.getName());
          }
        });
  }

  public Optional<Architecture> resolveFromLiveNode(Universe universe) {
    if (universe.getUniverseDetails().universePaused) {
      log.debug(
          "Skipping node architecture probe for paused universe {}", universe.getUniverseUUID());
      return Optional.empty();
    }
    if (universe.getUniverseDetails().updateInProgress) {
      log.debug(
          "Skipping node architecture probe for universe {} with update in progress",
          universe.getUniverseUUID());
      return Optional.empty();
    }

    List<NodeDetails> probeNodes =
        universe.getNodes().stream()
            .filter(n -> n.isTserver && n.state == NodeState.Live)
            .limit(MAX_LIVE_NODE_PROBE_ATTEMPTS)
            .collect(Collectors.toList());
    if (probeNodes.isEmpty()) {
      return Optional.empty();
    }
    for (NodeDetails node : probeNodes) {
      try {
        Optional<Architecture> resolved = resolveFromVmNode(universe, node);
        if (resolved.isPresent()) {
          return resolved;
        }
      } catch (Exception e) {
        log.debug(
            "Failed to probe architecture on node {} in universe {}: {}",
            node.nodeName,
            universe.getUniverseUUID(),
            e.getMessage());
      }
    }
    return Optional.empty();
  }

  private Optional<Architecture> resolveFromVmNode(Universe universe, NodeDetails node) {
    List<String> cmd = Arrays.asList("uname", "-m");
    ShellResponse response = nodeUniverseManager.runCommand(node, universe, cmd);
    if (response.isSuccess()) {
      String output = response.extractRunCommandOutput();
      if (StringUtils.isNotBlank(output)) {
        return parseMachineArchitecture(output);
      }
      return parseMachineArchitecture(response.message);
    }
    return Optional.empty();
  }

  private Optional<Architecture> parseMachineArchitecture(@Nullable String rawOutput) {
    if (StringUtils.isBlank(rawOutput)) {
      return Optional.empty();
    }
    String archToken = rawOutput.trim().split("\\s+")[0];
    try {
      return Optional.of(Architecture.parse(archToken));
    } catch (IllegalArgumentException e) {
      log.debug("Unsupported machine architecture output: {}", rawOutput);
      return Optional.empty();
    }
  }
}
