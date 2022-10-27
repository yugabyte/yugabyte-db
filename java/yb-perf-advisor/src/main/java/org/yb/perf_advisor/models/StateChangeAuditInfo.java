package org.yb.perf_advisor.models;

import io.ebean.Model;
import io.ebean.annotation.NotNull;
import lombok.Getter;
import lombok.Setter;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import javax.persistence.Column;
import javax.persistence.Entity;
import javax.persistence.Id;
import javax.persistence.ManyToOne;
import javax.persistence.Table;
import java.time.OffsetDateTime;
import java.util.UUID;

@Entity
@Getter
@Setter
@Table
public class StateChangeAuditInfo extends Model {
  public static final Logger LOG = LoggerFactory.getLogger(StateChangeAuditInfo.class);

  @Id
  public UUID id;

  @ManyToOne
  @NotNull
  public PerformanceRecommendation performanceRecommendation;

  public String fieldName;

  public String previousValue;

  public String updatedValue;

  public UUID userId;

  public OffsetDateTime timestamp;
}
