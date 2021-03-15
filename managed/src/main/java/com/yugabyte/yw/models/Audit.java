// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.models;

import com.fasterxml.jackson.databind.JsonNode;
import io.ebean.Finder;
import io.ebean.Model;
import io.ebean.annotation.CreatedTimestamp;
import io.ebean.annotation.DbJson;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.data.validation.Constraints;
import play.mvc.Http;

import javax.persistence.*;
import java.util.Date;
import java.util.List;
import java.util.UUID;

@Entity
public class Audit extends Model {

  public static final Logger LOG = LoggerFactory.getLogger(Audit.class);

  // An auto incrementing, user-friendly id for the audit entry.
  @Id
  @SequenceGenerator(name = "audit_id_seq", sequenceName = "audit_id_seq", allocationSize = 1)
  @GeneratedValue(strategy = GenerationType.SEQUENCE, generator = "audit_id_seq")
  private Long id;

  public Long getAuditID() {
    return this.id;
  }

  @Constraints.Required
  @Column(nullable = false)
  private UUID userUUID;

  public UUID getUserUUID() {
    return this.userUUID;
  }

  @Constraints.Required
  @Column(nullable = false)
  private UUID customerUUID;

  public UUID getCustomerUUID() {
    return this.customerUUID;
  }

  // The task creation time.
  @CreatedTimestamp
  private final Date timestamp;

  public Date getTimestamp() {
    return this.timestamp;
  }

  @Column(columnDefinition = "TEXT")
  @DbJson
  private JsonNode payload;

  public JsonNode getPayload() {
    return this.payload;
  }

  public void setPayload(JsonNode payload) {
    this.payload = payload;
    this.save();
  }

  @Constraints.Required
  @Column(columnDefinition = "TEXT", nullable = false)
  private String apiCall;

  public String getApiCall() {
    return this.apiCall;
  }

  @Constraints.Required
  @Column(columnDefinition = "TEXT", nullable = false)
  private String apiMethod;

  public String getApiMethod() {
    return this.apiMethod;
  }

  @Column(unique = true)
  private UUID taskUUID;

  public void setTaskUUID(UUID uuid) {
    this.taskUUID = uuid;
    this.save();
  }

  public UUID getTaskUUID() {
    return this.taskUUID;
  }

  public Audit() {
    this.timestamp = new Date();
  }

  public static final Finder<UUID, Audit> find = new Finder<UUID, Audit>(Audit.class) {
  };

  public static void createAuditEntry(Http.Context ctx, Http.Request request) {
    createAuditEntry(ctx, request, null, null);
  }

  public static void createAuditEntry(Http.Context ctx, Http.Request request, JsonNode params) {
    createAuditEntry(ctx, request, params, null);
  }

  public static void createAuditEntry(Http.Context ctx, Http.Request request, UUID taskUUID) {
    createAuditEntry(ctx, request, null, taskUUID);
  }

  public static void createAuditEntry(
    Http.Context ctx, Http.Request request, JsonNode params, UUID taskUUID) {
    Users user = (Users) ctx.args.get("user");
    String method = request.method();
    String path = request.path();
    Audit entry = Audit.create(user.uuid, user.customerUUID, path, method, params, taskUUID);
  }

  /**
   * Create new audit entry.
   *
   * @return Newly Created Audit table entry.
   */
  public static Audit create(
    UUID userUUID,
    UUID customerUUID,
    String apiCall,
    String apiMethod,
    JsonNode body,
    UUID taskUUID) {
    Audit entry = new Audit();
    entry.customerUUID = customerUUID;
    entry.userUUID = userUUID;
    entry.apiCall = apiCall;
    entry.apiMethod = apiMethod;
    entry.taskUUID = taskUUID;
    entry.payload = body;
    entry.save();
    return entry;
  }

  public static List<Audit> getAll(UUID customerUUID) {
    return find.query().where().eq("customer_uuid", customerUUID).findList();
  }

  public static Audit getFromTaskUUID(UUID taskUUID) {
    return find.query().where().eq("task_uuid", taskUUID).findOne();
  }

  public static List<Audit> getAllUserEntries(UUID userUUID) {
    return find.query().where().eq("user_uuid", userUUID).findList();
  }
}
