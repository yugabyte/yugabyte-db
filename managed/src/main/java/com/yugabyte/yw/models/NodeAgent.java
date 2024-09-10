// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.models;

import static com.yugabyte.yw.models.helpers.CommonUtils.appendInClause;
import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_ONLY;
import static play.mvc.Http.Status.BAD_REQUEST;

import com.fasterxml.jackson.annotation.JsonFormat;
import com.fasterxml.jackson.annotation.JsonIgnore;
import com.google.common.annotations.VisibleForTesting;
import com.google.common.collect.ImmutableSet;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.certmgmt.CertificateHelper;
import com.yugabyte.yw.common.concurrent.KeyLock;
import com.yugabyte.yw.models.filters.NodeAgentFilter;
import com.yugabyte.yw.models.helpers.TransactionUtil;
import com.yugabyte.yw.models.paging.PagedQuery;
import com.yugabyte.yw.models.paging.PagedQuery.SortByIF;
import io.ebean.DB;
import io.ebean.ExpressionList;
import io.ebean.Finder;
import io.ebean.Model;
import io.ebean.PersistenceContextScope;
import io.ebean.annotation.DbJson;
import io.ebean.annotation.WhenModified;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import jakarta.persistence.Column;
import jakarta.persistence.Entity;
import jakarta.persistence.EnumType;
import jakarta.persistence.Enumerated;
import jakarta.persistence.Id;
import java.io.ByteArrayInputStream;
import java.io.File;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.security.PrivateKey;
import java.security.PublicKey;
import java.security.cert.CertificateFactory;
import java.security.cert.X509Certificate;
import java.util.Arrays;
import java.util.Collection;
import java.util.Collections;
import java.util.Date;
import java.util.EnumSet;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;
import java.util.function.Consumer;
import java.util.stream.Collectors;
import lombok.AccessLevel;
import lombok.Getter;
import lombok.Setter;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections4.CollectionUtils;
import org.apache.commons.lang3.EnumUtils;
import org.apache.commons.lang3.StringUtils;
import play.mvc.Http.Status;

@Slf4j
@Entity
@Getter
@Setter
@ApiModel(description = "Node agent details")
public class NodeAgent extends Model {

  public static final KeyLock<UUID> NODE_AGENT_KEY_LOCK = new KeyLock<UUID>();
  public static final String NODE_AGENT_DIR = "node-agent";

  /** Node agent server OS type. */
  public enum OSType {
    DARWIN,
    LINUX;

    public static OSType parse(String strType) {
      OSType osType = EnumUtils.getEnumIgnoreCase(OSType.class, strType);
      if (osType == null) {
        throw new IllegalArgumentException("Unknown OS type: " + strType);
      }
      return osType;
    }
  }

  /** Node agent server arch type. */
  public enum ArchType {
    ARM64("aarch64"),
    AMD64("x86_64");

    private final Set<String> aliases;

    private ArchType(String... aliases) {
      this.aliases =
          aliases == null
              ? Collections.emptySet()
              : Collections.unmodifiableSet(
                  Arrays.stream(aliases).map(String::toLowerCase).collect(Collectors.toSet()));
    }

    public static ArchType parse(String strType) {
      String lower = strType.toLowerCase();
      for (ArchType archType : EnumSet.allOf(ArchType.class)) {
        if (archType.name().equalsIgnoreCase(lower) || archType.aliases.contains(lower)) {
          return archType;
        }
      }
      throw new IllegalArgumentException("Unknown arch type: " + strType);
    }
  }

  /** State and the transitions. */
  public enum State {
    REGISTERING {
      @Override
      public Set<State> nextStates() {
        return toSet(READY, REGISTERED);
      }
    },
    REGISTERED {
      @Override
      public Set<State> nextStates() {
        return toSet(READY);
      }
    },
    UPGRADE {
      @Override
      public Set<State> nextStates() {
        return toSet(UPGRADED);
      }
    },
    UPGRADED {
      @Override
      public Set<State> nextStates() {
        return toSet(READY);
      }
    },
    READY {
      @Override
      public Set<State> nextStates() {
        return toSet(READY, UPGRADE);
      }
    };

    public abstract Set<State> nextStates();

    public static State parse(String strType) {
      State state = EnumUtils.getEnumIgnoreCase(State.class, strType);
      if (state == null) {
        throw new IllegalArgumentException("Unknown state: " + state);
      }
      return state;
    }

    private static Set<State> toSet(State... states) {
      return states == null
          ? Collections.emptySet()
          : ImmutableSet.<State>builder().add(states).build();
    }

    public void validateTransition(State nextState) {
      if (!this.nextStates().contains(nextState)) {
        throw new IllegalStateException(
            String.format("Invalid state transition from %s to %s", name(), nextState.name()));
      }
    }
  }

