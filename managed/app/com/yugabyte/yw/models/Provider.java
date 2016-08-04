// Copyright (c) Yugabyte, Inc.
package com.yugabyte.yw.models;

import java.util.List;
import java.util.UUID;

import javax.persistence.Column;
import javax.persistence.Entity;
import javax.persistence.Id;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.avaje.ebean.Model;

@Entity
public class Provider extends Model {
  public static final Logger LOG = LoggerFactory.getLogger(Provider.class);

  @Id
  public UUID uuid;

  @Column(unique = true, nullable = false)
  public String name;

  @Column(nullable = false, columnDefinition = "boolean default true")
  public Boolean active = true;
  public Boolean isActive() { return active; }
  public void setActiveFlag(Boolean active) { this.active = active; }

  /**
   * Query Helper for Provider with uuid
   */
  public static final Find<UUID, Provider> find = new Find<UUID, Provider>(){};

  /**
   * Create a new Cloud Provider
   * @param name, name of cloud provider
   * @return instance of cloud provider
   */
  public static Provider create(String name)
  {
    Provider provider = new Provider();
    provider.uuid = UUID.randomUUID();
    provider.name = name;
    provider.save();
    return provider;
  }

  public static Provider get(String name) {
    List<Provider> providerList = find.where().eq("name", name).findList();
    // Handle the case when the provider is not found.
    if (providerList == null || providerList.size() == 0) {
      return null;
    }
    // Cannot happen as DB constraint on name is unique.
    if (providerList.size() > 1) {
      LOG.error("Found " + providerList.size() + " providers.");
      System.exit(1);
    }
    return providerList.get(0);
  }
}
