// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models;

import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_ONLY;

import com.fasterxml.jackson.annotation.JsonIgnore;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.CallHomeManager.CollectionLevel;
import com.yugabyte.yw.models.helpers.CommonUtils;
import io.ebean.Finder;
import io.ebean.Model;
import io.ebean.annotation.DbJson;
import io.ebean.annotation.EnumValue;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.Collection;
import java.util.Collections;
import java.util.List;
import java.util.Map;
import java.util.Map.Entry;
import java.util.UUID;
import java.util.stream.Collectors;
import javax.persistence.Column;
import javax.persistence.Entity;
import javax.persistence.EnumType;
import javax.persistence.Enumerated;
import javax.persistence.Id;
import javax.persistence.Transient;
import javax.validation.constraints.NotNull;
import javax.validation.constraints.Size;
import lombok.Data;
import lombok.EqualsAndHashCode;
import lombok.experimental.Accessors;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.util.CollectionUtils;
import play.libs.Json;

@Entity
@Data
@EqualsAndHashCode(callSuper = false)
@Accessors(chain = true)
@ApiModel(
    description =
        "Customer configuration. Includes storage, alerts, password policy, and call-home level.")
public class CustomerConfig extends Model {
  public static final Logger LOG = LoggerFactory.getLogger(CustomerConfig.class);
  public static final String ALERTS_PREFERENCES = "preferences";
  public static final String SMTP_INFO = "smtp info";
  public static final String PASSWORD_POLICY = "password policy";
  public static final String CALLHOME_PREFERENCES = "callhome level";

  public enum ConfigType {
    @EnumValue("STORAGE")
    STORAGE,

    @EnumValue("ALERTS")
    ALERTS,

    @EnumValue("CALLHOME")
    CALLHOME,

    @EnumValue("PASSWORD_POLICY")
    PASSWORD_POLICY,

    // TODO: move metric and other configs to this table as well.
    @EnumValue("OTHER")
    OTHER;

    public static boolean isValid(String type) {
      for (ConfigType t : ConfigType.values()) {
        if (t.name().equals(type)) {
          return true;
        }
      }

      return false;
    }
  }

  public enum ConfigState {
    @EnumValue("Active")
    Active,

    @EnumValue("QueuedForDeletion")
    QueuedForDeletion
  }

  @Id
  @ApiModelProperty(value = "Config UUID", accessMode = READ_ONLY)
  public UUID configUUID;

  @NotNull
  @Size(min = 1, max = 100)
  @Column(length = 100, nullable = true)
  @ApiModelProperty(value = "Config name", example = "backup20-01-2021")
  public String configName;

  @NotNull
  @Column(nullable = false)
  @ApiModelProperty(value = "Customer UUID", accessMode = READ_ONLY)
  public UUID customerUUID;

  @NotNull
  @Column(length = 25, nullable = false)
  @ApiModelProperty(value = "Config type", example = "STORAGE")
  public ConfigType type;

  @NotNull
  @Size(min = 1, max = 50)
  @Column(length = 100, nullable = false)
  @ApiModelProperty(value = "Name", example = "S3")
  public String name;

  @NotNull
  @Column(nullable = false, columnDefinition = "TEXT")
  @DbJson
  @ApiModelProperty(
      value = "Configuration data",
      required = true,
      dataType = "Object",
      example = "{\"AWS_ACCESS_KEY_ID\": \"AK****************ZD\"}")
  public ObjectNode data;

  @ApiModelProperty(
      value = "state of the customerConfig. Possible values are Active, QueuedForDeletion.",
      accessMode = READ_ONLY)
  @Column(nullable = false)
  @Enumerated(EnumType.STRING)
  private ConfigState state = ConfigState.Active;

  public static final Finder<UUID, CustomerConfig> find =
      new Finder<UUID, CustomerConfig>(CustomerConfig.class) {};

  public Map<String, String> dataAsMap() {
    Map<String, String> result = new ObjectMapper().convertValue(data, Map.class);
    // Remove not String values.
    for (Entry<String, String> entry : result.entrySet()) {
      if (!(entry.getValue() instanceof String)) {
        result.remove(entry.getKey());
      }
    }
    return result;
  }

  public CustomerConfig generateUUID() {
    return setConfigUUID(UUID.randomUUID());
  }

  public ObjectNode getData() {
    return data;
  }

  @Transient
  @JsonIgnore
  public ObjectNode getMaskedData() {
    return CommonUtils.maskConfig(data);
  }

  public CustomerConfig setConfigName(String configName) {
    this.configName = configName.trim();
    return this;
  }

  /**
   * Updates configuration data. If some fields are still masked with asterisks then these fields
   * remain unchanged.
   *
   * @param data
   */
  public CustomerConfig setData(ObjectNode data) {
    this.data = data;
    return this;
  }

  public CustomerConfig unmaskAndSetData(ObjectNode data) {
    this.data = CommonUtils.unmaskJsonObject(this.data, data);
    return this;
  }

  public static CustomerConfig createWithFormData(UUID customerUUID, JsonNode formData) {
    CustomerConfig customerConfig = Json.fromJson(formData, CustomerConfig.class);
    customerConfig.customerUUID = customerUUID;
    customerConfig.save();
    return customerConfig;
  }

  public static List<CustomerConfig> getAll(UUID customerUUID) {
    return CustomerConfig.find.query().where().eq("customer_uuid", customerUUID).findList();
  }

  public static CustomerConfig get(UUID customerUUID, UUID configUUID) {
    return CustomerConfig.find
        .query()
        .where()
        .eq("customer_uuid", customerUUID)
        .idEq(configUUID)
        .findOne();
  }

