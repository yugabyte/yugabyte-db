// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.models;

import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_ONLY;
import static play.mvc.Http.Status.BAD_REQUEST;

import com.fasterxml.jackson.annotation.JsonIgnore;
import com.google.common.collect.ImmutableSet;
import com.yugabyte.yw.common.PlatformServiceException;
import io.ebean.Finder;
import io.ebean.Model;
import io.ebean.annotation.DbJson;
import io.ebean.annotation.UpdatedTimestamp;
import io.swagger.annotations.ApiModelProperty;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.Collections;
import java.util.Date;
import java.util.Map;
import java.util.Objects;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;
import javax.persistence.Column;
import javax.persistence.Entity;
import javax.persistence.EnumType;
import javax.persistence.Enumerated;
import javax.persistence.Id;
import lombok.extern.slf4j.Slf4j;
import play.mvc.Http.Status;

@Slf4j
@Entity
public class NodeAgent extends Model {

  /** State and the transitions. */
  public enum State {
    REGISTERING {
      @Override
      public Set<State> nextStates() {
        return toSet(REGISTERING, LIVE);
      }
    },
    UPGRADE {
      @Override
      public Set<State> nextStates() {
        return toSet(UPGRADING);
      }
    },
    UPGRADING {
      @Override
      public Set<State> nextStates() {
        return toSet(UPGRADING, UPGRADED);
      }
    },
    UPGRADED {
      @Override
      public Set<State> nextStates() {
        return toSet(LIVE);
      }
    },
    LIVE {
      @Override
      public Set<State> nextStates() {
        return toSet(LIVE, UPGRADE);
      }
    };

    public abstract Set<State> nextStates();

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

  public static Set<NodeAgent> getNodeAgents(UUID customerUuid) {
    return finder.query().where().eq("customer_uuid", customerUuid).findSet();
  }

  public static Set<NodeAgent> getUpdatableNodeAgents(UUID customerUuid, String softwareVersion) {
    return finder
        .query()
        .where()
        .eq("customer_uuid", customerUuid)
        .eq("state", State.LIVE)
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
    Path serverCertPath = getFilePath(SERVER_CERT_NAME);
    Objects.requireNonNull(serverCertPath, "Server cert must exist");
    try {
      return Files.readAllBytes(serverCertPath);
    } catch (IOException e) {
      throw new RuntimeException(e);
    }
  }

  @JsonIgnore
  public byte[] getServerKey() {
    Path serverKeyPath = getFilePath(SERVER_KEY_NAME);
    Objects.requireNonNull(serverKeyPath, "Server key must exist");
    try {
      return Files.readAllBytes(serverKeyPath);
    } catch (IOException e) {
      throw new RuntimeException(e);
    }
  }

  @JsonIgnore
  public Path getFilePath(String fileName) {
    String dirPath = config.get(CERT_DIR_PATH_PROPERTY);
    Objects.requireNonNull(dirPath, "Cert directory must exist");
    return Paths.get(dirPath, fileName);
  }

  public void saveConfig(Map<String, String> config) {
    this.config = config;
    save();
  }

  public void saveState(State state) {
    this.state = state;
    save();
  }
}
