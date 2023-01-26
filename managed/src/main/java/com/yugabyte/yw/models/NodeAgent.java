// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.models;

import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_ONLY;
import static play.mvc.Http.Status.BAD_REQUEST;

import com.fasterxml.jackson.annotation.JsonIgnore;
import com.google.common.collect.ImmutableSet;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.certmgmt.CertificateHelper;
import io.ebean.ExpressionList;
import io.ebean.Finder;
import io.ebean.Model;
import io.ebean.annotation.DbJson;
import io.ebean.annotation.UpdatedTimestamp;
import io.swagger.annotations.ApiModelProperty;
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
import java.util.Map;
import java.util.Objects;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import javax.persistence.Column;
import javax.persistence.Entity;
import javax.persistence.EnumType;
import javax.persistence.Enumerated;
import javax.persistence.Id;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.io.FileUtils;
import org.apache.commons.lang3.EnumUtils;
import org.apache.commons.lang3.StringUtils;
import play.mvc.Http.Status;

@Slf4j
@Entity
public class NodeAgent extends Model {

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

  public static final Finder<UUID, NodeAgent> finder =
      new Finder<UUID, NodeAgent>(NodeAgent.class) {};

  public static final String CERT_DIR_PATH_PROPERTY = "certPath";
  public static final String SERVER_CERT_PROPERTY = "serverCert";
  public static final String SERVER_KEY_PROPERTY = "serverKey";
  public static final String ROOT_CA_CERT_NAME = "ca.root.crt";
  public static final String ROOT_CA_KEY_NAME = "ca.key.pem";
  public static final String SERVER_CERT_NAME = "server.crt";
  public static final String SERVER_KEY_NAME = "server.key";
  public static final String MERGED_ROOT_CA_CERT_NAME = "merged.ca.key.crt";

  @Id
  @ApiModelProperty(accessMode = READ_ONLY)
  public UUID uuid;

  @ApiModelProperty(accessMode = READ_ONLY)
  public String name;

  @ApiModelProperty(accessMode = READ_ONLY)
  public String ip;

  @ApiModelProperty(accessMode = READ_ONLY)
  public int port;

  @ApiModelProperty(accessMode = READ_ONLY)
  public UUID customerUuid;

  @ApiModelProperty(accessMode = READ_ONLY)
  public String version;

  @Enumerated(EnumType.STRING)
  @ApiModelProperty(accessMode = READ_ONLY)
  public State state;

  @UpdatedTimestamp
  @Column(nullable = false)
  @ApiModelProperty(value = "Updated time", accessMode = READ_ONLY, example = "1624295239113")
  public Date updatedAt;

  @ApiModelProperty(accessMode = READ_ONLY)
  @Column(nullable = false)
  @DbJson
  public Map<String, String> config;

  @Enumerated(EnumType.STRING)
  @ApiModelProperty(accessMode = READ_ONLY)
  public OSType osType;

  @Enumerated(EnumType.STRING)
  @ApiModelProperty(accessMode = READ_ONLY)
  public ArchType archType;

  public static Optional<NodeAgent> maybeGet(UUID uuid) {
    NodeAgent nodeAgent = finder.byId(uuid);
    if (nodeAgent == null) {
      log.trace("Cannot find node-agent {}", uuid);
      return Optional.empty();
    }
    return Optional.of(nodeAgent);
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

  public static Set<NodeAgent> getUpdatableNodeAgents(UUID customerUuid, String softwareVersion) {
    return finder
        .query()
        .where()
        .eq("customer_uuid", customerUuid)
        .ne("version", softwareVersion)
        .findSet();
  }

  public static void delete(UUID uuid) {
    finder.deleteById(uuid);
  }

  public void ensureState(State expectedState) {
    if (state != expectedState) {
      throw new PlatformServiceException(
          Status.CONFLICT,
          String.format(
              "Invalid current node agent state %s, expected state %s", state, expectedState));
    }
  }

  public void validateStateTransition(State nextState) {
    if (state == null) {
      throw new PlatformServiceException(Status.BAD_REQUEST, "Node agent state must be set");
    }
    state.validateTransition(nextState);
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
    validateStateTransition(state);
    this.state = state;
    save();
  }

  public void heartbeat() {
    Date current = new Date();
    if (db().update(NodeAgent.class).set("updatedAt", current).where().eq("uuid", uuid).update()
        > 0) {
      updatedAt = current;
    }
  }

  public void purge(Path certDir) {
    if (certDir != null) {
      try {
        File file = certDir.toFile();
        if (file.exists()) {
          FileUtils.deleteDirectory(file);
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
    String certDirPath = config.get(NodeAgent.CERT_DIR_PATH_PROPERTY);
    if (StringUtils.isBlank(certDirPath)) {
      throw new IllegalArgumentException(
          "Missing config key - " + NodeAgent.CERT_DIR_PATH_PROPERTY);
    }
    return Paths.get(certDirPath);
  }

  @JsonIgnore
  public Path getCaCertFilePath() {
    return getCertDirPath().resolve(NodeAgent.ROOT_CA_CERT_NAME);
  }

  @JsonIgnore
  public Path getMergedCaCertFilePath() {
    return getCertDirPath().resolve(NodeAgent.MERGED_ROOT_CA_CERT_NAME);
  }

  @JsonIgnore
  public Path getServerCertFilePath() {
    return getCertDirPath().resolve(NodeAgent.SERVER_CERT_NAME);
  }

  @JsonIgnore
  public Path getServerKeyFilePath() {
    return getCertDirPath().resolve(NodeAgent.SERVER_KEY_NAME);
  }

  public void updateCertDirPath(Path certDirPath) {
    config.put(NodeAgent.CERT_DIR_PATH_PROPERTY, certDirPath.toString());
    save();
  }
}
