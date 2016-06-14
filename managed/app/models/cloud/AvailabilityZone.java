// Copyright (c) Yugabyte, Inc.
package models.cloud;

import com.avaje.ebean.Model;
import com.fasterxml.jackson.annotation.JsonBackReference;
import play.data.validation.Constraints;

import javax.persistence.*;
import java.util.UUID;

@Entity
public class AvailabilityZone extends Model {

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
  public static final Find<UUID, AvailabilityZone> find = new Find<UUID,AvailabilityZone>(){};

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
