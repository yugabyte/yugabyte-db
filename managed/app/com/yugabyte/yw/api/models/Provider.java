// Copyright (c) Yugabyte, Inc.
package com.yugabyte.yw.api.models;

import com.avaje.ebean.Model;
import com.avaje.ebean.annotation.EnumValue;
import javax.persistence.*;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;

@Entity
public class Provider extends Model {
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
}
