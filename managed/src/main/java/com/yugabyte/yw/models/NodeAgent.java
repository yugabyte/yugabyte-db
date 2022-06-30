// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.models;

import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_ONLY;

import com.fasterxml.jackson.annotation.JsonIgnore;
import io.ebean.Finder;
import io.ebean.Model;
import io.ebean.annotation.DbJson;
import io.ebean.annotation.UpdatedTimestamp;
import io.swagger.annotations.ApiModelProperty;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Paths;
import java.util.Date;
import java.util.Map;
import java.util.Objects;
import java.util.Optional;
import java.util.UUID;
import javax.persistence.Column;
import javax.persistence.Entity;
import javax.persistence.Id;
import lombok.extern.slf4j.Slf4j;
import play.data.validation.Constraints;

@Slf4j
@Entity
public class NodeAgent extends Model {

  public static final Finder<UUID, NodeAgent> finder =
      new Finder<UUID, NodeAgent>(NodeAgent.class) {};

  public static final String ROOT_CA_CERT_PATH_PROPERTY = "rootCaCert";
  public static final String ROOT_CA_KEY_PATH_PROPERTY = "rootCaKey";
  public static final String SERVER_CERT_PATH_PROPERTY = "serverCertPath";
  public static final String SERVER_KEY_PATH_PROPERTY = "serverKeyPath";
  public static final String SERVER_CERT_PROPERTY = "serverCert";
  public static final String SERVER_KEY_PROPERTY = "serverKey";

  @Id
  @ApiModelProperty(accessMode = READ_ONLY)
  public UUID uuid;

  @Constraints.Required() public String name;

  @Constraints.Required() public String ip;

  @Constraints.Required() public UUID customerUuid;

  @ApiModelProperty(required = true)
  @Constraints.Required()
  public String version;

  @UpdatedTimestamp
  @Column(nullable = false)
  @ApiModelProperty(value = "Updated time", accessMode = READ_ONLY, example = "1624295239113")
  public Date updatedAt;

  @ApiModelProperty(required = true)
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
    return finder.query().where().eq("customer_uuid", customerUuid).idEq(nodeAgentUuid).findOne();
  }

  public static void delete(UUID uuid) {
    finder.deleteById(uuid);
  }

  @JsonIgnore
  public byte[] getServerCert() {
    String serverCertPath = config.get(SERVER_CERT_PATH_PROPERTY);
    Objects.requireNonNull(serverCertPath, "Server cert must exist");
    try {
      return Files.readAllBytes(Paths.get(serverCertPath));
    } catch (IOException e) {
      throw new RuntimeException(e);
    }
  }

  @JsonIgnore
  public byte[] getServerKey() {
    String serverKeyPath = config.get(SERVER_KEY_PATH_PROPERTY);
    Objects.requireNonNull(serverKeyPath, "Server key must exist");
    try {
      return Files.readAllBytes(Paths.get(serverKeyPath));
    } catch (IOException e) {
      throw new RuntimeException(e);
    }
  }

  public void saveConfig(Map<String, String> config) {
    this.config = config;
    save();
  }
}
