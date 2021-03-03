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
import com.fasterxml.jackson.annotation.JsonPropertyOrder;
import com.fasterxml.jackson.core.JsonGenerator;
import com.fasterxml.jackson.core.JsonParser;
import com.fasterxml.jackson.core.ObjectCodec;
import com.fasterxml.jackson.databind.*;
import com.fasterxml.jackson.databind.annotation.JsonDeserialize;
import com.fasterxml.jackson.databind.annotation.JsonSerialize;
import io.ebean.Finder;
import io.ebean.Model;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.data.validation.Constraints;

import javax.persistence.*;
import java.io.IOException;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.UUID;

@Entity
@JsonPropertyOrder({ "uuid", "config_uuid", "address", "is_leader", "is_local", "last_backup" })
@JsonDeserialize(using= PlatformInstance.PlatformInstanceDeserializer.class)
public class PlatformInstance extends Model {

  private static final Finder<UUID, PlatformInstance> find =
    new Finder<UUID, PlatformInstance>(PlatformInstance.class){};

  private static final Logger LOG = LoggerFactory.getLogger(PlatformInstance.class);

  private static final SimpleDateFormat TIMESTAMP_FORMAT =
    new SimpleDateFormat("EEE MMM dd HH:mm:ss zzz yyyy");

  @Id
  @Constraints.Required
  @Column(nullable = false, unique = true)
  private UUID uuid;

  @Constraints.Required
  @Column(nullable = false, unique = true)
  private String address;

  @ManyToOne
  private HighAvailabilityConfig config;

  @Constraints.Required
  @Temporal(TemporalType.TIMESTAMP)
  private Date lastBackup;

  @Constraints.Required()
  @Column(unique = true)
  private Boolean isLeader;

  @Constraints.Required
  @Column(unique = true)
  private Boolean isLocal;

  public UUID getUUID() {
    return this.uuid;
  }

  public void setUUID(UUID uuid) {
    this.uuid = uuid;
  }

  public String getAddress() {
    return this.address;
  }

  public void setAddress(String address) {
    this.address = address;
  }

  @JsonGetter("config_uuid")
  @JsonSerialize(using = HAConfigToUUIDSerializer.class)
  public HighAvailabilityConfig getConfig() {
    return this.config;
  }

  public void setConfig(HighAvailabilityConfig config) {
    this.config = config;
  }

  @JsonGetter("last_backup")
  public Date getLastBackup() {
    return this.lastBackup;
  }

  public void setLastBackup(Date lastBackup) {
    this.lastBackup = lastBackup;
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

  @JsonGetter("is_leader")
  public boolean getIsLeader() {
    return this.isLeader != null;
  }

  public void setIsLeader(Boolean isLeader) {
    this.isLeader = isLeader ? true : null;
  }

  @JsonGetter("is_local")
  public Boolean getIsLocal() {
    return this.isLocal != null;
  }

  public void setIsLocal(Boolean isLocal) {
    this.isLocal = isLocal ? true : null;
  }

  public void setIsLocalAndUpdate(Boolean isLocal) {
    this.setIsLocal(isLocal);
    this.update();
  }

  public void promote() {
    this.setIsLeader(true);
    this.update();
  }

  public void demote() {
    this.setIsLeader(false);
    this.update();
  }

  public static PlatformInstance create(
    HighAvailabilityConfig config,
    String address,
    boolean isLeader,
    boolean isLocal
  ) {
    PlatformInstance model = new PlatformInstance();
    model.uuid = UUID.randomUUID();
    model.config = config;
    model.address = address;
    model.isLeader = isLeader ? true : null;
    model.isLocal = isLocal ? true : null;
    model.save();

    return model;
  }

  public static void update(
    PlatformInstance instance,
    String address,
    boolean isLeader,
    boolean isLocal
  ) {
    instance.setAddress(address);
    instance.setIsLeader(isLeader);
    instance.setIsLocal(isLocal);
    instance.update();
  }

  public static PlatformInstance get(UUID uuid) {
    return find.byId(uuid);
  }

  public static PlatformInstance getByAddress(String address) {
    return find.query().where()
      .eq("address", address)
      .findOne();
  }

  public static void delete(UUID uuid) {
    find.deleteById(uuid);
  }

  private static class HAConfigToUUIDSerializer extends JsonSerializer<HighAvailabilityConfig> {
    @Override
    public void serialize(
      HighAvailabilityConfig value,
      JsonGenerator gen,
      SerializerProvider provider
    ) throws IOException {
      gen.writeString(value.getUUID().toString());
    }
  }

  static class PlatformInstanceDeserializer extends JsonDeserializer<PlatformInstance> {
    @Override
    public PlatformInstance deserialize(JsonParser jp, DeserializationContext ctxt)
      throws IOException {
      ObjectCodec codec = jp.getCodec();
      JsonNode json = codec.readTree(jp);
      try {
        if (json.has("uuid") && json.has("config_uuid") &&
          json.has("address") && json.has("is_leader") &&
          json.has("is_local") && json.has("last_backup")) {
          PlatformInstance instance = new PlatformInstance();
          instance.uuid = UUID.fromString(json.get("uuid").asText());
          instance.config =
            HighAvailabilityConfig.get(UUID.fromString(json.get("config_uuid").asText()));
          instance.address = json.get("address").asText();
          instance.setIsLeader(json.get("is_leader").asBoolean());
          instance.setIsLocal(json.get("is_local").asBoolean());
          JsonNode lastBackup = json.get("last_backup");
          instance.lastBackup =
            lastBackup.asText().equals("null") ? null : new Date(lastBackup.asLong());

          return instance;
        } else {
          LOG.error(
            "Could not deserialize {} to platform instance model. " +
              "At least one expected field is missing",
            json
          );
        }
      } catch (Exception e) {
        LOG.error("Error importing platform instance: {}", json, e);
      }

      return null;
    }
  }
}
