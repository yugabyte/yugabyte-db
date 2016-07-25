// Copyright (c) Yugabyte, Inc.
package com.yugabyte.yw.models;

import java.util.List;
import java.util.Set;
import java.util.UUID;

import javax.persistence.Column;
import javax.persistence.Entity;
import javax.persistence.Id;
import javax.persistence.ManyToOne;
import javax.persistence.OneToMany;

import com.avaje.ebean.Ebean;
import com.avaje.ebean.Model;
import com.avaje.ebean.Query;
import com.avaje.ebean.RawSql;
import com.avaje.ebean.RawSqlBuilder;
import com.fasterxml.jackson.annotation.JsonBackReference;

import play.data.validation.Constraints;

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

  @JsonBackReference
  @OneToMany
  private Set<AvailabilityZone> zones;

  @Column(nullable = false, columnDefinition = "boolean default true")
  public Boolean active = true;
  public Boolean isActive() { return active; }
  public void setActiveFlag(Boolean active) { this.active = active; }

  /**
   * Query Helper for PlacementRegion with region code
   */
  public static final Find<UUID, Region> find = new Find<UUID, Region>(){};

  /**
   * Create new instance of PlacementRegion
   * @param provider Cloud Provider
   * @param code Unique PlacementRegion Code
   * @param name User Friendly PlacementRegion Name
   * @return instance of PlacementRegion
   */
  public static Region create(Provider provider, String code, String name) {
    Region region = new Region();
    region.provider = provider;
    region.code = code;
    region.name = name;
    region.save();
    return region;
  }

  public static Region get(UUID regionUUID) {
    return find.byId(regionUUID);
  }

  /**
   * Fetch Regions with Specific Zone Count
   * @param providerUUID
   * @param minZoneCount
   * @return List of PlacementRegion
   */
  public static List<Region> fetchRegionsWithZoneCount(UUID providerUUID, int minZoneCount) {
    String regionQuery
      = " select r.uuid, r.code, r.name"
      + "   from region r left outer join availability_zone zone"
      + "     on zone.region_uuid = r.uuid "
      + "  where r.provider_uuid = :provider_uuid"
      + "  group by r.uuid "
      + " having count(zone.uuid) >= " + minZoneCount;

    RawSql rawSql = RawSqlBuilder.parse(regionQuery).create();
    Query<Region> query = Ebean.find(Region.class);
    query.setRawSql(rawSql);
    query.setParameter("provider_uuid", providerUUID);
    return query.findList();
  }
}
