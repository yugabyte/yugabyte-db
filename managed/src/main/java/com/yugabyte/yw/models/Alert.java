// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models;

import com.fasterxml.jackson.annotation.JsonFormat;
import com.fasterxml.jackson.annotation.JsonIgnore;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.alerts.AlertLabelsProvider;
import com.yugabyte.yw.models.helpers.KnownAlertLabels;
import io.ebean.*;
import io.ebean.annotation.EnumValue;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.data.validation.Constraints;
import play.libs.Json;

import javax.persistence.*;

import java.util.*;

@Entity
public class Alert extends Model implements AlertLabelsProvider {

  /** These are the possible targets for the alert. */
  public enum TargetType {
    @EnumValue("UniverseType")
    UniverseType,

    @EnumValue("BackupType")
    BackupType,

    @EnumValue("ClusterType")
    ClusterType,

    @EnumValue("KMSConfigurationType")
    KMSConfigurationType,

    @EnumValue("NodeType")
    NodeType,

    @EnumValue("ProviderType")
    ProviderType,

    @EnumValue("TableType")
    TableType,

    @EnumValue("TaskType")
    TaskType,

    @EnumValue("CustomerConfigType")
    CustomerConfigType,

    @EnumValue("AlertReceiver")
    AlertReceiverType
  }

  public enum State {
    @EnumValue("CREATED")
    CREATED("FIRING"),
    @EnumValue("ACTIVE")
    ACTIVE("FIRING"),
    @EnumValue("RESOLVED")
    RESOLVED("RESOLVED");

    private final String action;

    State(String action) {
      this.action = action;
    }

    public String getAction() {
      return action;
    }
  }

  @Constraints.Required
  @Id
  @Column(nullable = false, unique = true)
  private UUID uuid;

  @Constraints.Required
  @Column(nullable = false)
  public UUID customerUUID;

  // UUID of the target type if the alert is associated with one.
  public UUID targetUUID;

  // The target type.
  @Enumerated(EnumType.STRING)
  public TargetType targetType;

