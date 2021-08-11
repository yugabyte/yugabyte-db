// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models;

import com.fasterxml.jackson.annotation.JsonIgnore;
import com.fasterxml.jackson.annotation.JsonProperty;
import com.fasterxml.jackson.annotation.JsonSubTypes;
import com.fasterxml.jackson.annotation.JsonTypeInfo;
import com.fasterxml.jackson.annotation.JsonTypeInfo.As;
import com.yugabyte.yw.common.alerts.AlertReceiverEmailParams;
import com.yugabyte.yw.common.alerts.AlertReceiverParams;
import com.yugabyte.yw.common.alerts.AlertReceiverSlackParams;
import io.ebean.ExpressionList;
import io.ebean.Finder;
import io.ebean.Model;
import io.ebean.annotation.DbJson;
import io.ebean.annotation.EnumValue;
import java.util.ArrayList;
import java.util.List;
import java.util.Set;
import java.util.UUID;
import javax.persistence.Column;
import javax.persistence.Entity;
import javax.persistence.FetchType;
import javax.persistence.Id;
import javax.persistence.ManyToMany;
import lombok.Data;
import lombok.EqualsAndHashCode;
import lombok.ToString;
import lombok.experimental.Accessors;
import play.data.validation.Constraints;

@Data
@Accessors(chain = true)
@EqualsAndHashCode(callSuper = false)
@Entity
public class AlertReceiver extends Model {

  /** These are the possible types of channels. */
  public enum TargetType {
    @EnumValue("Email")
    Email,

    @EnumValue("Slack")
    Slack,

    @EnumValue("Sms")
    Sms,

    @EnumValue("PagerDuty")
    PagerDuty,
  }

  @Constraints.Required
  @Id
  @Column(nullable = false, unique = true)
  private UUID uuid;

  @Constraints.Required
  @Column(columnDefinition = "Text", length = 255, nullable = false)
  private String name;

  @Constraints.Required
  @Column(nullable = false)
  @JsonProperty("customer_uuid")
  private UUID customerUUID;

  @Constraints.Required
  @Column(columnDefinition = "TEXT", nullable = false)
  @DbJson
  @JsonTypeInfo(use = JsonTypeInfo.Id.NAME, include = As.PROPERTY, property = "targetType")
  @JsonSubTypes({
    @JsonSubTypes.Type(value = AlertReceiverEmailParams.class, name = "Email"),
    @JsonSubTypes.Type(value = AlertReceiverSlackParams.class, name = "Slack")
  })
  private AlertReceiverParams params;

  @JsonIgnore
  @ToString.Exclude
  @EqualsAndHashCode.Exclude
  @ManyToMany(mappedBy = "receivers", fetch = FetchType.LAZY)
  private Set<AlertRoute> routes;

  private static final Finder<UUID, AlertReceiver> find =
      new Finder<UUID, AlertReceiver>(AlertReceiver.class) {};

  @JsonIgnore
  public List<AlertRoute> getRoutesList() {
    return new ArrayList<>(routes);
  }

  public AlertReceiver generateUUID() {
    this.uuid = UUID.randomUUID();
    return this;
  }

  public static ExpressionList<AlertReceiver> createQuery() {
    return find.query().where();
  }

  public static AlertReceiver get(UUID customerUUID, UUID receiverUUID) {
    return AlertReceiver.createQuery()
        .idEq(receiverUUID)
        .eq("customerUUID", customerUUID)
        .findOne();
  }
}