  @Getter
  @Setter
  public static class Config {
    private String certPath;
    private String serverCert;
    private String serverKey;
    private boolean offloadable;
  }

  public static final Finder<UUID, NodeAgent> finder =
      new Finder<UUID, NodeAgent>(NodeAgent.class) {};

  public static final String ROOT_CA_CERT_NAME = "ca.root.crt";
  public static final String ROOT_CA_KEY_NAME = "ca.key.pem";
  public static final String SERVER_CERT_NAME = "server.crt";
  public static final String SERVER_KEY_NAME = "server.key";
  public static final String MERGED_ROOT_CA_CERT_NAME = "merged.ca.key.crt";

  @Id
  @ApiModelProperty(value = "Node agent UUID", accessMode = READ_ONLY)
  private UUID uuid;

  @ApiModelProperty(value = "Node agent name", accessMode = READ_ONLY)
  private String name;

  @ApiModelProperty(value = "Node agent server IP", accessMode = READ_ONLY)
  private String ip;

  @ApiModelProperty(value = "Node agent server port", accessMode = READ_ONLY)
  private int port;

  @ApiModelProperty(value = "Customer UUID", accessMode = READ_ONLY)
  private UUID customerUuid;

  @ApiModelProperty(value = "Node agent installed version", accessMode = READ_ONLY)
  private String version;

  @Enumerated(EnumType.STRING)
  @ApiModelProperty(value = "Node agent state", accessMode = READ_ONLY)
  @Setter(AccessLevel.NONE)
  private State state;

  @WhenModified
  @Column(nullable = false)
  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd'T'HH:mm:ss'Z'")
  @ApiModelProperty(
      value = "Updated time",
      accessMode = READ_ONLY,
      example = "2022-12-21T13:07:18Z")
  private Date updatedAt;

  @ApiModelProperty(value = "Node agent configuration", accessMode = READ_ONLY)
  @Column(nullable = false)
  @DbJson
  private Config config;

  @Enumerated(EnumType.STRING)
  @ApiModelProperty(value = "Node agent host OS", accessMode = READ_ONLY)
  private OSType osType;

  @Enumerated(EnumType.STRING)
  @ApiModelProperty(value = "Node agent host machine arch", accessMode = READ_ONLY)
  private ArchType archType;

  @ApiModelProperty(value = "Node agent installation directory", accessMode = READ_ONLY)
  @Column(nullable = false)
  private String home;

  public enum SortBy implements PagedQuery.SortByIF {
    uuid("uuid"),
    ip("ip"),
    name("name"),
    state("state"),
    updatedAt("updatedAt"),
    version("version");

    private final String sortField;

    SortBy(String sortField) {
      this.sortField = sortField;
    }

    public String getSortField() {
      return sortField;
    }

    @Override
    public SortByIF getOrderField() {
      return SortBy.uuid;
    }
  }

  public void setState(State state) {
    if (this.state != null) {
      this.state.validateTransition(state);
    }
    this.state = state;
  }

  public static Optional<NodeAgent> maybeGet(UUID uuid) {
    return Optional.ofNullable(finder.byId(uuid));
  }

  public static Optional<NodeAgent> maybeGetByIp(String ip) {
    return finder.query().where().eq("ip", ip).findOneOrEmpty();
  }

  public static NodeAgent getOrBadRequest(UUID customerUuid, UUID nodeAgentUuid) {
    NodeAgent nodeAgent =
        finder.query().where().eq("customer_uuid", customerUuid).idEq(nodeAgentUuid).findOne();
    if (nodeAgent == null) {
      throw new PlatformServiceException(BAD_REQUEST, "Cannot find node agent " + nodeAgentUuid);
    }
    return nodeAgent;
  }

  public static NodeAgent getOrBadRequest(UUID nodeAgentUuid) {
    return NodeAgent.maybeGet(nodeAgentUuid)
        .orElseThrow(
            () ->
                new PlatformServiceException(
                    BAD_REQUEST, "Cannot find node agent " + nodeAgentUuid));
  }

  public static Collection<NodeAgent> list(UUID customerUuid, String nodeAgentIp /* Optional */) {
    ExpressionList<NodeAgent> expr = finder.query().where().eq("customer_uuid", customerUuid);
    if (StringUtils.isNotBlank(nodeAgentIp)) {
      expr = expr.eq("ip", nodeAgentIp);
    }
    return expr.findList();
  }

  public static Set<NodeAgent> getNodeAgents(UUID customerUuid) {
    return finder.query().where().eq("customer_uuid", customerUuid).findSet();
  }

  public static Set<NodeAgent> getAll() {
    return finder.query().findSet();
  }

