// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models;

import com.avaje.ebean.Model;
import com.avaje.ebean.annotation.DbJson;
import com.avaje.ebean.annotation.EnumValue;
import com.avaje.ebean.annotation.JsonIgnore;
import com.fasterxml.jackson.annotation.JsonBackReference;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.data.validation.Constraints;
import play.libs.Json;

import javax.persistence.Column;
import javax.persistence.Entity;
import javax.persistence.Id;
import java.util.Iterator;
import java.util.List;
import java.util.Map;
import java.util.UUID;

@Entity
public class CustomerConfig extends Model {
  public static final Logger LOG = LoggerFactory.getLogger(CustomerConfig.class);

  public enum ConfigType {
    @EnumValue("STORAGE")
    STORAGE,

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
  public UUID config_uuid;

  @Column(nullable = false)
  public UUID customerUUID;

  @Column(length=25, nullable = false)
  public ConfigType type;

  @Column(length=100, nullable = false)
  public String name;

  @Constraints.Required
  @Column(nullable = false)
  @DbJson
  @JsonIgnore
  public JsonNode data;

  public static final Find<UUID, CustomerConfig> find = new Find<UUID, CustomerConfig>(){};

  public JsonNode getData() {
    // MASK any sensitive data.
    JsonNode maskedData = data.deepCopy();
    for (Iterator<String> it = maskedData.fieldNames(); it.hasNext(); ) {
      String key = it.next();
      if (key.contains("KEY") || key.contains("SECRET")) {
        ((ObjectNode) maskedData).put(key, maskedData.get(key).asText().replaceAll("(?<!^.?).(?!.?$)", "*"));
      }
    }
    return maskedData;
  }

  public static CustomerConfig createWithFormData(UUID customerUUID, JsonNode formData) {
    CustomerConfig customerConfig = Json.fromJson(formData, CustomerConfig.class);
    customerConfig.customerUUID = customerUUID;
    customerConfig.save();
    return customerConfig;
  }

  public static List<CustomerConfig> getAll(UUID customerUUID) {
    return CustomerConfig.find.where().eq("customer_uuid", customerUUID).findList();
  }

  public static CustomerConfig get(UUID customerUUID, UUID configUUID) {
    return CustomerConfig.find.where().eq("customer_uuid", customerUUID).idEq(configUUID).findUnique();
  }
}
