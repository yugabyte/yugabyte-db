// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models;

import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_ONLY;
import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_WRITE;

import com.fasterxml.jackson.annotation.JsonIgnore;
import com.fasterxml.jackson.annotation.JsonProperty;
import com.yugabyte.yw.common.alerts.AlertChannelParams;
import io.ebean.ExpressionList;
import io.ebean.Finder;
import io.ebean.Model;
import io.ebean.annotation.DbJson;
import io.ebean.annotation.Encrypted;
import io.ebean.annotation.EnumValue;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import jakarta.persistence.Entity;
import jakarta.persistence.FetchType;
import jakarta.persistence.Id;
import jakarta.persistence.ManyToMany;
import java.util.ArrayList;
import java.util.List;
import java.util.Set;
import java.util.UUID;
import javax.validation.Valid;
import javax.validation.constraints.NotNull;
import javax.validation.constraints.Size;
import lombok.Data;
import lombok.EqualsAndHashCode;
import lombok.Getter;
import lombok.ToString;
import lombok.experimental.Accessors;

@Data
@Accessors(chain = true)
@EqualsAndHashCode(callSuper = false)
@Entity
@ApiModel(description = "Alert notification channel")
public class AlertChannel extends Model {

  /** These are the possible types of channels. */
  @Getter
  public enum ChannelType {
    @EnumValue("Email")
    Email(true),
    @EnumValue("Slack")
    Slack(false),
    @EnumValue("PagerDuty")
    PagerDuty(true),
    @EnumValue("WebHook")
    WebHook(false);

    private final boolean hasTitle;

    ChannelType(boolean hasTitle) {
      this.hasTitle = hasTitle;
    }
  }

  @Id
  @ApiModelProperty(value = "Channel UUID", accessMode = READ_ONLY)
  private UUID uuid;

  @NotNull
  @Size(min = 1, max = 63)
  @ApiModelProperty(value = "Name", accessMode = READ_WRITE)
  private String name;

  @NotNull
  @JsonProperty("customer_uuid")
  @ApiModelProperty(value = "Customer UUID", accessMode = READ_ONLY)
  private UUID customerUUID;

  @NotNull
  @Valid
  @DbJson
  @ApiModelProperty(value = "Channel params", accessMode = READ_WRITE)
  @Encrypted
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