  public static List<NodeAgent> getByIps(UUID customerUuid, Set<String> ips) {
    ExpressionList<NodeAgent> query = finder.query().where().eq("customerUuid", customerUuid);
    appendInClause(query, "ip", ips);
    return query.findList();
  }

  public static Set<NodeAgent> getUpdatableNodeAgents(UUID customerUuid, String softwareVersion) {
    return finder
        .query()
        .where()
        .eq("customer_uuid", customerUuid)
        .ne("version", softwareVersion)
        .findSet();
  }

  public static ExpressionList<NodeAgent> createQuery(UUID customerUuid, Set<UUID> nodeAgentUuids) {
    ExpressionList<NodeAgent> query =
        finder
            .query()
            .setPersistenceContextScope(PersistenceContextScope.QUERY)
            .where()
            .eq("customer_uuid", customerUuid);
    if (CollectionUtils.isEmpty(nodeAgentUuids)) {
      query = query.isNull("uuid");
    } else {
      appendInClause(query, "uuid", nodeAgentUuids);
    }
    return query;
  }

  public static void delete(UUID uuid) {
    finder.deleteById(uuid);
  }

  private void updateInTxn(Consumer<NodeAgent> consumer) {
    NODE_AGENT_KEY_LOCK.acquireLock(getUuid());
    try {
      TransactionUtil.doInTxn(
          () -> consumer.accept(NodeAgent.getOrBadRequest(getUuid())),
          TransactionUtil.DEFAULT_RETRY_CONFIG);
      // Reload the record from the DB.
      refresh();
    } finally {
      NODE_AGENT_KEY_LOCK.releaseLock(getUuid());
    }
  }

  public void ensureState(State expectedState) {
    if (getState() != expectedState) {
      throw new PlatformServiceException(
          Status.CONFLICT,
          String.format(
              "Invalid current node agent state %s, expected state %s", getState(), expectedState));
    }
  }

  public void validateStateTransition(State nextState) {
    if (getState() == null) {
      throw new PlatformServiceException(Status.BAD_REQUEST, "Node agent state must be set");
    }
    getState().validateTransition(nextState);
  }

  @JsonIgnore
  public byte[] getServerCert() {
    Path serverCertPath = getCertDirPath().resolve(SERVER_CERT_NAME);
    Objects.requireNonNull(serverCertPath, "Server cert must exist");
    try {
      return Files.readAllBytes(serverCertPath);
    } catch (IOException e) {
      throw new RuntimeException(e);
    }
  }

  @JsonIgnore
  public byte[] getServerKey() {
    Path serverKeyPath = getCertDirPath().resolve(SERVER_KEY_NAME);
    Objects.requireNonNull(serverKeyPath, "Server key must exist");
    try {
      return Files.readAllBytes(serverKeyPath);
    } catch (IOException e) {
      throw new RuntimeException(e);
    }
  }

  public void saveState(State state) {
    updateInTxn(
        n -> {
          n.setState(state);
          n.update();
        });
  }

  public void finalizeUpgrade(String nodeAgentHome, String version) {
    updateInTxn(
        n -> {
          n.setHome(nodeAgentHome);
          n.setVersion(version);
          n.setState(State.READY);
          n.update();
        });
  }

  public void heartbeat() {
    updateTimestamp(new Date());
  }

  @VisibleForTesting
  public void updateTimestamp(Date date) {
    if (db().update(NodeAgent.class).set("updatedAt", date).where().eq("uuid", getUuid()).update()
        > 0) {
      setUpdatedAt(date);
    }
  }

  public void purge(Path certDir) {
    if (certDir != null) {
      try {
        File file = certDir.toFile();
        if (file.exists()) {
          FileData.deleteFiles(file.getAbsolutePath(), true);
        }
      } catch (Exception e) {
        log.warn("Error deleting cert directory {}", certDir, e);
      }
    }
    delete();
  }

  @JsonIgnore
  public PrivateKey getPrivateKey() {
    return CertificateHelper.getPrivateKey(new String(getServerKey()));
  }

  @JsonIgnore
  public PublicKey getPublicKey() {
    try {
      CertificateFactory factory = CertificateFactory.getInstance("X.509");
      X509Certificate cert =
          (X509Certificate) factory.generateCertificate(new ByteArrayInputStream(getServerCert()));
      return cert.getPublicKey();
    } catch (RuntimeException e) {
      throw e;
    } catch (Exception e) {
      throw new RuntimeException(e.getMessage(), e);
    }
  }

  @JsonIgnore
  public Path getCertDirPath() {
    String certDirPath = getConfig().getCertPath();
    if (StringUtils.isBlank(certDirPath)) {
      throw new IllegalArgumentException("Missing cert path");
    }
    return Paths.get(certDirPath);
  }

