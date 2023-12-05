// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.fasterxml.jackson.annotation.JsonFormat;
import com.yugabyte.yw.common.PlatformServiceException;
import io.ebean.Finder;
import io.ebean.Model;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import io.swagger.annotations.ApiModelProperty.AccessMode;
import jakarta.persistence.Column;
import jakarta.persistence.Entity;
import jakarta.persistence.Id;
import java.util.Date;
import java.util.UUID;
import lombok.Getter;
import lombok.Setter;
import play.data.validation.Constraints;

@Entity
@ApiModel(
    description =
        "Customer Licenses. This helps customer to "
            + "upload licenses for the thirdparrty softwares if required.")
@Getter
@Setter
public class CustomerLicense extends Model {

  @ApiModelProperty(value = "License UUID", accessMode = AccessMode.READ_ONLY)
  @Id
  private UUID licenseUUID;

  @ApiModelProperty(
      value = "Customer UUID that owns this license",
      accessMode = AccessMode.READ_WRITE)
  @Column(nullable = false)
  private UUID customerUUID;

  @Constraints.Required
  @Column(nullable = false)
  @ApiModelProperty(value = "License File Path", required = true)
  private String license;

  @Constraints.Required
  @Column(nullable = false)
  @ApiModelProperty(value = "Type of the license", required = true)
  private String licenseType;

  @Column(nullable = false)
  @ApiModelProperty(
      value = "Creation date of license",
      required = false,
      accessMode = AccessMode.READ_ONLY,
      example = "2022-12-12T13:07:18Z")
  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd'T'HH:mm:ss'Z'")
  @Getter
  private Date creationDate;

  public void setCreationDate() {
    this.setCreationDate(new Date());
  }

  public static CustomerLicense create(UUID customerUUID, String license, String licenseType) {
    CustomerLicense cLicense = new CustomerLicense();
    cLicense.setLicenseUUID(UUID.randomUUID());
    cLicense.setCustomerUUID(customerUUID);
    cLicense.setLicenseType(licenseType);
    cLicense.setLicense(license);
    cLicense.setCreationDate();
    cLicense.save();
    return cLicense;
  }

  public static final Finder<UUID, CustomerLicense> find =
      new Finder<UUID, CustomerLicense>(CustomerLicense.class) {};

  public static CustomerLicense get(UUID licenseUUID) {
    return find.byId(licenseUUID);
  }

  public static CustomerLicense getOrBadRequest(UUID licenseUUID) {
    CustomerLicense cLicense = get(licenseUUID);
    if (cLicense == null) {
      throw new PlatformServiceException(BAD_REQUEST, "License not found: " + licenseUUID);
    }
    return cLicense;
  }

  public static CustomerLicense getOrBadRequest(UUID customerUUID, UUID licenseUUID) {
    CustomerLicense customerLicense =
        find.query().where().idEq(licenseUUID).eq("customer_uuid", customerUUID).findOne();
    if (customerLicense == null) {
      throw new PlatformServiceException(BAD_REQUEST, "License not found: " + licenseUUID);
    }
    return customerLicense;
  }
}
