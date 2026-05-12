/*
 * Copyright 2021 YugabyteDB, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.models;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.fasterxml.jackson.annotation.JsonFormat;
import com.fasterxml.jackson.annotation.JsonGetter;
import com.fasterxml.jackson.annotation.JsonIgnore;
import com.fasterxml.jackson.annotation.JsonProperty;
import com.fasterxml.jackson.annotation.JsonPropertyOrder;
import com.fasterxml.jackson.annotation.JsonSetter;
import com.google.common.collect.ImmutableSet;
import com.yugabyte.yw.common.HaConfigStates.InstanceState;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.inject.StaticInjectorHolder;
import io.ebean.Finder;
import io.ebean.Model;
import io.swagger.annotations.ApiModelProperty;
import jakarta.persistence.Column;
import jakarta.persistence.Entity;
import jakarta.persistence.EnumType;
import jakarta.persistence.Enumerated;
import jakarta.persistence.Id;
import jakarta.persistence.ManyToOne;
import jakarta.persistence.Temporal;
import jakarta.persistence.TemporalType;
import jakarta.persistence.Transient;
import java.time.Duration;
import java.util.Collections;
import java.util.Date;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;
import lombok.Getter;
import lombok.Setter;
import org.apache.commons.lang3.EnumUtils;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.data.validation.Constraints;

@Entity
@JsonPropertyOrder({"uuid", "config_uuid", "address", "is_leader", "is_local", "last_backup"})
@Getter
@Setter
public class PlatformInstance extends Model {

  private static final Finder<UUID, PlatformInstance> find =
      new Finder<UUID, PlatformInstance>(PlatformInstance.class) {};

  private static final Logger LOG = LoggerFactory.getLogger(PlatformInstance.class);

  private static long BACKUP_DISCONNECT_TIME_MILLIS = 15 * (60 * 1000);

  @Id
  @Constraints.Required
  @Column(nullable = false, unique = true)
  private UUID uuid;

  @Constraints.Required
  @Column(nullable = false, unique = true)
  private String address;

  @ManyToOne @JsonIgnore private HighAvailabilityConfig config;

  @Constraints.Required
  @Temporal(TemporalType.TIMESTAMP)
  @ApiModelProperty(value = "Last backup time", example = "2022-12-12T13:07:18Z")
  @JsonProperty("last_backup")
  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd'T'HH:mm:ss'Z'")
  private Date lastBackup;

  @Constraints.Required
  @Enumerated(EnumType.STRING)
  private State state;

  @JsonProperty("is_local")
  @Constraints.Required
  private boolean isLocal;

  @Transient private String ybaVersion = null;

  /** State and the transitions. */
  public enum State {
    LEADER {
      @Override
      public Set<State> nextStates() {
        return toSet(LEADER, STAND_BY);
      }
    },
    STAND_BY {
      @Override
      public Set<State> nextStates() {
        return toSet(STAND_BY, LEADER);
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
        throw new PlatformServiceException(
            BAD_REQUEST,
            String.format("Invalid state transition from %s to %s", name(), nextState.name()));
      }
    }
  }

  @JsonProperty("config_uuid")
  public UUID getConfigUuid() {
    return config != null ? config.getUuid() : null;
  }

  @JsonSetter("config_uuid")
  public void setConfigUuid(UUID configUuid) {
    if (configUuid != null) {
      this.config = HighAvailabilityConfig.maybeGet(configUuid).orElse(null);
    } else {
      this.config = null;
    }
  }

  public boolean updateLastBackup() {
    try {
      this.lastBackup = new Date();
      this.update();
      return true;
    } catch (Exception exception) {
      LOG.warn("DB error saving last backup time", exception);
    }
    return false;
  }

  public boolean updateLastBackup(Date lastBackup) {
    try {
      this.lastBackup = lastBackup;
      this.update();
      return true;
    } catch (Exception e) {
      LOG.warn("DB error saving last backup time", e);
    }
    return false;
  }

  // Backward compatibility.
  @JsonGetter("is_leader")
  public boolean isLeader() {
    return state == State.LEADER;
  }

  public void updateLocal(boolean isLocal) {
    setLocal(isLocal);
    update();
  }

  public void promote() {
    getState().validateTransition(State.LEADER);
    setState(State.LEADER);
    update();
  }

  public void demote() {
    getState().validateTransition(State.STAND_BY);
    setState(State.STAND_BY);
    update();
  }

  @JsonGetter("instance_state")
  public InstanceState getInstanceState() {
    if (this.lastBackup == null) {
      return InstanceState.AwaitingReplicas;
    }
    return isBackupOutdated(getReplicationFrequency(), this.lastBackup)
        ? InstanceState.Disconnected
        : InstanceState.Connected;
  }

  @JsonIgnore
  public boolean isAwaitingReplicas() {
    return this.lastBackup == null;
  }

  @JsonIgnore
  public boolean isConnected() {
    return !isBackupOutdated(getReplicationFrequency(), this.lastBackup);
  }

  @JsonIgnore
  public boolean isDisconnected() {
    return isBackupOutdated(getReplicationFrequency(), this.lastBackup);
  }

  private Duration getReplicationFrequency() {
    RuntimeConfGetter runtimeConfGetter =
        StaticInjectorHolder.injector().instanceOf(RuntimeConfGetter.class);
    return runtimeConfGetter.getGlobalConf(GlobalConfKeys.replicationFrequency);
  }

  public static PlatformInstance create(
      HighAvailabilityConfig config, String address, boolean isLeader, boolean isLocal) {
    PlatformInstance model = new PlatformInstance();
    model.uuid = UUID.randomUUID();
    model.config = config;
    model.address = address;
    model.setState(isLeader ? State.LEADER : State.STAND_BY);
    model.setLocal(isLocal);
    model.save();
    return model;
  }

  public static void update(
      PlatformInstance instance, String address, boolean isLeader, boolean isLocal) {
    instance.setAddress(address);
    instance.setState(isLeader ? State.LEADER : State.STAND_BY);
    instance.setLocal(isLocal);
    instance.update();
  }

  public static Optional<PlatformInstance> get(UUID uuid) {
    return Optional.ofNullable(find.byId(uuid));
  }

  public static Optional<PlatformInstance> getByAddress(String address) {
    return find.query().where().eq("address", address).findOneOrEmpty();
  }

  public static void delete(UUID uuid) {
    find.deleteById(uuid);
  }

  public static boolean isBackupOutdated(Duration replicationFrequency, Date lastBackupTime) {
    // Means awaiting connection
    if (lastBackupTime == null) {
      return false;
    }
    long backupAgeMillis = System.currentTimeMillis() - lastBackupTime.getTime();
    return backupAgeMillis
        >= Math.max(2 * replicationFrequency.toMillis(), BACKUP_DISCONNECT_TIME_MILLIS);
  }
}
