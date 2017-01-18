// Copyright (c) Yugabyte, Inc.
package com.yugabyte.yw.models;

import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;

import javax.persistence.Column;
import javax.persistence.Entity;
import javax.persistence.Id;
import javax.persistence.Table;
import javax.persistence.UniqueConstraint;

import com.avaje.ebean.annotation.DbJson;
import com.fasterxml.jackson.annotation.JsonBackReference;
import com.fasterxml.jackson.databind.JsonNode;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.avaje.ebean.Model;
import play.data.validation.Constraints;
import play.libs.Json;


@Table(
  uniqueConstraints =
  @UniqueConstraint(columnNames = {"customer_uuid", "code"})
)

@Entity
public class Provider extends Model {
  public static final Logger LOG = LoggerFactory.getLogger(Provider.class);

  @Id
  public UUID uuid;

  @Column(nullable = false)
  public String code;

  @Column(nullable = false)
  public String name;

  @Column(nullable = false, columnDefinition = "boolean default true")
  public Boolean active = true;
  public Boolean isActive() { return active; }
  public void setActiveFlag(Boolean active) { this.active = active; }

  @Column(nullable = false)
  public UUID customerUUID;

  @Constraints.Required
  @Column(nullable = false)
  @DbJson
  @JsonBackReference
  private JsonNode config;

  public void setConfig(Map<String, String> configMap) { this.config = Json.toJson(configMap); }

  @JsonBackReference
  public Map<String, String> getConfig() { return Json.fromJson(this.config, Map.class); }

  /**
   * Query Helper for Provider with uuid
   */
  public static final Find<UUID, Provider> find = new Find<UUID, Provider>(){};

  /**
   * Create a new Cloud Provider
   * @param customerUUID, customer uuid
   * @param code, code of cloud provider
   * @param name, name of cloud provider
   * @return instance of cloud provider
   */
  public static Provider create(UUID customerUUID, String code, String name)
  {
    return create(customerUUID, code, name, new HashMap<String, String>());
  }

  /**
   * Create a new Cloud Provider
   * @param customerUUID, customer uuid
   * @param code, code of cloud provider
   * @param name, name of cloud provider
   * @param config, Map of cloud provider configuration
   * @return instance of cloud provider
   */
  public static Provider create(UUID customerUUID, String code, String name, Map<String, String> config)
  {
    Provider provider = new Provider();
    provider.customerUUID = customerUUID;
    provider.uuid = UUID.randomUUID();
    provider.code = code;
    provider.name = name;
    provider.setConfig(config);
    provider.save();
    return provider;
  }

  /**
   * Query provider based on customer uuid and provider uuid
   * @param customerUUID, customer uuid
   * @param providerUUID, cloud provider uuid
   * @return instance of cloud provider.
   */
  public static Provider get(UUID customerUUID, UUID providerUUID) {
    return find.where().eq("customer_uuid", customerUUID).idEq(providerUUID).findUnique();
  }

  /**
   * Get all the providers for a given customer uuid
   * @param customerUUID, customer uuid
   * @return list of cloud providers. 
   */
  public static List<Provider> getAll(UUID customerUUID) {
    return find.where().eq("customer_uuid", customerUUID).findList();
  }

  /**
   * Get Provider by name for a given customer uuid. If there is multiple
   * providers with the same name, it will raise a exception.
   * @param customerUUID
   * @param name
   * @return
   */
  public static Provider get(UUID customerUUID, String name) {
    List<Provider> providerList = find.where().eq("customer_uuid", customerUUID)
            .eq("name", name).findList();
    int size = providerList.size();
    
    if (size == 0) {
      return null;
    } else if (size > 1) {
        throw new RuntimeException("Found " + size + " providers with name: " + name);
    }
    return providerList.get(0);
  }

  public static Provider get(UUID providerUuid) {
    return find.byId(providerUuid);
  }
}
