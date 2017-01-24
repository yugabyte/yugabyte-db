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

import com.avaje.ebean.*;
import com.fasterxml.jackson.annotation.JsonBackReference;

import com.yugabyte.yw.common.ApiResponse;
import play.data.validation.Constraints;

import static com.avaje.ebean.Ebean.beginTransaction;
import static com.avaje.ebean.Ebean.commitTransaction;
import static com.avaje.ebean.Ebean.endTransaction;

@Entity
public class Region extends Model {

  @Id
  public UUID uuid;

  @Column(length = 25, nullable = false)
  public String code;

  @Column(length = 100, nullable = false)
  @Constraints.Required
  public String name;

  // The AMI to be used in this region.
  @Constraints.Required
  public String ybImage;

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
   * @param ybImage The YB image id that we need to use for provisioning in this region
   * @return instance of PlacementRegion
   */
  public static Region create(Provider provider, String code, String name, String ybImage) {
    Region region = new Region();
    region.provider = provider;
    region.code = code;
    region.name = name;
    region.ybImage = ybImage;
    region.save();
    return region;
  }

  public static Region get(UUID regionUUID) {
    return find.byId(regionUUID);
  }

  public static Region getByCode(String code) {
    return find.where().eq("code", code).findUnique();
  }

  /**
   * Fetch Regions with the minimum zone count and having a valid yb server image.
   * @param providerUUID
   * @param minZoneCount
   * @return List of PlacementRegion
   */
  public static List<Region> fetchValidRegions(UUID providerUUID, int minZoneCount) {
    String regionQuery
      = " select r.uuid, r.code, r.name"
      + "   from region r left outer join availability_zone zone"
      + "     on zone.region_uuid = r.uuid "
      + "  where r.provider_uuid = :provider_uuid and r.yb_image is not null"
      + "  group by r.uuid "
      + " having count(zone.uuid) >= " + minZoneCount;

    RawSql rawSql = RawSqlBuilder.parse(regionQuery).create();
    Query<Region> query = Ebean.find(Region.class);
    query.setRawSql(rawSql);
    query.setParameter("provider_uuid", providerUUID);
    return query.findList();
  }

  public void disableRegionAndZones() {
    beginTransaction();
    try {
      setActiveFlag(false);
      update();
      String s = "UPDATE availability_zone set active = :active_flag where region_uuid = :region_uuid";
      SqlUpdate updateStmt = Ebean.createSqlUpdate(s);
      updateStmt.setParameter("active_flag", false);
      updateStmt.setParameter("region_uuid", uuid);
      Ebean.execute(updateStmt);
      commitTransaction();
    } catch (Exception e) {
      throw new RuntimeException("Unable to flag Region UUID as deleted: " + uuid);
    } finally {
      endTransaction();
    }
  }
}
