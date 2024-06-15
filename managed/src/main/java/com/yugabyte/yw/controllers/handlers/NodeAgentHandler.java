// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.controllers.handlers;

import static com.yugabyte.yw.models.helpers.CommonUtils.performPagedQuery;

import com.google.common.annotations.VisibleForTesting;
import com.google.common.collect.ImmutableSet;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.commissioner.tasks.ReinstallNodeAgent;
import com.yugabyte.yw.common.NodeAgentClient;
import com.yugabyte.yw.common.NodeAgentManager;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.utils.FileUtils;
import com.yugabyte.yw.forms.NodeAgentForm;
import com.yugabyte.yw.forms.NodeAgentResp;
import com.yugabyte.yw.forms.ReinstallNodeAgentForm;
import com.yugabyte.yw.forms.paging.NodeAgentPagedApiResponse;
import com.yugabyte.yw.models.NodeAgent;
import com.yugabyte.yw.models.NodeAgent.ArchType;
import com.yugabyte.yw.models.NodeAgent.OSType;
import com.yugabyte.yw.models.NodeAgent.State;
import com.yugabyte.yw.models.filters.NodeAgentFilter;
import com.yugabyte.yw.models.helpers.TaskType;
import com.yugabyte.yw.models.paging.NodeAgentPagedQuery;
import com.yugabyte.yw.models.paging.NodeAgentPagedResponse;
import com.yugabyte.yw.models.paging.PagedQuery.SortDirection;
import io.ebean.Query;
import java.io.ByteArrayInputStream;
import java.io.InputStream;
import java.nio.file.Path;
import java.time.Instant;
import java.util.Arrays;
import java.util.Collection;
import java.util.Date;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;
import java.util.function.Function;
import java.util.stream.Collectors;
import javax.inject.Inject;
import javax.inject.Singleton;
import lombok.AllArgsConstructor;
import lombok.Getter;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.EnumUtils;
import org.apache.commons.lang3.StringUtils;
import org.threeten.bp.Duration;
import play.mvc.Http;
import play.mvc.Http.Status;

@Slf4j
@Singleton
public class NodeAgentHandler {
  private static final String NODE_AGENT_INSTALLER_FILE = "node-agent-installer.sh";
  private static final Duration NODE_AGENT_HEARTBEAT_TIMEOUT = Duration.ofMinutes(5);

  private final Commissioner commissioner;
  private final NodeAgentManager nodeAgentManager;
  private final NodeAgentClient nodeAgentClient;
  private boolean validateConnection = true;

  @Inject
  public NodeAgentHandler(
      Commissioner commissioner,
      NodeAgentManager nodeAgentManager,
      NodeAgentClient nodeAgentClient) {
    this.commissioner = commissioner;
    this.nodeAgentManager = nodeAgentManager;
    this.nodeAgentClient = nodeAgentClient;
  }

  @AllArgsConstructor
  public static class NodeAgentDownloadFile {
    @Getter String ContentType;
    @Getter InputStream Content;
    @Getter String FileName;
  }

  private enum DownloadType {
    INSTALLER,
    PACKAGE;
  }

  @VisibleForTesting
  public void enableConnectionValidation(boolean enable) {
    validateConnection = enable;
  }

  /**
   * Registers the node agent to platform to set up the authentication keys.
   *
   * @param nodeAgent Partially populated node agent.
   * @return the fully populated node agent.
   */
  public NodeAgent register(UUID customerUuid, NodeAgentForm payload) {
    Optional<NodeAgent> nodeAgentOp = NodeAgent.maybeGetByIp(payload.ip);
    if (nodeAgentOp.isPresent()) {
      log.error("Node {} is already registered with {}", payload.ip, nodeAgentOp.get().getUuid());
      throw new PlatformServiceException(Status.BAD_REQUEST, "Node agent is already registered");
    }
    if (StringUtils.isBlank(payload.version)) {
      throw new PlatformServiceException(
          Status.BAD_REQUEST, "Node agent version must be specified");
    }
    NodeAgent nodeAgent = payload.toNodeAgent(customerUuid);
    if (validateConnection) {
      try {
        nodeAgentClient.ping(nodeAgent, false);
      } catch (RuntimeException e) {
        String msg =
            String.format("Failed to ping node agent %s. Error: %s", payload.ip, e.getMessage());
        log.error(msg, e);
        throw new PlatformServiceException(Status.BAD_REQUEST, msg);
      }
    }
    return nodeAgentManager.create(nodeAgent, true);
  }

