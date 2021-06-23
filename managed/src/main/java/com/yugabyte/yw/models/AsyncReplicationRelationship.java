package com.yugabyte.yw.models;

import com.fasterxml.jackson.databind.JsonNode;
import io.ebean.Finder;
import io.ebean.Model;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.data.validation.Constraints;
import play.libs.Json;

import javax.persistence.*;
import java.util.List;
import java.util.UUID;

@Entity
public class AsyncReplicationRelationship extends Model {

  public static final Logger LOG = LoggerFactory.getLogger(AsyncReplicationRelationship.class);

  private static final Finder<UUID, AsyncReplicationRelationship> find =
      new Finder<UUID, AsyncReplicationRelationship>(AsyncReplicationRelationship.class) {};

  @Constraints.Required
  @Id
  @Column(nullable = false, unique = true)
  public UUID uuid;

  @ManyToOne
  @Constraints.Required
  @JoinColumn(name = "source_universe_uuid", nullable = false)
  public Universe sourceUniverse;

  @Constraints.Required
  @Column(nullable = false)
  public UUID sourceTableUUID;

  @ManyToOne
  @Constraints.Required
  @JoinColumn(name = "target_universe_uuid", nullable = false)
  public Universe targetUniverse;

  @Constraints.Required
  @Column(nullable = false)
  public UUID targetTableUUID;

  @Constraints.Required
  @Column(nullable = false)
  public boolean active;

  public static AsyncReplicationRelationship create(
      Universe sourceUniverse,
      UUID sourceTableUUID,
      Universe targetUniverse,
      UUID targetTableUUID,
      boolean active) {
    AsyncReplicationRelationship relationship = new AsyncReplicationRelationship();
    relationship.uuid = UUID.randomUUID();
    relationship.sourceUniverse = sourceUniverse;
    relationship.sourceTableUUID = sourceTableUUID;
    relationship.targetUniverse = targetUniverse;
    relationship.targetTableUUID = targetTableUUID;
    relationship.active = active;
    relationship.save();
    return relationship;
  }

  public static AsyncReplicationRelationship get(UUID asyncReplicationRelationshipUUID) {
    return find.query().where().idEq(asyncReplicationRelationshipUUID).findOne();
  }

  public static AsyncReplicationRelationship get(
      UUID sourceUniverseUUID,
      UUID sourceTableUUID,
      UUID targetUniverseUUID,
      UUID targetTableUUID) {
    return find.query()
        .where()
        .eq("source_universe_uuid", sourceUniverseUUID)
        .eq("source_table_uuid", sourceTableUUID)
        .eq("target_universe_uuid", targetUniverseUUID)
        .eq("target_table_uuid", targetTableUUID)
        .findOne();
  }

  public static boolean delete(UUID asyncReplicationRelationshipUUID) {
    AsyncReplicationRelationship relationship = get(asyncReplicationRelationshipUUID);
    return relationship != null && relationship.delete();
  }

  public void update(boolean active) {
    this.active = active;
    update();
  }

  public JsonNode toJson() {
    return Json.newObject()
        .put("uuid", uuid.toString())
        .put("sourceUniverseUUID", sourceUniverse.universeUUID.toString())
        .put("sourceTableUUID", sourceTableUUID.toString())
        .put("targetUniverseUUID", targetUniverse.universeUUID.toString())
        .put("targetTableUUID", targetTableUUID.toString())
        .put("active", active);
  }

  @Override
  public String toString() {
    return "AsyncReplicationRelationship [uuid="
        + uuid
        + ", sourceUniverseUUID="
        + sourceUniverse.universeUUID
        + ", sourceTableUUID="
        + sourceTableUUID
        + ", targetUniverseUUID="
        + targetUniverse.universeUUID
        + ", targetTableUUID="
        + targetTableUUID
        + ", active="
        + active
        + "]";
  }
}
