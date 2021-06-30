// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models;

import static com.yugabyte.yw.models.helpers.CommonUtils.appendInClause;
import static play.mvc.Http.Status.BAD_REQUEST;

import java.util.List;
import java.util.UUID;
import java.util.stream.Collectors;

import javax.persistence.Column;
import javax.persistence.Entity;
import javax.persistence.Id;

import com.fasterxml.jackson.annotation.JsonProperty;
import com.fasterxml.jackson.annotation.JsonSubTypes;
import com.fasterxml.jackson.annotation.JsonTypeInfo;
import com.fasterxml.jackson.annotation.JsonTypeInfo.As;
import com.yugabyte.yw.common.YWServiceException;
import com.yugabyte.yw.common.alerts.AlertReceiverEmailParams;
import com.yugabyte.yw.common.alerts.AlertReceiverParams;
import com.yugabyte.yw.common.alerts.AlertReceiverSlackParams;

import io.ebean.ExpressionList;
import io.ebean.Finder;
import io.ebean.Model;
import io.ebean.annotation.DbJson;
import io.ebean.annotation.EnumValue;
import lombok.Data;
import lombok.EqualsAndHashCode;
import play.data.validation.Constraints;
import play.data.validation.Constraints.Required;

@Data
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

  private static final Finder<UUID, AlertReceiver> find =
      new Finder<UUID, AlertReceiver>(AlertReceiver.class) {};

  public static AlertReceiver create(UUID customerUUID, String name, AlertReceiverParams params) {
    return create(UUID.randomUUID(), customerUUID, name, params);
  }

  public static AlertReceiver create(
      UUID uuid, UUID customerUUID, String name, AlertReceiverParams params) {
    AlertReceiver receiver = new AlertReceiver();
    receiver.uuid = uuid;
    receiver.customerUUID = customerUUID;
    receiver.name = name;
    receiver.params = params;
    receiver.save();
    return receiver;
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

  public static List<AlertReceiver> getOrBadRequest(UUID customerUUID, @Required List<UUID> uuids) {
    ExpressionList<AlertReceiver> query = find.query().where().eq("customer_uuid", customerUUID);
    appendInClause(query, "uuid", uuids);
    List<AlertReceiver> result = query.findList();
    if (result.size() != uuids.size()) {
      // We have incorrect receiver id(s).
      result.forEach(receiver -> uuids.remove(receiver.uuid));
      throw new YWServiceException(
          BAD_REQUEST,
          "Invalid Alert Receiver UUID: "
              + uuids.stream().map(uuid -> uuid.toString()).collect(Collectors.joining(", ")));
    }
    return result;
  }

  public static List<AlertReceiver> list(UUID customerUUID) {
    return find.query().where().eq("customer_uuid", customerUUID).findList();
  }
}
