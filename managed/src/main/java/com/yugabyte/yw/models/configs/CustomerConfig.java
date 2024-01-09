// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.configs;

import static com.yugabyte.yw.models.helpers.CustomerConfigConsts.NAME_AZURE;
import static com.yugabyte.yw.models.helpers.CustomerConfigConsts.NAME_GCS;
import static com.yugabyte.yw.models.helpers.CustomerConfigConsts.NAME_NFS;
import static com.yugabyte.yw.models.helpers.CustomerConfigConsts.NAME_S3;
import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_ONLY;
import static play.mvc.Http.Status.BAD_REQUEST;

import com.fasterxml.jackson.annotation.JsonIgnore;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.CallHomeManager.CollectionLevel;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.models.DrConfig;
import com.yugabyte.yw.models.configs.data.CustomerConfigAlertsPreferencesData;
import com.yugabyte.yw.models.configs.data.CustomerConfigAlertsSmtpInfoData;
import com.yugabyte.yw.models.configs.data.CustomerConfigCallHomeData;
import com.yugabyte.yw.models.configs.data.CustomerConfigData;
import com.yugabyte.yw.models.configs.data.CustomerConfigPasswordPolicyData;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageAzureData;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageGCSData;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageNFSData;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageS3Data;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageS3Data.ProxySetting;
import com.yugabyte.yw.models.helpers.CommonUtils;
import io.ebean.Finder;
import io.ebean.Model;
import io.ebean.annotation.DbJson;
import io.ebean.annotation.Encrypted;
import io.ebean.annotation.EnumValue;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import jakarta.persistence.Column;
import jakarta.persistence.Entity;
import jakarta.persistence.EnumType;
import jakarta.persistence.Enumerated;
import jakarta.persistence.Id;
import java.util.Collection;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Map.Entry;
import java.util.UUID;
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
@Accessors(chain = true)
@EqualsAndHashCode(callSuper = false)
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
    PASSWORD_POLICY;

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
  private UUID configUUID;

  @NotNull
  @Size(min = 1, max = 100)
  @Column(length = 100, nullable = true)
  @ApiModelProperty(value = "Config name", example = "backup20-01-2021")
  private String configName;

  public CustomerConfig setConfigName(String configName) {
    this.configName = configName.trim();
    return this;
  }

  @NotNull
  @Column(nullable = false)
  @ApiModelProperty(value = "Customer UUID", accessMode = READ_ONLY)
  private UUID customerUUID;

  @NotNull
  @Column(length = 25, nullable = false)
  @ApiModelProperty(value = "Config type", example = "STORAGE")
  private ConfigType type;

  @NotNull
  @Size(min = 1, max = 50)
  @Column(length = 100, nullable = false)
  @ApiModelProperty(value = "Name", example = "S3")
  private String name;

  @NotNull
  @Column(nullable = false, columnDefinition = "TEXT")
  @DbJson
  @Encrypted
  @ApiModelProperty(
      value = "Configuration data",
      required = true,
      dataType = "Object",
      example = "{\"AWS_ACCESS_KEY_ID\": \"AK****************ZD\"}")
  private ObjectNode data;

  @ApiModelProperty(
      value = "state of the customerConfig. Possible values are Active, QueuedForDeletion.",
      accessMode = READ_ONLY)
  @Column(nullable = false)
  @Enumerated(EnumType.STRING)
  private ConfigState state = ConfigState.Active;

  public static final Finder<UUID, CustomerConfig> find =
      new Finder<UUID, CustomerConfig>(CustomerConfig.class) {};

  public Map<String, String> dataAsMap() {
    ObjectMapper mapper = new ObjectMapper();
    Map<String, Object> result = mapper.convertValue(getData(), Map.class);
    // Remove not String values.
    Map<String, String> r = new HashMap<>();
    for (Entry<String, Object> entry : result.entrySet()) {
      if (entry.getValue() instanceof String) {
        r.put(entry.getKey(), entry.getValue().toString());
      } else if (entry.getKey().equals("PROXY_SETTINGS")) {
        r.putAll((mapper.convertValue(entry.getValue(), ProxySetting.class)).toMap());
      }
    }

    if (type.equals(ConfigType.STORAGE)
        && name.equals(NAME_AZURE)
        && !r.get("AZURE_STORAGE_SAS_TOKEN").startsWith("?")) {
      r.put("AZURE_STORAGE_SAS_TOKEN", "?" + r.get("AZURE_STORAGE_SAS_TOKEN"));
    }

    return r;
  }

  public CustomerConfig generateUUID() {
    return setConfigUUID(UUID.randomUUID());
  }

  @JsonIgnore
  public ObjectNode getMaskedData() {
    return CommonUtils.maskConfig(getData());
  }

  public CustomerConfig unmaskAndSetData(ObjectNode data) {
    this.setData(CommonUtils.unmaskJsonObject(this.getData(), data));
    return this;
  }

  public static CustomerConfig createWithFormData(UUID customerUUID, JsonNode formData) {
    CustomerConfig customerConfig = createInstance(customerUUID, formData);
    customerConfig.save();
    return customerConfig;
  }

  public static CustomerConfig createInstance(UUID customerUUID, JsonNode formData) {
    CustomerConfig customerConfig = Json.fromJson(formData, CustomerConfig.class);
    customerConfig.setCustomerUUID(customerUUID);
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

  public static CustomerConfig createStorageConfig(
      UUID customerUUID, String name, String configName, JsonNode payload) {
    // We allow overriding name here because this is used by operator.
    CustomerConfig c = createConfig(customerUUID, ConfigType.STORAGE, name, payload);
    c.setConfigName(configName);
    return c;
  }

  /**
   * Creates customer config of the specified type but doesn't save it into DB.
   *
   * @param customerUUID
   * @param type
   * @param name
   * @param payload
   * @return
   */
  private static CustomerConfig createConfig(
      UUID customerUUID, ConfigType type, String name, JsonNode payload) {
    CustomerConfig customerConfig = new CustomerConfig();
    customerConfig.setType(type);
    customerConfig.setName(name);
    customerConfig.setCustomerUUID(customerUUID);
    customerConfig.setData((ObjectNode) payload);
    customerConfig.setConfigName(name + "-Default");
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
    customerConfig.setType(ConfigType.CALLHOME);
    customerConfig.setName(CALLHOME_PREFERENCES);
    customerConfig.setCustomerUUID(customerUUID);
    ObjectNode callhome_json = Json.newObject().put("callhomeLevel", level);
    customerConfig.setData(callhome_json);
    customerConfig.setConfigName("callhome level-Default");
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
      callhomeConfig.setData(Json.newObject().put("callhomeLevel", callhomeLevel));
      callhomeConfig.update();
    }
    return callhomeConfig;
  }

  public void updateState(ConfigState newState) {
    if (this.state == newState) {
      LOG.debug("Invalid State transition as no change requested");
      return;
    }
    if (this.state == ConfigState.QueuedForDeletion) {
      LOG.debug("Invalid State transition {} to {}", this.state, newState);
      return;
    }
    LOG.info("Customer config: transitioned from {} to {}", this.state, newState);
    this.state = newState;
    this.save();
  }

  @JsonIgnore
  public CustomerConfigData getDataObject() {
    Class<? extends CustomerConfigData> expectedClass = getDataClass(getType(), getName());
    if (expectedClass == null) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          String.format(
              "Unknown data type in configuration data. Type %s, name %s.", getType(), getName()));
    }

    ObjectMapper mapper = new ObjectMapper();
    try {
      return mapper.readValue(Json.stringify(getData()), expectedClass);
    } catch (Exception e) {
      throw new PlatformServiceException(BAD_REQUEST, e.getMessage());
    }
  }

  // TODO: Should be removed later if we remove "ObjectNode data" and use
  // @JsonSubTypes.
  public static Class<? extends CustomerConfigData> getDataClass(ConfigType type, String name) {
    if (type == ConfigType.STORAGE) {
      if (NAME_S3.equals(name)) {
        return CustomerConfigStorageS3Data.class;
      } else if (NAME_GCS.equals(name)) {
        return CustomerConfigStorageGCSData.class;
      } else if (NAME_AZURE.equals(name)) {
        return CustomerConfigStorageAzureData.class;
      } else if (NAME_NFS.equals(name)) {
        return CustomerConfigStorageNFSData.class;
      }
    } else if (type == ConfigType.ALERTS) {
      if (ALERTS_PREFERENCES.equals(name)) {
        return CustomerConfigAlertsPreferencesData.class;
      } else if (SMTP_INFO.equals(name)) {
        return CustomerConfigAlertsSmtpInfoData.class;
      }
    } else if (type == ConfigType.CALLHOME) {
      if (CALLHOME_PREFERENCES.equals(name)) {
        return CustomerConfigCallHomeData.class;
      }
    } else if (type == ConfigType.PASSWORD_POLICY) {
      if (PASSWORD_POLICY.equals(name)) {
        return CustomerConfigPasswordPolicyData.class;
      }
    }
    return null;
  }

  @JsonIgnore
  public boolean isStorageConfigUsedForDr() {
    return DrConfig.getByStorageConfigUuid(configUUID).size() != 0;
  }
}
