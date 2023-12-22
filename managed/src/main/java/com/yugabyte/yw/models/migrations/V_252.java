/*
 * Copyright 2022 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.models.migrations;

import com.fasterxml.jackson.annotation.JsonBackReference;
import com.fasterxml.jackson.annotation.JsonInclude;
import com.fasterxml.jackson.annotation.JsonManagedReference;
import com.yugabyte.yw.cloud.PublicCloudConstants.Architecture;
import com.yugabyte.yw.commissioner.Common.CloudType;
import io.ebean.Finder;
import io.ebean.Model;
import io.ebean.annotation.DbJson;
import io.ebean.annotation.Encrypted;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import javax.persistence.CascadeType;
import javax.persistence.Column;
import javax.persistence.Entity;
import javax.persistence.Id;
import javax.persistence.ManyToOne;
import javax.persistence.OneToMany;
import javax.persistence.Table;
import lombok.Data;
import lombok.EqualsAndHashCode;
import lombok.Getter;
import lombok.Setter;

/** Snapshot View of ORM entities at the time migration V_252 was added. */
public class V_252 {

  @Entity
  @Table(name = "customer")
  public static class Customer extends Model {

    public UUID uuid;

    @Id public Long id;

    public static final Finder<UUID, Customer> find = new Finder<UUID, Customer>(Customer.class) {};
  }

  @Entity
  @Table(name = "provider")
  @Getter
  @Setter
  public static class Provider extends Model {

    @Id public UUID uuid;

    @Column(name = "customer_uuid", nullable = false)
    public UUID customerUUID;

    @Column(nullable = false)
    public String code;

    public String name;

    @Column(nullable = false, columnDefinition = "TEXT")
    @Encrypted
    @DbJson
    public ProviderDetails details = new ProviderDetails();

    @OneToMany(cascade = CascadeType.ALL)
    @JsonManagedReference(value = "provider-regions")
    public List<Region> regions;

    @OneToMany(cascade = CascadeType.ALL)
    @JsonManagedReference(value = "provider-image-bundles")
    public List<ImageBundle> imageBundles;

    public static final Finder<UUID, Provider> find = new Finder<UUID, Provider>(Provider.class) {};

    public static List<Provider> getAll(UUID customerUUID) {
      return find.query().where().eq("customer_uuid", customerUUID).findList();
    }

    public static Provider get(UUID customerUUID, UUID providerUUID) {
      return find.query().where().eq("customer_uuid", customerUUID).idEq(providerUUID).findOne();
    }

    public CloudType getCloudCode() {
      return CloudType.valueOf(this.code);
    }
  }

  @Entity
  @Table(name = "region")
  @Getter
  @Setter
  public static class Region extends Model {

    @Id public UUID uuid;

    @Column(nullable = false)
    public String code;

    @Column(nullable = false)
    public String name;

    @Column(nullable = false)
    @ManyToOne
    @JsonBackReference("provider-regions")
    public Provider provider;

    @Encrypted
    @DbJson
    @Column(columnDefinition = "TEXT")
    public RegionDetails details = new RegionDetails();

    public static final Finder<UUID, Region> find = new Finder<UUID, Region>(Region.class) {};

    public static Region getByCode(Provider provider, String code) {
      return find.query().where().eq("provider_UUID", provider.uuid).eq("code", code).findOne();
    }
  }

  public static class ProviderDetails {
    public String sshUser;
    public Integer sshPort = 22;
  }

  @Data
  @EqualsAndHashCode(callSuper = false)
  @JsonInclude(JsonInclude.Include.NON_NULL)
  public static class RegionDetails {
    public RegionCloudInfo cloudInfo;
  }

  @Data
  @JsonInclude(JsonInclude.Include.NON_NULL)
  public static class RegionCloudInfo {
    public AWSRegionCloudInfo aws;
    public AzureRegionCloudInfo azu;
    public GCPRegionCloudInfo gcp;
  }

  @Data
  @EqualsAndHashCode(callSuper = false)
  public static class AWSRegionCloudInfo {
    public String ybImage;
    public Architecture arch;
  }

  @Data
  @EqualsAndHashCode(callSuper = false)
  public static class AzureRegionCloudInfo {
    public String ybImage;
  }

  @Data
  @EqualsAndHashCode(callSuper = false)
  public static class GCPRegionCloudInfo {
    public String ybImage;
  }

  @Entity
  @Table(name = "image_bundle")
  public static class ImageBundle extends Model {
    @Id public UUID uuid;

    public String name;

    @ManyToOne
    @JsonBackReference("provider-image-bundles")
    public Provider provider;

    @DbJson public ImageBundleDetails details = new ImageBundleDetails();

    @Column(name = "is_default")
    public boolean useAsDefault = false;

    public static final Finder<UUID, ImageBundle> find =
        new Finder<UUID, ImageBundle>(ImageBundle.class) {};

    public static ImageBundle create(
        Provider provider, String name, ImageBundleDetails details, boolean isDefault) {
      ImageBundle bundle = new ImageBundle();
      bundle.provider = provider;
      bundle.name = name;
      bundle.details = details;
      bundle.useAsDefault = isDefault;
      bundle.save();
      return bundle;
    }

    public static ImageBundle getDefaultForProvider(UUID providerUUID) {
      return find.query()
          .where()
          .eq("provider_uuid", providerUUID)
          .eq("is_default", true)
          .findOne();
    }
  }

  @Data
  @EqualsAndHashCode(callSuper = false)
  @JsonInclude(JsonInclude.Include.NON_NULL)
  public static class ImageBundleDetails {
    private String globalYbImage;
    private Architecture arch;
    private Map<String, BundleInfo> regions;

    @Data
    @JsonInclude(JsonInclude.Include.NON_NULL)
    public static class BundleInfo {
      private String ybImage;
      private String sshUserOverride;
      private Integer sshPortOverride;
    }
  }
}
