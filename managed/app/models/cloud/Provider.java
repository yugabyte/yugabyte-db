// Copyright (c) Yugabyte, Inc.
package models.cloud;

import com.avaje.ebean.Model;
import com.avaje.ebean.annotation.EnumValue;
import javax.persistence.*;
import java.util.UUID;

@Entity
public class Provider extends Model {

	public enum Type {
		@EnumValue("AWS")
		AmazonWebService,

		@EnumValue("GCE")
		GoogleCloud,

		@EnumValue("AZU")
		MicrosoftAzure,
	}

	@Id
	public UUID uuid;

	@Column(unique = true, nullable = false)
	@Enumerated(EnumType.STRING)
	public Type type;

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
	 * @param type, type of cloud provider
	 * @return instance of cloud provider
	 */
	public static Provider create(Type type)
	{
		Provider provider = new Provider();
		provider.uuid = UUID.randomUUID();
		provider.type = type;
		provider.save();
		return provider;
	}
}
