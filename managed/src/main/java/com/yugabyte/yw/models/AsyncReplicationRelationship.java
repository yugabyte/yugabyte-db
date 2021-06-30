package com.yugabyte.yw.models;

import com.fasterxml.jackson.databind.JsonNode;
import io.ebean.Finder;
import io.ebean.Model;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.data.validation.Constraints;
import play.libs.Json;

import javax.persistence.*;
import java.util.UUID;

@Table(
    uniqueConstraints =
        @UniqueConstraint(
            columnNames = {
              "source_universe_uuid",
              "source_table_id",
              "target_universe_uuid",
              "target_table_id"
            }))
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
  public String sourceTableID;

  @ManyToOne
  @Constraints.Required
  @JoinColumn(name = "target_universe_uuid", nullable = false)
  public Universe targetUniverse;

  @Constraints.Required
  @Column(nullable = false)
  public String targetTableID;

  @Constraints.Required
  @Column(nullable = false)
  public boolean active;

  public static AsyncReplicationRelationship create(
      Universe sourceUniverse,
      String sourceTableID,
      Universe targetUniverse,
      String targetTableID,
      boolean active) {
    AsyncReplicationRelationship relationship = new AsyncReplicationRelationship();
    relationship.uuid = UUID.randomUUID();
    relationship.sourceUniverse = sourceUniverse;
    relationship.sourceTableID = sourceTableID;
    relationship.targetUniverse = targetUniverse;
    relationship.targetTableID = targetTableID;
    relationship.active = active;
    relationship.save();
    return relationship;
  }

  public static AsyncReplicationRelationship get(UUID asyncReplicationRelationshipUUID) {
    return find.query().where().idEq(asyncReplicationRelationshipUUID).findOne();
  }

  public static AsyncReplicationRelationship get(
      UUID sourceUniverseUUID,
      String sourceTableID,
      UUID targetUniverseUUID,
      String targetTableID) {
    return find.query()
        .where()
        .eq("source_universe_uuid", sourceUniverseUUID)
        .eq("source_table_id", sourceTableID)
        .eq("target_universe_uuid", targetUniverseUUID)
        .eq("target_table_id", targetTableID)
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
        .put("sourceTableID", sourceTableID)
        .put("targetUniverseUUID", targetUniverse.universeUUID.toString())
        .put("targetTableID", targetTableID)
        .put("active", active);
  }

  @Override
  public String toString() {
    return "AsyncReplicationRelationship [uuid="
        + uuid
        + ", sourceUniverseUUID="
        + sourceUniverse.universeUUID
        + ", sourceTableID="
        + sourceTableID
        + ", targetUniverseUUID="
        + targetUniverse.universeUUID
        + ", targetTableID="
        + targetTableID
        + ", active="
        + active
        + "]";
  }
}