  private List<NodeAgentResp> transformNodeAgentResponse(
      List<Map<String, Object>> joinResults, Collection<NodeAgent> nodeAgents) {
    Date startTime =
        Date.from(Instant.now().minusSeconds(NODE_AGENT_HEARTBEAT_TIMEOUT.getSeconds()));
    String ybaVersion = nodeAgentManager.getSoftwareVersion();
    Map<UUID, NodeAgent> uuidNodeAgentMap =
        nodeAgents.stream().collect(Collectors.toMap(NodeAgent::getUuid, Function.identity()));
    return joinResults.stream()
        .map(
            r -> {
              NodeAgent nodeAgent = uuidNodeAgentMap.get(r.get("uuid"));
              NodeAgentResp nodeAgentResp = new NodeAgentResp(nodeAgent);
              nodeAgentResp.setReachable(nodeAgent.getUpdatedAt().after(startTime));
              nodeAgentResp.setVersionMatched(
                  Util.areYbVersionsEqual(ybaVersion, nodeAgent.getVersion(), true));
              Object obj = r.get("provider_name");
              if (obj != null) {
                nodeAgentResp.setProviderName((String) obj);
              }
              obj = r.get("provider_uuid");
              if (obj != null) {
                nodeAgentResp.setProviderUuid((UUID) obj);
              }
              obj = r.get("universe_name");
              if (obj != null) {
                nodeAgentResp.setUniverseName((String) obj);
              }
              obj = r.get("universe_uuid");
              if (obj != null) {
                nodeAgentResp.setUniverseUuid((UUID) obj);
              }
              return nodeAgentResp;
            })
        .collect(Collectors.toList());
  }

  /**
   * Returns the node agents for the customer with additional node agent IP filter.
   *
   * @param customerUuid customer UUID.
   * @param nodeAgentIp optional node agent IP.
   * @return the node agent.
   */
  public Collection<NodeAgentResp> list(UUID customerUuid, String nodeAgentIp) {
    NodeAgentFilter.NodeAgentFilterBuilder builder = NodeAgentFilter.builder();
    if (StringUtils.isNotEmpty(nodeAgentIp)) {
      builder.nodeIps(ImmutableSet.of(nodeAgentIp)).build();
    }
    List<Map<String, Object>> joinResults = NodeAgent.getJoinResults(customerUuid, builder.build());
    return transformNodeAgentResponse(joinResults, NodeAgent.list(customerUuid, nodeAgentIp));
  }

  /**
   * Returns a page of node agents for the customer with additional node agent IP filter.
   *
   * @param customerUuid the customer UUID.
   * @param pagedQuery the page query with filter and page param.
   * @return a page of node agents.
   */
  public NodeAgentPagedApiResponse pagedList(UUID customerUuid, NodeAgentPagedQuery pagedQuery) {
    List<Map<String, Object>> joinResults =
        NodeAgent.getJoinResults(customerUuid, pagedQuery.getFilter());
    if (pagedQuery.getSortBy() == null) {
      pagedQuery.setSortBy(NodeAgent.SortBy.ip);
      pagedQuery.setDirection(SortDirection.DESC);
    }
    Set<UUID> nodeAgentUuids =
        joinResults.stream().map(m -> (UUID) m.get("uuid")).collect(Collectors.toSet());
    Query<NodeAgent> query = NodeAgent.createQuery(customerUuid, nodeAgentUuids).query();
    NodeAgentPagedResponse response =
        performPagedQuery(query, pagedQuery, NodeAgentPagedResponse.class);
    return response.setData(
        transformNodeAgentResponse(joinResults, response.getEntities()),
        new NodeAgentPagedApiResponse());
  }

