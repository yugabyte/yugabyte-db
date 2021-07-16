// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.CallHomeManager.CollectionLevel;
import com.yugabyte.yw.models.helpers.CommonUtils;
import com.yugabyte.yw.common.YWServiceException;
import io.ebean.Finder;
import io.ebean.Model;
import io.ebean.annotation.DbJson;
import io.ebean.annotation.EnumValue;
import io.ebean.annotation.JsonIgnore;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import play.data.validation.Constraints;
import play.libs.Json;

import javax.persistence.Column;
import javax.persistence.Entity;
import javax.persistence.Id;
import java.util.*;

import com.yugabyte.yw.common.Util;
import static play.mvc.Http.Status.*;

import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_ONLY;

@Entity
@ApiModel(value = "Customer Config", description = "Customers Configuration")
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

  @Id
  @ApiModelProperty(value = "Config UUID", accessMode = READ_ONLY)
  public UUID configUUID;

  @Column(length = 100, nullable = true)
  @ApiModelProperty(value = "Config name", example = "backup20-01-2021")
  public String configName;

  @Column(nullable = false)
  @ApiModelProperty(value = "Customer UUID", accessMode = READ_ONLY)
  public UUID customerUUID;

  @Column(length = 25, nullable = false)
  @ApiModelProperty(value = "Config type", example = "STORAGE")
  public ConfigType type;

  @Column(length = 100, nullable = false)
  @ApiModelProperty(value = "Name", example = "S3")
  public String name;

  @Constraints.Required
  @Column(nullable = false, columnDefinition = "TEXT")
  @DbJson
  @JsonIgnore
  @ApiModelProperty(
      value = "Configuration data",
      required = true,
      example = "{\"AWS_ACCESS_KEY_ID\": \"AK****************ZD\"}")
  public JsonNode data;

  public static final Finder<UUID, CustomerConfig> find =
      new Finder<UUID, CustomerConfig>(CustomerConfig.class) {};

  public Map<String, String> dataAsMap() {
    return new ObjectMapper().convertValue(data, Map.class);
  }

  public JsonNode getData() {
    return CommonUtils.maskConfig(data);
  }

  /**
   * Updates configuration data. If some fields are still masked with asterisks then these fields
   * remain unchanged.
   *
   * @param data
   */
  public void setData(JsonNode data) {
    this.data = CommonUtils.unmaskConfig(this.data, data);
  }

  // Returns if there is an in use reference to the object.
  @ApiModelProperty(value = "True if there is an in use reference to the object")
  public boolean getInUse() {
    if (this.type == ConfigType.STORAGE) {
      // Check if a backup or schedule currently has a reference.
      return (Backup.existsStorageConfig(this.configUUID)
          || Schedule.existsStorageConfig(this.configUUID));
    }
    return false;
  }

  @ApiModelProperty(value = "Universe details", example = "{\"name\": \"jd-aws-21-6-21-test4\"}")
  public ArrayNode getUniverseDetails() {
    Set<Universe> universes = new HashSet<>();
    if (this.type == ConfigType.STORAGE) {
      universes = Backup.getAssociatedUniverses(this.configUUID);
    }
    return Util.getUniverseDetails(universes);
  }

  @Deprecated
  @Override
  public boolean delete() {
    if (!this.getInUse()) {
      return super.delete();
    }
    return false;
  }

  public void deleteOrThrow() {
    if (!delete()) {
      throw new YWServiceException(
          INTERNAL_SERVER_ERROR, "Customer Configuration could not be deleted.");
    }
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

  @Deprecated
  public static CustomerConfig get(UUID customerUUID, UUID configUUID) {
    return CustomerConfig.find
        .query()
        .where()
        .eq("customer_uuid", customerUUID)
        .idEq(configUUID)
        .findOne();
  }

  public static CustomerConfig getOrBadRequest(UUID customerUUID, UUID configUUID) {
    CustomerConfig storageConfig = get(customerUUID, configUUID);
    if (storageConfig == null) {
      throw new YWServiceException(BAD_REQUEST, "Invalid StorageConfig UUID: " + configUUID);
    }
    return storageConfig;
  }

  public static CustomerConfig get(UUID configUUID) {
    return CustomerConfig.find.query().where().idEq(configUUID).findOne();
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
    customerConfig.data = payload;
    customerConfig.save();
    return customerConfig;
  }

  public static CustomerConfig getAlertConfig(UUID customerUUID) {
    return getConfig(customerUUID, ConfigType.ALERTS, ALERTS_PREFERENCES);
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
}