  @JsonIgnore
  public Path getCaCertFilePath() {
    return getCertDirPath().resolve(ROOT_CA_CERT_NAME);
  }

  @JsonIgnore
  public Path getMergedCaCertFilePath() {
    return getCertDirPath().resolve(MERGED_ROOT_CA_CERT_NAME);
  }

  @JsonIgnore
  public Path getServerCertFilePath() {
    return getCertDirPath().resolve(SERVER_CERT_NAME);
  }

  @JsonIgnore
  public Path getServerKeyFilePath() {
    return getCertDirPath().resolve(SERVER_KEY_NAME);
  }

  public void updateCertDirPath(Path certDirPath) {
    updateCertDirPath(certDirPath, null);
  }

  public void updateCertDirPath(Path certDirPath, State state) {
    updateInTxn(
        n -> {
          if (state != null) {
            n.setState(state);
          }
          n.getConfig().setCertPath(certDirPath.toString());
          n.update();
        });
  }

  public void updateOffloadable(boolean offloadable) {
    if (getConfig().isOffloadable() != offloadable) {
      updateInTxn(
          n -> {
            n.getConfig().setOffloadable(offloadable);
            n.update();
          });
    }
  }

  @JsonIgnore
  public static List<Map<String, Object>> getJoinResults(
      UUID customerUuid, NodeAgentFilter filter) {
    StringBuilder sb = new StringBuilder();
    sb.append("select node_agents.uuid as uuid, universe_uuid, universe_name, provider_uuid,");
    sb.append(" provider_name, provider_code, region_uuid, region_code, az_uuid, az_code");
    // Left join node_agents with left join of provider and full join of universe and node_instance.
    // Node agents can exist without any provider (not yet added).
    sb.append(" from (select * from node_agent) as node_agents left join");
    sb.append(" (select p.uuid as provider_uuid, p.name as provider_name,");
    sb.append(" p.code as provider_code, r.uuid as region_uuid, r.code as region_code,");
    sb.append(" az.uuid as az_uuid, az.code as az_code");
    sb.append(" from provider p, region r, availability_zone az");
    sb.append(" where az.region_uuid = r.uuid and r.provider_uuid = p.uuid) as providers");
    sb.append(" left join ((select universe_uuid, universe_name,");
    sb.append(" (node_details->>'azUuid')::uuid as universe_az_uuid,");
    sb.append(" node_details->'cloudInfo'->>'private_ip' as universe_node_private_ip");
    sb.append(" from (select universe_uuid as universe_uuid, universe.name as universe_name,");
    sb.append(" jsonb_array_elements(universe_details_json::jsonb->'nodeDetailsSet')");
    sb.append(" as node_details from universe) as tmp) as universe_nodes");
    sb.append(" full join (select zone_uuid, node_details_json::jsonb->>'ip'");
    sb.append(" as node_instance_ip from node_instance) as node_instances");
    sb.append(" on node_instance_ip = universe_node_private_ip) as node_instances");
    sb.append(" on az_uuid = universe_az_uuid or az_uuid = zone_uuid");
    sb.append(" on node_agents.ip = universe_node_private_ip or node_agents.ip = node_instance_ip");
    sb.append(" where node_agents.customer_uuid = '").append(customerUuid.toString()).append("'");
    if (CollectionUtils.isNotEmpty(filter.getNodeIps())) {
      sb.append(" and node_agents.ip in ('").append(StringUtils.join(filter.getNodeIps(), "','"));
      sb.append("')");
    }
    if (filter.getCloudType() != null) {
      sb.append(" and provider_code = '").append(filter.getCloudType().name()).append("'");
    }
    if (filter.getProviderUuid() != null) {
      sb.append(" and provider_uuid = '").append(filter.getProviderUuid().toString()).append("'");
    }

    if (filter.getRegionUuid() != null) {
      sb.append(" and region_uuid = '").append(filter.getRegionUuid().toString()).append("'");
    }

    if (filter.getZoneUuid() != null) {
      sb.append(" and az_uuid = '").append(filter.getZoneUuid().toString()).append("'");
    }
    if (filter.getUniverseUuid() != null) {
      sb.append(" and universe_uuid = '").append(filter.getUniverseUuid().toString()).append("'");
    }
    String nodeAgentQuery = sb.toString();
    log.trace("Running SQL query: {}", nodeAgentQuery);
    return DB.sqlQuery(nodeAgentQuery).findList().stream()
        .map(
            rs ->
                rs.entrySet().stream()
                    .filter(e -> e.getValue() != null)
                    .collect(
                        Collectors.toMap(Map.Entry::getKey, Map.Entry::getValue, (e1, e2) -> e2)))
        .collect(Collectors.toList());
  }
}
