// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models;

import com.fasterxml.jackson.annotation.JsonAlias;
import com.fasterxml.jackson.annotation.JsonFormat;
import io.ebean.Finder;
import io.ebean.Model;
import io.ebean.annotation.DbJson;
import java.util.ArrayList;
import java.util.Date;
import java.util.List;
import java.util.UUID;
import javax.persistence.Column;
import javax.persistence.EmbeddedId;
import javax.persistence.Entity;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.data.validation.Constraints;
import play.libs.Json;

@Entity
public class HealthCheck extends Model {
  public static final Logger LOG = LoggerFactory.getLogger(HealthCheck.class);

  public static class Details {
    public static class NodeData {
      public String node;
      public String process;

      @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd HH:mm:ss")
      public Date timestamp;

      public String node_name;
      public Boolean has_error;
      public List<String> details;
      public String message;
    }

    @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd HH:mm:ss")
    public Date timestamp;

    public List<NodeData> data = new ArrayList<>();
    public String yb_version;
    // TODO: This was done as HealthCheckerTest is using error field. Reconcile this.
    @JsonAlias({"error"})
    public Boolean has_error = false;
  }

  // The max number of records to keep per universe.
  public static final int RECORD_LIMIT = 10;

  @EmbeddedId @Constraints.Required public HealthCheckKey idKey;

  // The customer id, needed only to enforce unique universe names for a customer.
  @Constraints.Required public Long customerId;

  // The Json serialized version of the details. This is used only in read from and writing to the
  // DB.
  @DbJson
  @Constraints.Required
  @Column(columnDefinition = "TEXT", nullable = false)
  public Details detailsJson = new Details();

  public boolean hasError() {
    if (detailsJson != null) {
      return detailsJson.has_error;
    }
    return false;
  }

  public static final Finder<UUID, HealthCheck> find =
      new Finder<UUID, HealthCheck>(HealthCheck.class) {};

  /**
   * Creates an empty universe.
   *
   * @param universeUUID: UUID of the universe..
   * @param customerId: UUID of the customer creating the universe.
   * @param details: The details that will describe the universe.
   * @return the newly created universe
   */
  public static HealthCheck addAndPrune(UUID universeUUID, Long customerId, String details) {
    // Create the HealthCheck object.
    HealthCheck check = new HealthCheck();
    check.idKey = HealthCheckKey.create(universeUUID);
    check.customerId = customerId;
    check.detailsJson = Json.fromJson(Json.parse(details), Details.class);
    // Save the object.
    check.save();
    keepOnlyLast(universeUUID, RECORD_LIMIT);
    return check;
  }

  public static void keepOnlyLast(UUID universeUUID, int numChecks) {
    List<HealthCheck> checks = getAll(universeUUID);
    for (int i = 0; i < checks.size() - numChecks; ++i) {
      checks.get(i).delete();
    }
  }

  /**
   * Returns the HealthCheck object for a certain universe.
   *
   * @param universeUUID
   * @return the HealthCheck object
   */
  public static List<HealthCheck> getAll(UUID universeUUID) {
    return find.query().where().eq("universe_uuid", universeUUID).orderBy("check_time").findList();
  }

  public static HealthCheck getLatest(UUID universeUUID) {
    List<HealthCheck> checks =
        find.query()
            .where()
            .eq("universe_uuid", universeUUID)
            .orderBy("check_time desc")
            .setMaxRows(1)
            .findList();
    if (checks != null && checks.size() > 0) {
      return checks.get(0);
    } else {
      return null;
    }
  }
}