  @Constraints.Required
  @Column(nullable = false)
  // TODO for now set date time format similar to Date.toString to avoid changing APIs.
  // Need to revisit it as yyyy-MM-dd HH:mm:ss is our typical date time format for APIs
  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "dow mon dd hh:mm:ss zzz yyyy")
  private Date createTime;

  @Constraints.Required
  @Column(columnDefinition = "Text", nullable = false)
  public String errCode;

  @Constraints.Required
  @Column(length = 255)
  public String type;

  @Constraints.Required
  @Column(columnDefinition = "Text", nullable = false)
  public String message;

  @Enumerated(EnumType.STRING)
  private State state;

  @Constraints.Required @JsonIgnore public boolean sendEmail;

  private UUID definitionUUID;

  @OneToMany(mappedBy = "alert", cascade = CascadeType.ALL, orphanRemoval = true)
  private List<AlertLabel> labels;

  public static final Logger LOG = LoggerFactory.getLogger(Alert.class);
  private static final Finder<UUID, Alert> find = new Finder<UUID, Alert>(Alert.class) {};

  public static Alert create(
      UUID customerUUID,
      UUID targetUUID,
      TargetType targetType,
      String errCode,
      String type,
      String message,
      boolean sendEmail,
      UUID definitionUUID,
      List<AlertLabel> labels) {
    Alert alert = new Alert();
    alert.uuid = UUID.randomUUID();
    alert.customerUUID = customerUUID;
    alert.targetUUID = targetUUID;
    alert.targetType = targetType;
    alert.createTime = new Date();
    alert.errCode = errCode;
    alert.type = type;
    alert.message = message;
    alert.sendEmail = sendEmail;
    alert.state = State.CREATED;
    alert.definitionUUID = definitionUUID;
    alert.setLabels(labels);
    alert.save();
    return alert;
  }

  public static Alert create(
      UUID customerUUID,
      UUID targetUUID,
      TargetType targetType,
      String errCode,
      String type,
      String message) {
    return Alert.create(
        customerUUID,
        targetUUID,
        targetType,
        errCode,
        type,
        message,
        false,
        null,
        Collections.emptyList());
  }

  public static Alert create(UUID customerUUID, String errCode, String type, String message) {
    return Alert.create(customerUUID, null, null, errCode, type, message);
  }

  public void update(String newMessage) {
    createTime = new Date();
    message = newMessage;
    save();
  }

  public JsonNode toJson() {
    ObjectNode json =
        Json.newObject()
            .put("uuid", uuid.toString())
            .put("customerUUID", customerUUID.toString())
            .put("createTime", createTime.toString())
            .put("errCode", errCode)
            .put("type", type)
            .put("message", message)
            .put("state", state.name());
    return json;
  }

  public static Boolean exists(String errCode) {
    return find.query().where().eq("err_code", errCode).findCount() != 0;
  }

  public static Boolean exists(String errCode, UUID targetUUID) {
    return find.query().where().eq("err_code", errCode).eq("target_uuid", targetUUID).findCount()
        != 0;
  }

  public static List<Alert> getActiveCustomerAlerts(UUID customerUUID, UUID definitionUUID) {
    return find.query()
        .fetch("labels")
        .where()
        .eq("customer_uuid", customerUUID)
        .in("state", State.CREATED, State.ACTIVE)
        .eq("definition_uuid", definitionUUID)
        .findList();
  }

  public static List<Alert> getActiveCustomerAlertsByTargetUuid(
      UUID customerUUID, UUID targetUUID) {
    return find.query()
        .fetch("labels")
        .where()
        .eq("customer_uuid", customerUUID)
        .in("state", State.CREATED, State.ACTIVE)
        .eq("target_uuid", targetUUID)
        .findList();
  }

  public static List<Alert> list(UUID customerUUID) {
    return find.query()
        .fetch("labels")
        .where()
        .eq("customer_uuid", customerUUID)
        .orderBy("create_time desc")
        .findList();
  }

  public static List<Alert> list(UUID customerUUID, String errCode) {
    return find.query()
        .fetch("labels")
        .where()
        .eq("customer_uuid", customerUUID)
        .eq("errCode", errCode)
        .findList();
  }

  public static List<Alert> listToActivate() {
    return find.query().fetch("labels").where().eq("state", State.CREATED).findList();
  }

  public static List<Alert> listActive(UUID customerUUID) {
    return find.query()
        .fetch("labels")
        .where()
        .eq("customer_uuid", customerUUID)
        .eq("state", State.ACTIVE)
        .orderBy("create_time desc")
        .findList();
  }

  public static List<Alert> listActiveCustomerAlerts(UUID customerUUID) {
    return find.query()
        .fetch("labels")
        .where()
        .eq("customer_uuid", customerUUID)
        .eq("state", State.ACTIVE)
        .eq("err_code", "CUSTOMER_ALERT")
        .findList();
  }

  public static List<Alert> list(UUID customerUUID, String errCode, UUID targetUUID) {
    return find.query()
        .fetch("labels")
        .where()
        .eq("customer_uuid", customerUUID)
        .like("err_code", errCode)
        .eq("target_uuid", targetUUID)
        .orderBy("create_time desc")
        .findList();
  }

  public static Alert get(UUID alertUUID) {
    return find.query().fetch("labels").where().idEq(alertUUID).findOne();
  }

  public static Alert get(UUID customerUUID, UUID targetUUID, TargetType targetType) {
    return find.query()
        .fetch("labels")
        .where()
        .eq("customer_uuid", customerUUID)
        .eq("target_uuid", targetUUID)
        .eq("target_type", targetType)
        .findOne();
  }

  public static List<Alert> get(UUID customerUUID, AlertLabel label) {
    return find.query()
        .fetch("labels")
        .where()
        .eq("customer_uuid", customerUUID)
        .eq("labels.key.name", label.getName())
        .eq("labels.value", label.getValue())
        .findList();
  }

  @Override
  public UUID getUuid() {
    return uuid;
  }

  public List<AlertLabel> getLabels() {
    return labels;
  }

  public String getLabelValue(KnownAlertLabels knownLabel) {
    return getLabelValue(knownLabel.labelName());
  }

  @Override
  public String getLabelValue(String name) {
    // Some dynamic labels.
    if (KnownAlertLabels.ALERT_STATE.labelName().equals(name)) {
      return state.getAction();
    }

    return labels
        .stream()
        .filter(label -> name.equals(label.getName()))
        .map(AlertLabel::getValue)
        .findFirst()
        .orElse(null);
  }

  public void setLabels(List<AlertLabel> labels) {
    if (this.labels == null) {
      this.labels = labels;
    } else {
      // Ebean ORM requires us to update existing loaded field rather than replace it completely.
      this.labels.clear();
      this.labels.addAll(labels);
    }
    this.labels.forEach(label -> label.setAlert(this));
  }

  public State getState() {
    return state;
  }

  public void setState(State state) {
    this.state = state;
  }

  public UUID getDefinitionUUID() {
    return definitionUUID;
  }

  public void setDefinitionUUID(UUID definitionUUID) {
    this.definitionUUID = definitionUUID;
  }

  @Override
  public String toString() {
    return "Alert [uuid="
        + uuid
        + ", customerUUID="
        + customerUUID
        + ", targetUUID="
        + targetUUID
        + ", targetType="
        + targetType
        + ", createTime="
        + createTime
        + ", errCode="
        + errCode
        + ", type="
        + type
        + ", message="
        + message
        + ", state="
        + state
        + ", sendEmail="
        + sendEmail
        + ", definitionUUID="
        + definitionUUID
        + ", labels="
        + labels
        + "]";
  }
}
