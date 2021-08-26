// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models;

import com.fasterxml.jackson.annotation.JsonIgnore;
import com.fasterxml.jackson.annotation.JsonProperty;
import com.fasterxml.jackson.annotation.JsonSubTypes;
import com.fasterxml.jackson.annotation.JsonTypeInfo;
import com.fasterxml.jackson.annotation.JsonTypeInfo.As;
import com.yugabyte.yw.common.alerts.AlertChannelEmailParams;
import com.yugabyte.yw.common.alerts.AlertChannelParams;
import com.yugabyte.yw.common.alerts.AlertChannelSlackParams;

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
public class AlertChannel extends Model {

  public static final int MAX_NAME_LENGTH = 255;

  /** These are the possible types of channels. */
  public enum ChannelType {
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
  @Column(columnDefinition = "Text", length = MAX_NAME_LENGTH, nullable = false)
  private String name;

  @Constraints.Required
  @Column(nullable = false)
  @JsonProperty("customer_uuid")
  private UUID customerUUID;

  @Constraints.Required
  @Column(columnDefinition = "TEXT", nullable = false)
  @DbJson
  @JsonTypeInfo(use = JsonTypeInfo.Id.NAME, include = As.PROPERTY, property = "channelType")
  @JsonSubTypes({
    @JsonSubTypes.Type(value = AlertChannelEmailParams.class, name = "Email"),
    @JsonSubTypes.Type(value = AlertChannelSlackParams.class, name = "Slack")
  })
  private AlertChannelParams params;

  @JsonIgnore
  @ToString.Exclude
  @EqualsAndHashCode.Exclude
  @ManyToMany(mappedBy = "channels", fetch = FetchType.LAZY)
  private Set<AlertDestination> destinations;

  private static final Finder<UUID, AlertChannel> find =
      new Finder<UUID, AlertChannel>(AlertChannel.class) {};

  @JsonIgnore
  public List<AlertDestination> getDestinationsList() {
    return new ArrayList<>(destinations);
  }

  public AlertChannel generateUUID() {
    this.uuid = UUID.randomUUID();
    return this;
  }

  public static ExpressionList<AlertChannel> createQuery() {
    return find.query().where();
  }

  public static AlertChannel get(UUID customerUUID, UUID channelUUID) {
    return AlertChannel.createQuery().idEq(channelUUID).eq("customerUUID", customerUUID).findOne();
  }
}