  public static CustomerConfig get(UUID configUUID) {
    return CustomerConfig.find.query().where().idEq(configUUID).findOne();
  }

  public static CustomerConfig get(UUID customerUUID, String configName) {
    return CustomerConfig.find
        .query()
        .where()
        .eq("customer_uuid", customerUUID)
        .eq("config_name", configName)
        .findOne();
  }

  public static CustomerConfig createAlertConfig(UUID customerUUID, JsonNode payload) {
    return createConfig(customerUUID, ConfigType.ALERTS, ALERTS_PREFERENCES, payload);
  }

  public static CustomerConfig createSmtpConfig(UUID customerUUID, JsonNode payload) {
    return createConfig(customerUUID, ConfigType.ALERTS, SMTP_INFO, payload);
  }

  public static CustomerConfig createPasswordPolicyConfig(UUID customerUUID, JsonNode payload) {
    return createConfig(customerUUID, ConfigType.PASSWORD_POLICY, PASSWORD_POLICY, payload);
  }

  public static CustomerConfig createConfig(
      UUID customerUUID, ConfigType type, String name, JsonNode payload) {
    CustomerConfig customerConfig = new CustomerConfig();
    customerConfig.type = type;
    customerConfig.name = name;
    customerConfig.customerUUID = customerUUID;
    customerConfig.data = (ObjectNode) payload;
    customerConfig.save();
    return customerConfig;
  }

  public static CustomerConfig getAlertConfig(UUID customerUUID) {
    return getConfig(customerUUID, ConfigType.ALERTS, ALERTS_PREFERENCES);
  }

  public static List<CustomerConfig> getAlertConfigs(Collection<UUID> customerUUIDs) {
    return getConfigs(customerUUIDs, ConfigType.ALERTS, ALERTS_PREFERENCES);
  }

  public static CustomerConfig getSmtpConfig(UUID customerUUID) {
    return getConfig(customerUUID, ConfigType.ALERTS, SMTP_INFO);
  }

  public static CustomerConfig getPasswordPolicyConfig(UUID customerUUID) {
    return getConfig(customerUUID, ConfigType.PASSWORD_POLICY, PASSWORD_POLICY);
  }

  public static CustomerConfig getConfig(UUID customerUUID, ConfigType type, String name) {
    return CustomerConfig.find
        .query()
        .where()
        .eq("customer_uuid", customerUUID)
        .eq("type", type.toString())
        .eq("name", name)
        .findOne();
  }

  public static List<CustomerConfig> getConfigs(
      Collection<UUID> customerUUIDs, ConfigType type, String name) {
    if (CollectionUtils.isEmpty(customerUUIDs)) {
      return Collections.emptyList();
    }
    return CustomerConfig.find
        .query()
        .where()
        .in("customer_uuid", customerUUIDs)
        .eq("type", type.toString())
        .eq("name", name)
        .findList();
  }

  public static List<CustomerConfig> getAllStorageConfigsQueuedForDeletion(UUID customerUUID) {
    List<CustomerConfig> configList =
        CustomerConfig.find
            .query()
            .where()
            .eq("customer_uuid", customerUUID)
            .eq("type", ConfigType.STORAGE)
            .eq("state", ConfigState.QueuedForDeletion)
            .findList();
    return configList;
  }

  public static CustomerConfig createCallHomeConfig(UUID customerUUID) {
    return createCallHomeConfig(customerUUID, "MEDIUM");
  }

  public static CustomerConfig createCallHomeConfig(UUID customerUUID, String level) {
    CustomerConfig customerConfig = new CustomerConfig();
    customerConfig.type = ConfigType.CALLHOME;
    customerConfig.name = CALLHOME_PREFERENCES;
    customerConfig.customerUUID = customerUUID;
    ObjectNode callhome_json = Json.newObject().put("callhomeLevel", level);
    customerConfig.data = callhome_json;
    customerConfig.save();
    return customerConfig;
  }

  public static CustomerConfig getCallhomeConfig(UUID customerUUID) {
    return getConfig(customerUUID, ConfigType.CALLHOME, CALLHOME_PREFERENCES);
  }

  public static CollectionLevel getOrCreateCallhomeLevel(UUID customerUUID) {
    CustomerConfig callhomeConfig = CustomerConfig.getCallhomeConfig(customerUUID);
    if (callhomeConfig == null) CustomerConfig.createCallHomeConfig(customerUUID);
    return CustomerConfig.getCallhomeLevel(customerUUID);
  }

  public static CollectionLevel getCallhomeLevel(UUID customerUUID) {
    CustomerConfig config = getCallhomeConfig(customerUUID);
    if (config != null) {
      return CollectionLevel.valueOf(config.getData().get("callhomeLevel").textValue());
    }
    return null;
  }

  public static CustomerConfig upsertCallhomeConfig(UUID customerUUID, String callhomeLevel) {
    CustomerConfig callhomeConfig = CustomerConfig.getCallhomeConfig(customerUUID);
    if (callhomeConfig == null) {
      callhomeConfig = CustomerConfig.createCallHomeConfig(customerUUID, callhomeLevel);
    } else {
      callhomeConfig.data = Json.newObject().put("callhomeLevel", callhomeLevel);
      callhomeConfig.update();
    }
    return callhomeConfig;
  }

  public ConfigState getState() {
    return this.state;
  }

  public void setState(ConfigState newState) {
    if (this.state == newState) {
      LOG.debug("Invalid State transition as no change requested");
      return;
    }
    if (this.state == ConfigState.QueuedForDeletion) {
      LOG.debug("Invalid State transition {} to {}", this.state, newState);
      return;
    }
    this.state = newState;
    save();
  }
}
