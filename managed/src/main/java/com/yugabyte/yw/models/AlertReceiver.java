// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models;

import static play.mvc.Http.Status.BAD_REQUEST;

import java.util.List;
import java.util.Objects;
import java.util.UUID;

import javax.persistence.Column;
import javax.persistence.Entity;
import javax.persistence.Id;

import com.fasterxml.jackson.annotation.JsonProperty;
import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.common.YWServiceException;
import com.yugabyte.yw.common.alerts.AlertReceiverParams;
import com.yugabyte.yw.common.alerts.AlertUtils;

import io.ebean.Finder;
import io.ebean.Model;
import io.ebean.annotation.DbJson;
import io.ebean.annotation.EnumValue;
import play.data.validation.Constraints;
import play.data.validation.Constraints.Required;
import play.libs.Json;

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
  @Column(nullable = false)
  @JsonProperty("customer_uuid")
  private UUID customerUUID;

  @Constraints.Required
  @Column(nullable = false)
  @JsonProperty("target_type")
  private TargetType targetType;

  @Constraints.Required
  @Column(columnDefinition = "TEXT", nullable = false)
  @DbJson
  private JsonNode params;

  private static final Finder<UUID, AlertReceiver> find =
      new Finder<UUID, AlertReceiver>(AlertReceiver.class) {};

  public static AlertReceiver create(
      UUID customerUUID, TargetType targetType, AlertReceiverParams params) {
    return create(UUID.randomUUID(), customerUUID, targetType, params);
  }

  public static AlertReceiver create(
      UUID uuid, UUID customerUUID, TargetType targetType, AlertReceiverParams params) {
    AlertReceiver receiver = new AlertReceiver();
    receiver.uuid = uuid;
    receiver.customerUUID = customerUUID;
    receiver.targetType = targetType;
    receiver.setParams(params);
    receiver.save();
    return receiver;
  }

  public UUID getUuid() {
    return uuid;
  }

  public void setUuid(UUID uuid) {
    this.uuid = uuid;
  }

  public TargetType getTargetType() {
    return targetType;
  }

  public void setTargetType(TargetType targetType) {
    this.targetType = targetType;
  }

  public AlertReceiverParams getParams() {
    return AlertUtils.fromJson(targetType, params);
  }

  public void setParams(AlertReceiverParams params) {
    this.params = Json.toJson(params);
  }

  public UUID getCustomerUUID() {
    return customerUUID;
  }

  public void setCustomerUuid(UUID customerUUID) {
    this.customerUUID = customerUUID;
  }

  public static AlertReceiver get(UUID customerUUID, UUID receiverUUID) {
    return find.query().where().idEq(receiverUUID).eq("customer_uuid", customerUUID).findOne();
  }

  public static AlertReceiver getOrBadRequest(UUID customerUUID, @Required UUID receiverUUID) {
    AlertReceiver alertReceiver = get(customerUUID, receiverUUID);
    if (alertReceiver == null) {
      throw new YWServiceException(BAD_REQUEST, "Invalid Alert Receiver UUID: " + receiverUUID);
    }
    return alertReceiver;
  }

  public static List<AlertReceiver> list(UUID customerUUID) {
    return find.query().where().eq("customer_uuid", customerUUID).findList();
  }

  @Override
  public int hashCode() {
    return Objects.hash(customerUUID, params, targetType, uuid);
  }

  @Override
  public boolean equals(Object obj) {
    if (this == obj) {
      return true;
    }
    if (!(obj instanceof AlertReceiver)) {
      return false;
    }
    AlertReceiver other = (AlertReceiver) obj;
    return Objects.equals(customerUUID, other.customerUUID)
        && Objects.equals(params, other.params)
        && targetType == other.targetType
        && Objects.equals(uuid, other.uuid);
  }

  @Override
  public String toString() {
    return "AlertReceiver [uuid="
        + uuid
        + ", customerUUID="
        + customerUUID
        + ", targetType="
        + targetType
        + ", params="
        + params
        + "]";
  }
}
