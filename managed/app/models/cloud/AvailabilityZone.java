// Copyright (c) Yugabyte, Inc.
package models.cloud;

import com.avaje.ebean.Model;
import play.data.validation.Constraints;

import javax.persistence.*;

@Entity
public class AvailabilityZone extends Model {

	@Id
	@Column(length = 25, nullable = false)
	public String code;

	@Column(length = 100, nullable = false)
	@Constraints.Required
	public String name;

	@Constraints.Required
	@Column(nullable = false)
	@ManyToOne
	public Region region;

	@Column(nullable = false, columnDefinition = "boolean default true")
	public Boolean active = true;
	public Boolean isActive() { return active; }
	public void setActiveFlag(Boolean active) { this.active = active; }

	@Column(length = 50, nullable = false)
	public String subnet;

	/**
	 * Query Helper for Availability Zone with primary key
	 */
  public static final Find<String, AvailabilityZone> find = new Find<String,AvailabilityZone>(){};

	public static AvailabilityZone create(Region region, String code, String name, String subnet) {
		AvailabilityZone az = new AvailabilityZone();
		az.region = region;
		az.code = code;
		az.name = name;
		az.subnet = subnet;
		az.save();
		return az;
	}
}
