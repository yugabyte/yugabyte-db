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

  @Column
  public double longitude;

  @Column
  public double latitude;

  public void setLatLon(double latitude, double longitude) {
    if (latitude < -90 || latitude > 90) {
      throw new IllegalArgumentException("Invalid Latitude Value, it should be between -90 to 90");
    }
    if (longitude < -180 || longitude > 180) {
      throw new IllegalArgumentException("Invalid Longitude Value, it should be between -180 to 180");
    }

    this.latitude = latitude;
    this.longitude = longitude;
    this.save();
  }

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
