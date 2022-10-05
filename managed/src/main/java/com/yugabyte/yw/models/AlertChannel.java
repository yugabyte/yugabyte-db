// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models;

import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_ONLY;
import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_WRITE;

import com.fasterxml.jackson.annotation.JsonIgnore;
import com.fasterxml.jackson.annotation.JsonProperty;
import com.fasterxml.jackson.annotation.JsonSubTypes;
import com.fasterxml.jackson.annotation.JsonTypeInfo;
import com.fasterxml.jackson.annotation.JsonTypeInfo.As;
import com.yugabyte.yw.common.alerts.AlertChannelEmailParams;
import com.yugabyte.yw.common.alerts.AlertChannelPagerDutyParams;
import com.yugabyte.yw.common.alerts.AlertChannelParams;
import com.yugabyte.yw.common.alerts.AlertChannelSlackParams;
import com.yugabyte.yw.common.alerts.AlertChannelWebHookParams;
import io.ebean.ExpressionList;
import io.ebean.Finder;
import io.ebean.Model;
import io.ebean.annotation.DbJson;
import io.ebean.annotation.EnumValue;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.ArrayList;
import java.util.List;
import java.util.Set;
import java.util.UUID;
import javax.persistence.Column;
import javax.persistence.Entity;
import javax.persistence.FetchType;
import javax.persistence.Id;
import javax.persistence.ManyToMany;
import javax.validation.Valid;
import javax.validation.constraints.NotNull;
import javax.validation.constraints.Size;
import lombok.Data;
import lombok.EqualsAndHashCode;
import lombok.ToString;
import lombok.experimental.Accessors;

@Data
@Accessors(chain = true)
@EqualsAndHashCode(callSuper = false)
@Entity
@ApiModel(description = "Alert notification channel")
public class AlertChannel extends Model {

  /** These are the possible types of channels. */
  public enum ChannelType {
    @EnumValue("Email")
    Email,

    @EnumValue("Slack")
    Slack,

    @EnumValue("PagerDuty")
    PagerDuty,

    @EnumValue("WebHook")
    WebHook,
  }

  @Id
  @Column(nullable = false, unique = true)
  @ApiModelProperty(value = "Channel UUID", accessMode = READ_ONLY)
  private UUID uuid;

  @NotNull
  @Size(min = 1, max = 63)
  @Column(columnDefinition = "Text", nullable = false)
  @ApiModelProperty(value = "Name", accessMode = READ_WRITE)
  private String name;

  @NotNull
  @Column(nullable = false)
  @JsonProperty("customer_uuid")
  @ApiModelProperty(value = "Customer UUID", accessMode = READ_ONLY)
  private UUID customerUUID;

  @NotNull
  @Valid
  @Column(columnDefinition = "TEXT", nullable = false)
  @DbJson
  @JsonTypeInfo(use = JsonTypeInfo.Id.NAME, include = As.PROPERTY, property = "channelType")
  @JsonSubTypes({
    @JsonSubTypes.Type(value = AlertChannelEmailParams.class, name = "Email"),
    @JsonSubTypes.Type(value = AlertChannelSlackParams.class, name = "Slack"),
    @JsonSubTypes.Type(value = AlertChannelPagerDutyParams.class, name = "PagerDuty"),
    @JsonSubTypes.Type(value = AlertChannelWebHookParams.class, name = "WebHook")
  })
  @ApiModelProperty(value = "Channel params", accessMode = READ_WRITE)
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
