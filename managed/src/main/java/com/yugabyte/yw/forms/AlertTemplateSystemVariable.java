package com.yugabyte.yw.forms;

import com.fasterxml.jackson.annotation.JsonCreator;
import com.fasterxml.jackson.annotation.JsonFormat;
import com.fasterxml.jackson.annotation.JsonIgnore;
import com.fasterxml.jackson.annotation.JsonProperty;
import lombok.Getter;

@Getter
@JsonFormat(shape = JsonFormat.Shape.OBJECT)
public enum AlertTemplateSystemVariable {
  YUGABYTE_ALERT_STATUS(
      "yugabyte_alert_status",
      "Alert status. Possible values are 'firing' and 'resolved'",
      "$labels.alert_state"),
  YUGABYTE_ALERT_SEVERITY(
      "yugabyte_alert_severity",
      "Alert severity. Possible values are 'WARNING' and 'SEVERE'",
      "$labels.severity"),
  YUGABYTE_ALERT_TYPE("yugabyte_alert_type", "Alert policy template name", "$labels.alert_type"),
  YUGABYTE_ALERT_EXPRESSION(
      "yugabyte_alert_expression",
      "Prometheus expression used to trigger alert",
      "$labels.alert_expression"),
  YUGABYTE_CUSTOMER_CODE("yugabyte_customer_code", "Customer code", "$labels.customer_code"),
  YUGABYTE_CUSTOMER_NAME("yugabyte_customer_name", "Customer name", "$labels.customer_name"),
  YUGABYTE_CUSTOMER_UUID("yugabyte_customer_uuid", "Customer UUID", "$labels.customer_uuid"),
  YUGABYTE_ALERT_SOURCE_UUID(
      "yugabyte_alert_source_UUID", "Alert source UUID", "$labels.source_uuid"),
  YUGABYTE_ALERT_SOURCE_TYPE(
      "yugabyte_alert_source_type", "Alert source type", "$labels.source_type"),
  YUGABYTE_ALERT_SOURCE_NAME(
      "yugabyte_alert_source_name", "Alert source name", "$labels.source_name"),
  YUGABYTE_ALERT_POLICY_TYPE(
      "yugabyte_alert_policy_type",
      "Alert policy type. Possible values are UNIVERSE and PLATFORM",
      "$labels.configuration_type"),
  YUGABYTE_ALERT_POLICY_NAME(
      "yugabyte_alert_policy_name", "Alert policy name", "$labels.definition_name"),
  YUGABYTE_ALERT_POLICY_UUID(
      "yugabyte_alert_policy_uuid", "Alert policy UUID", "$labels.configuration_uuid"),
  YUGABYTE_ALERT_MESSAGE("yugabyte_alert_message", "Alert message", "$annotations.message"),
  YUGABYTE_ALERT_START_TIME(
      "yugabyte_alert_start_time", "Date/time alert fired", "$labels.alert_start_time"),
  YUGABYTE_ALERT_END_TIME(
      "yugabyte_alert_end_time", "Date/time alert resolved", "$labels.alert_end_time"),
  YUGABYTE_ALERT_CHANNEL_NAME(
      "yugabyte_alert_channel_name",
      "Alert notification channel name",
      "$labels.alert_channel_name"),
  YUGABYTE_AFFECTED_NODE_NAMES(
      "yugabyte_affected_node_names",
      "List of node names, affected by particular alert, separated by comma",
      "$annotations.affected_node_names"),
  YUGABYTE_AFFECTED_NODE_ADDRESSES(
      "yugabyte_affected_node_addresses",
      "List of node addresses, affected by particular alert, separated by comma",
      "$annotations.affected_node_addresses"),
  YUGABYTE_AFFECTED_NODE_IDENTIFIERS(
      "yugabyte_affected_node_identifiers",
      "List of onprem node identifiers, affected by particular alert, separated by comma",
      "$annotations.affected_node_identifiers"),
  YUGABYTE_ALERT_LABELS_JSON(
      "yugabyte_alert_labels_json",
      "All alert labels in json format",
      "$labels.alert_labels_json",
      true);
  private final String name;
  private final String description;

  private final boolean jsonObject;

  @JsonIgnore private final String placeholderValue;

  AlertTemplateSystemVariable(String name, String description, String placeholderValue) {
    this(name, description, placeholderValue, false);
  }

  AlertTemplateSystemVariable(
      String name, String description, String placeholderValue, boolean jsonObject) {
    this.name = name;
    this.description = description;
    this.placeholderValue = placeholderValue;
    this.jsonObject = jsonObject;
  }

  @JsonCreator
  public static AlertTemplateSystemVariable forValues(@JsonProperty("name") String name) {
    return AlertTemplateSystemVariable.valueOf(name.toUpperCase());
  }
}
