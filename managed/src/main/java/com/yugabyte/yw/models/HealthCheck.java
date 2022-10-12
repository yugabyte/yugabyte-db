// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models;

import com.fasterxml.jackson.annotation.JsonFormat;
import com.fasterxml.jackson.databind.PropertyNamingStrategy;
import com.fasterxml.jackson.databind.annotation.JsonNaming;
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
import lombok.Data;
import lombok.experimental.Accessors;
import org.apache.commons.lang3.StringUtils;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.data.validation.Constraints;

@Entity
public class HealthCheck extends Model {
  public static final Logger LOG = LoggerFactory.getLogger(HealthCheck.class);

  @Data
  @Accessors(chain = true)
  @JsonNaming(PropertyNamingStrategy.SnakeCaseStrategy.class)
  public static class Details {
    @Data
    @Accessors(chain = true)
    @JsonNaming(PropertyNamingStrategy.SnakeCaseStrategy.class)
    public static class NodeData {
      private String node;
      private String process;

      @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd HH:mm:ss")
      private Date timestamp;

      private String nodeName;
      private Boolean hasError = false;
      private Boolean hasWarning = false;
      private Boolean metricsOnly = false;
      private List<String> details;
      private List<Metric> metrics;
      private String message;

      public String toHumanReadableString() {
        return String.format(
            "%s (%s) - %s - '%s'",
            nodeName,
            node,
            (StringUtils.isEmpty(process) ? message : message + " (" + process + ")"),
            String.join("; ", details));
      }
    }

    @Data
    @Accessors(chain = true)
    public static class Metric {
      private String name;
      private String help;
      private String unit;
      private List<MetricValue> values;
    }

    @Data
    @Accessors(chain = true)
    public static class MetricValue {
      private Double value;
      private List<MetricLabel> labels;
    }

    @Data
    @Accessors(chain = true)
    public static class MetricLabel {
      private String name;
      private String value;
    }

    @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd HH:mm:ss")
    private Date timestamp;

    private List<NodeData> data = new ArrayList<>();
    private String ybVersion;
    private Boolean hasError = false;
    private Boolean hasWarning = false;
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
      return detailsJson.getHasError();
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
   * @param report: The details that will describe the universe.
   * @return the newly created universe
   */
  public static HealthCheck addAndPrune(UUID universeUUID, Long customerId, Details report) {
    // Create the HealthCheck object.
    HealthCheck check = new HealthCheck();
    check.idKey = HealthCheckKey.create(universeUUID);
    check.customerId = customerId;
    check.detailsJson = report;
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
