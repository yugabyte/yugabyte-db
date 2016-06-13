// Copyright (c) Yugabyte, Inc.
package models.cloud;

import com.avaje.ebean.Model;
import play.data.validation.Constraints;

import javax.persistence.*;

@Entity
public class Region extends Model {

	@Id
	@Column(length = 25, nullable = false)
	public String code;

	@Column(length = 100, nullable = false)
	@Constraints.Required
	public String name;

	@Constraints.Required
	@Column(nullable = false)
	@ManyToOne
	public Provider provider;

	@Column(nullable = false, columnDefinition = "boolean default true")
	public Boolean active = true;
	public Boolean isActive() { return active; }
	public void setActiveFlag(Boolean active) { this.active = active; }

	/**
	 * Query Helper for Region with region code
	 */
	public static final Find<String, Region> find = new Find<String, Region>(){};

	/**
	 * Create new instance of Region
	 * @param provider Cloud Provider
	 * @param code Unique Region Code
	 * @param name User Friendly Region Name
	 * @return instance of Region
	 */
	public static Region create(Provider provider, String code, String name) {
		Region region = new Region();
		region.provider = provider;
		region.code = code;
		region.name = name;
		region.save();
		return region;
	}
}
