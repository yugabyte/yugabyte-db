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

import com.fasterxml.jackson.annotation.JsonGetter;
import com.fasterxml.jackson.annotation.JsonIgnore;
import com.fasterxml.jackson.annotation.JsonPropertyOrder;
import io.ebean.Finder;
import io.ebean.Model;
import play.data.validation.Constraints;

import javax.crypto.KeyGenerator;
import javax.crypto.SecretKey;
import javax.persistence.*;
import java.util.*;
import java.util.stream.Collectors;

@Entity
@JsonPropertyOrder({ "uuid", "cluster_key", "instances" })
public class HighAvailabilityConfig extends Model {

  private static final Finder<UUID, HighAvailabilityConfig> find =
    new Finder<UUID, HighAvailabilityConfig>(HighAvailabilityConfig.class){};

  @Id
  @Constraints.Required
  @Column(nullable = false, unique = true)
  private UUID uuid;

  @Column(nullable = false, unique = true)
  private String clusterKey;

  @Temporal(TemporalType.TIMESTAMP)
  private Date lastFailover;

  @OneToMany(mappedBy = "config", cascade= CascadeType.ALL)
  private List<PlatformInstance> instances;

  public UUID getUUID() {
    return this.uuid;
  }

  public void setUUID(UUID uuid) {
    this.uuid = uuid;
  }

  @JsonGetter("cluster_key")
  public String getClusterKey() {
    return this.clusterKey;
  }

  public void setClusterKey(String clusterKey) {
    this.clusterKey = clusterKey;
  }

  @JsonGetter("last_failover")
  public Date getLastFailover() {
    return this.lastFailover;
  }

  public void setLastFailover(Date lastFailover) {
    this.lastFailover = lastFailover;
  }

  public void updateLastFailover() {
    this.lastFailover = new Date();
    this.update();
  }

  public List<PlatformInstance> getInstances() {
    return this.instances;
  }

  @JsonIgnore
  public List<PlatformInstance> getRemoteInstances() {
    return this.instances.stream()
      .filter(i -> !i.getIsLocal())
      .collect(Collectors.toList());
  }

  @JsonIgnore
  public boolean isLocalLeader() {
    return this.instances.stream()
      .anyMatch(i -> i.getIsLeader() && i.getIsLocal());
  }

  @JsonIgnore
  public PlatformInstance getLocal() {
    return this.instances.stream()
      .filter(PlatformInstance::getIsLocal)
      .findFirst()
      .orElse(null);
  }

  @JsonIgnore
  public PlatformInstance getLeader() {
    return this.instances.stream()
      .filter(PlatformInstance::getIsLeader)
      .findFirst()
      .orElse(null);
  }

  public static HighAvailabilityConfig create(String clusterKey) {
    HighAvailabilityConfig model = new HighAvailabilityConfig();
    model.uuid = UUID.randomUUID();
    model.clusterKey = clusterKey;
    model.instances = new ArrayList<>();
    model.save();

    return model;
  }

  public static void update(HighAvailabilityConfig config, String clusterKey) {
    config.clusterKey = clusterKey;
    config.update();
  }

  public static HighAvailabilityConfig get(UUID uuid) {
    return find.byId(uuid);
  }

  public static HighAvailabilityConfig getByClusterKey(String clusterKey) {
    return find.query().where()
      .eq("cluster_key", clusterKey)
      .findOne();
  }

  public static List<HighAvailabilityConfig> list() {
    return find.all();
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
}