  /**
   * Returns the node agent with the given IDs.
   *
   * @param customerUuid customer UUID.
   * @param nodeAgentUuid node agent UUID.
   * @return the node agent.
   */
  public NodeAgentResp get(UUID customerUuid, UUID nodeAgentUuid) {
    NodeAgent nodeAgent = NodeAgent.getOrBadRequest(customerUuid, nodeAgentUuid);
    List<Map<String, Object>> joinResults =
        NodeAgent.getJoinResults(
            customerUuid,
            NodeAgentFilter.builder().nodeIps(ImmutableSet.of(nodeAgent.getIp())).build());
    return transformNodeAgentResponse(joinResults, Arrays.asList(nodeAgent)).get(0);
  }

  /**
   * Updates the current state of the node agent.
   *
   * @param customerUuid customer UUID.
   * @param nodeAgentUuid node agent UUID.
   * @param payload request payload.
   * @return the node agent.
   */
  public NodeAgent updateState(UUID customerUuid, UUID nodeAgentUuid, NodeAgentForm payload) {
    State state = State.parse(payload.state);
    if (state != State.READY) {
      throw new PlatformServiceException(
          Status.BAD_REQUEST, "Invalid node agent state " + payload.state);
    }
    NodeAgent nodeAgent = NodeAgent.getOrBadRequest(customerUuid, nodeAgentUuid);
    nodeAgent.saveState(state);
    return nodeAgent;
  }

  /**
   * Unregisters the node agent from platform.
   *
   * @param uuid the node UUID.
   */
  public void unregister(UUID uuid) {
    NodeAgent.maybeGet(uuid).ifPresent(n -> nodeAgentManager.purge(n));
  }

  @VisibleForTesting
  void validateDownloadType(DownloadType downloadType, OSType osType, ArchType archType) {
    if (downloadType == null) {
      throw new PlatformServiceException(
          Http.Status.BAD_REQUEST, "Incorrect download step provided");
    }
    if (downloadType == DownloadType.PACKAGE && (osType == null || archType == null)) {
      throw new PlatformServiceException(
          Http.Status.BAD_REQUEST, "Incorrect OS or Arch passed for package download step");
    }
  }

  /**
   * Validates the request type and returns the node agent download file.
   *
   * @param type download type, os type, arch type.
   * @return the Node Agent download file (installer or build package).
   */
  public NodeAgentDownloadFile validateAndGetDownloadFile(String type, String os, String arch) {
    DownloadType downloadType =
        StringUtils.isBlank(type)
            ? DownloadType.INSTALLER
            : EnumUtils.getEnumIgnoreCase(DownloadType.class, type);
    OSType osType = EnumUtils.getEnumIgnoreCase(OSType.class, os);
    ArchType archType = EnumUtils.getEnumIgnoreCase(ArchType.class, arch);
    validateDownloadType(downloadType, osType, archType);
    if (downloadType == DownloadType.PACKAGE) {
      Path packagePath = nodeAgentManager.getNodeAgentPackagePath(osType, archType);
      return new NodeAgentDownloadFile(
          "application/gzip",
          FileUtils.getInputStreamOrFail(packagePath.toFile()),
          packagePath.getFileName().toString());
    }
    byte[] contents = nodeAgentManager.getInstallerScript();
    return new NodeAgentDownloadFile(
        "application/x-sh", new ByteArrayInputStream(contents), NODE_AGENT_INSTALLER_FILE);
  }

  public UUID reinstall(UUID customerUuid, UUID universeUuid, ReinstallNodeAgentForm payload) {
    ReinstallNodeAgent.Params taskParams = new ReinstallNodeAgent.Params();
    taskParams.setUniverseUUID(universeUuid);
    taskParams.nodeNames = payload.nodeNames;
    return commissioner.submit(TaskType.ReinstallNodeAgent, taskParams);
  }
}
