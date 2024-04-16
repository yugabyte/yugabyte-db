/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.models;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.fasterxml.jackson.annotation.JsonIgnore;
import com.fasterxml.jackson.annotation.JsonProperty;
import com.fasterxml.jackson.annotation.JsonPropertyOrder;
import com.typesafe.config.ConfigValue;
import com.typesafe.config.ConfigValueFactory;
import com.yugabyte.yw.common.HaConfigStates.GlobalState;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.ha.PlatformReplicationHelper;
import com.yugabyte.yw.common.inject.StaticInjectorHolder;
import io.ebean.Finder;
import io.ebean.Model;
import jakarta.persistence.CascadeType;
import jakarta.persistence.Column;
import jakarta.persistence.Entity;
import jakarta.persistence.Id;
import jakarta.persistence.OneToMany;
import jakarta.persistence.Temporal;
import jakarta.persistence.TemporalType;
import java.util.ArrayList;
import java.util.Base64;
import java.util.Date;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.UUID;
import java.util.stream.Collectors;
import javax.crypto.KeyGenerator;
import javax.crypto.SecretKey;
import lombok.Getter;
import lombok.Setter;
import lombok.extern.slf4j.Slf4j;
import play.data.validation.Constraints;

@Slf4j
@Entity
@JsonPropertyOrder({"uuid", "cluster_key", "instances"})
@Getter
@Setter
public class HighAvailabilityConfig extends Model {

  private static final Finder<UUID, HighAvailabilityConfig> find =
      new Finder<UUID, HighAvailabilityConfig>(HighAvailabilityConfig.class) {};

  private static long BACKUP_DISCONNECT_TIME_MILLIS = 15 * (60 * 1000);

  @JsonIgnore private final int id = 1;

  @Id
  @Constraints.Required
  @Column(nullable = false, unique = true)
  private UUID uuid;

  @Column(nullable = false, unique = true)
  @JsonProperty("cluster_key")
  private String clusterKey;

  @Temporal(TemporalType.TIMESTAMP)
  @JsonProperty("last_failover")
  private Date lastFailover;

  @OneToMany(mappedBy = "config", cascade = CascadeType.ALL)
  private List<PlatformInstance> instances;

  @Column(nullable = false, unique = false)
  @JsonProperty("accept_any_certificate")
  private boolean acceptAnyCertificate;

  public void updateLastFailover(Date lastFailover) {
    this.lastFailover = lastFailover;
    this.update();
  }

  public void updateLastFailover() {
    this.lastFailover = new Date();
    this.update();
  }

  public void updateAcceptAnyCertificate(boolean acceptAnyCertificate) {
    this.acceptAnyCertificate = acceptAnyCertificate;
    this.update();
  }

  @JsonIgnore
  public List<PlatformInstance> getRemoteInstances() {
    return this.instances.stream().filter(i -> !i.getIsLocal()).collect(Collectors.toList());
  }

  @JsonIgnore
  public boolean isLocalLeader() {
    return this.instances.stream().anyMatch(i -> i.getIsLeader() && i.getIsLocal());
  }

  @JsonIgnore
  public Optional<PlatformInstance> getLocal() {
    return this.instances.stream().filter(PlatformInstance::getIsLocal).findFirst();
  }

  @JsonIgnore
  public Optional<PlatformInstance> getLeader() {
    return this.instances.stream().filter(PlatformInstance::getIsLeader).findFirst();
  }

  public boolean getAcceptAnyCertificate() {
    return this.acceptAnyCertificate;
  }

  @JsonIgnore
  public Map<String, ConfigValue> getAcceptAnyCertificateOverrides() {
    return Map.of(
        PlatformReplicationHelper.WS_ACCEPT_ANY_CERTIFICATE_KEY,
        ConfigValueFactory.fromAnyRef(getAcceptAnyCertificate()));
  }

  public static HighAvailabilityConfig create(String clusterKey, boolean acceptAnyCertificate) {
    HighAvailabilityConfig model = new HighAvailabilityConfig();
    model.uuid = UUID.randomUUID();
    model.clusterKey = clusterKey;
    model.instances = new ArrayList<>();
    model.acceptAnyCertificate = acceptAnyCertificate;
    model.save();

    return model;
  }

  public static HighAvailabilityConfig create(String clusterKey) {
    return create(clusterKey, true);
  }

  public static void update(HighAvailabilityConfig config, String clusterKey) {
    update(config, clusterKey, config.getAcceptAnyCertificate());
  }

  public static void update(
      HighAvailabilityConfig config, String clusterKey, boolean acceptAnyCertificate) {
    config.setClusterKey(clusterKey);
    config.setAcceptAnyCertificate(acceptAnyCertificate);
    config.update();
  }

  @Deprecated
  public static Optional<HighAvailabilityConfig> get(UUID uuid) {
    return Optional.ofNullable(find.byId(uuid));
  }

  public static Optional<HighAvailabilityConfig> getOrBadRequest(UUID uuid) {
    Optional<HighAvailabilityConfig> config = get(uuid);
    if (!config.isPresent()) {
      throw new PlatformServiceException(BAD_REQUEST, "Invalid config UUID");
    }
    return config;
  }

  public static Optional<HighAvailabilityConfig> get() {
    return find.query().where().findOneOrEmpty();
  }

  public static Optional<HighAvailabilityConfig> getByClusterKey(String clusterKey) {
    return find.query().where().eq("cluster_key", clusterKey).findOneOrEmpty();
  }

  public static void delete(UUID uuid) {
    find.deleteById(uuid);
  }

  public static String generateClusterKey() throws Exception {
    KeyGenerator keyGen = KeyGenerator.getInstance("AES");
    keyGen.init(256);
    SecretKey secretKey = keyGen.generateKey();

    return Base64.getEncoder().encodeToString(secretKey.getEncoded());
  }

  public static boolean isFollower() {
    return get().flatMap(HighAvailabilityConfig::getLocal).map(i -> !i.getIsLeader()).orElse(false);
  }

  public boolean anyConnected() {
    return this.getRemoteInstances().stream().anyMatch(i -> i.isConnected());
  }

  public boolean anyDisconnected() {
    return this.getRemoteInstances().stream().anyMatch(i -> i.isDisconnected());
  }

  public GlobalState computeGlobalState() {
    if (this.isLocalLeader()) {
      if (this.instances.size() == 1) {
        return GlobalState.NoReplicas;
      } else if (this.getRemoteInstances().stream().allMatch(i -> i.isAwaitingReplicas())) {
        return GlobalState.AwaitingReplicas;
      } else if (this.anyDisconnected() && !this.anyConnected()) {
        return GlobalState.Error;
      } else if (this.anyDisconnected() && this.anyConnected()) {
        return GlobalState.Warning;
      } else if (this.anyConnected()) {
        return GlobalState.Operational;
      }
    } else if (this.isFollower()) {
      if (this.instances.size() == 1) {
        return GlobalState.AwaitingReplicas;
      } else if (this.getLocal().isPresent()) {
        if (this.getLocal().get().isConnected()) {
          return GlobalState.StandbyConnected;
        }
        RuntimeConfGetter runtimeConfGetter =
            StaticInjectorHolder.injector().instanceOf(RuntimeConfGetter.class);
        if (PlatformInstance.isBackupOutdated(
            runtimeConfGetter.getGlobalConf(GlobalConfKeys.replicationFrequency),
            this.getLocal().get().getLastBackup())) {
          return GlobalState.StandbyDisconnected;
        }
      }
    }
    log.warn("Could not compute global state, defaulting to Unknown.");
    return GlobalState.Unknown;
  }
}
