// Copyright (c) Yugabyte, Inc.
package models.cloud;

import com.avaje.ebean.Model;
import com.fasterxml.jackson.annotation.JsonBackReference;
import play.data.validation.Constraints;

import javax.persistence.*;
import java.util.UUID;

@Entity
public class Region extends Model {

	@Id
	public UUID uuid;

	@Column(length = 25, nullable = false)
	public String code;

	@Column(length = 100, nullable = false)
	@Constraints.Required
	public String name;

	@Constraints.Required
	@Column(nullable = false)
	@ManyToOne
	@JsonBackReference
	public Provider provider;

	@Column(nullable = false, columnDefinition = "boolean default true")
	public Boolean active = true;
	public Boolean isActive() { return active; }
	public void setActiveFlag(Boolean active) { this.active = active; }

	@Column(nullable = false, columnDefinition = "boolean default true")
	public Boolean multi_az_capable = true;
	public Boolean isMultiAZCapable() { return multi_az_capable; }
	public void setMultiAZCapability(Boolean capable) { this.multi_az_capable = capable; }

	/**
	 * Query Helper for Region with region code
	 */
	public static final Find<UUID, Region> find = new Find<UUID, Region>(){};

	/**
	 * Create new instance of Region
	 * @param provider Cloud Provider
	 * @param code Unique Region Code
	 * @param name User Friendly Region Name
	 * @return instance of Region
	 */
	public static Region create(Provider provider, String code, String name, boolean multiAZ) {
		Region region = new Region();
		region.provider = provider;
		region.code = code;
		region.name = name;
		region.multi_az_capable = multiAZ;
		region.save();
		return region;
	}
}
