// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models;

import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.fasterxml.jackson.annotation.JsonFormat;
import com.fasterxml.jackson.annotation.JsonIgnore;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.SupportBundleUtil;
import com.yugabyte.yw.common.utils.FileUtils;
import com.yugabyte.yw.forms.SupportBundleFormData;
import com.yugabyte.yw.models.helpers.BundleDetails;
import io.ebean.Finder;
import io.ebean.Model;
import io.ebean.annotation.DbEnumValue;
import io.ebean.annotation.DbJson;
import io.swagger.annotations.ApiModelProperty;
import jakarta.persistence.Column;
import jakarta.persistence.Entity;
import jakarta.persistence.Id;
import jakarta.persistence.Transient;
import java.io.File;
import java.io.InputStream;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.text.ParseException;
import java.util.Date;
import java.util.List;
import java.util.UUID;
import lombok.Getter;
import lombok.Setter;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

@Entity
public class SupportBundle extends Model {

  private static final Logger LOG = LoggerFactory.getLogger(SupportBundle.class);

  @Id
  @Column(nullable = false, unique = true)
  @Getter
  private UUID bundleUUID;

  @Column @Getter @Setter private String path;

  @Column(nullable = false)
  @Getter
  private UUID scopeUUID;

  @Column
  @Getter
  @ApiModelProperty(value = "Support bundle start date.", example = "2022-12-12T13:07:18Z")
  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd'T'HH:mm:ss'Z'")
  private Date startDate;

  @Column
  @Getter
  @ApiModelProperty(value = "Support bundle end date.", example = "2022-12-12T13:07:18Z")
  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd'T'HH:mm:ss'Z'")
  private Date endDate;

  @Column(nullable = true)
  @Getter
  @DbJson
  private BundleDetails bundleDetails;

  @Column(name = "status", nullable = false)
  @Getter
  @Setter
  private SupportBundleStatusType status;

  @JsonIgnore @Setter @Getter private static int retentionDays;

  @Transient
  @ApiModelProperty(value = "Size in bytes of the support bundle", required = false)
  // 0 is the only invalid size.
  private long sizeInBytes;

  public enum SupportBundleStatusType {
    Running("Running"),
    Success("Success"),
    Failed("Failed");

    private final String status;

    private SupportBundleStatusType(String status) {
      this.status = status;
    }

    @DbEnumValue
    public String toString() {
      return this.status;
    }
  }

  public SupportBundle() {}

  public SupportBundle(
      UUID bundleUUID,
      UUID scopeUUID,
      String path,
      Date startDate,
      Date endDate,
      BundleDetails bundleDetails,
      SupportBundleStatusType status) {
    this.bundleUUID = bundleUUID;
    this.scopeUUID = scopeUUID;
    this.path = path;
    this.startDate = startDate;
    this.endDate = endDate;
    this.bundleDetails = bundleDetails;
    this.status = status;
  }

  @JsonIgnore
  public Path getPathObject() {
    return Paths.get(this.path);
  }

  public void setPathObject(Path path) {
    this.path = path.toString();
  }

  public static SupportBundle create(SupportBundleFormData bundleData, Universe universe) {
    SupportBundle supportBundle = new SupportBundle();
    supportBundle.bundleUUID = UUID.randomUUID();
    supportBundle.scopeUUID = universe.getUniverseUUID();
    supportBundle.path = null;
    if (bundleData != null) {
      supportBundle.startDate = bundleData.startDate;
      supportBundle.endDate = bundleData.endDate;
      supportBundle.bundleDetails =
          new BundleDetails(
              bundleData.components, bundleData.maxNumRecentCores, bundleData.maxCoreFileSize);
    }
    supportBundle.status = SupportBundleStatusType.Running;
    supportBundle.save();
    return supportBundle;
  }

  public static final Finder<UUID, SupportBundle> find =
      new Finder<UUID, SupportBundle>(SupportBundle.class) {};

  public static SupportBundle getOrBadRequest(UUID bundleUUID) {
    SupportBundle bundle = get(bundleUUID);
    if (bundle == null) {
      throw new PlatformServiceException(BAD_REQUEST, "Invalid Bundle UUID:" + bundleUUID);
    }
    return bundle;
  }

  public static SupportBundle get(UUID bundleUUID) {
    return find.query().where().eq("bundle_uuid", bundleUUID).findOne();
  }

  public static List<SupportBundle> getAll() {
    List<SupportBundle> supportBundleList = find.query().findList();
    return supportBundleList;
  }

  public static InputStream getAsInputStream(UUID bundleUUID) {
    SupportBundle supportBundle = getOrBadRequest(bundleUUID);
    Path bundlePath = supportBundle.getPathObject();
    File file = bundlePath.toFile();
    return FileUtils.getInputStreamOrFail(file);
  }

  @JsonIgnore
  public String getFileName() {
    Path bundlePath = this.getPathObject();
    if (bundlePath == null) {
      return null;
    }
    return bundlePath.getFileName().toString();
  }

  public Date parseCreationDate() {
    Date creationDate;
    try {
      SupportBundleUtil sbutil = new SupportBundleUtil();
      creationDate = sbutil.getDateFromBundleFileName(this.getFileName());
    } catch (ParseException e) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          String.format(
              "Failed to parse supportBundle filename %s for creation date", this.getFileName()));
    }
    return creationDate;
  }

  @ApiModelProperty(value = "Support bundle creation date.", example = "2022-12-12T13:07:18Z")
  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd'T'HH:mm:ss'Z'")
  public Date getCreationDate() {
    if (this.status != SupportBundleStatusType.Success) {
      return null;
    }
    return this.parseCreationDate();
  }

  @ApiModelProperty(value = "Support bundle expiration date.", example = "2022-12-12T13:07:18Z")
  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd'T'HH:mm:ss'Z'")
  public Date getExpirationDate() {
    if (this.status != SupportBundleStatusType.Success) {
      return null;
    }
    SupportBundleUtil sbutil = new SupportBundleUtil();
    Date expirationDate = sbutil.getDateNDaysAfter(this.parseCreationDate(), getRetentionDays());
    return expirationDate;
  }

  public long getSizeInBytes() {
    if (this.status != SupportBundleStatusType.Success) {
      return 0;
    }

    sizeInBytes = FileUtils.getFileSize(path);
    return sizeInBytes;
  }

  public static List<SupportBundle> getAll(UUID universeUUID) {
    List<SupportBundle> supportBundleList =
        find.query().where().eq("scope_uuid", universeUUID).findList();
    return supportBundleList;
  }

  public static void delete(UUID bundleUUID) {
    SupportBundle supportBundle = SupportBundle.getOrBadRequest(bundleUUID);
    if (supportBundle.getStatus() == SupportBundleStatusType.Running) {
      throw new PlatformServiceException(BAD_REQUEST, "The support bundle is in running state.");
    } else {
      if (supportBundle.delete()) {
        LOG.info("Successfully deleted the db entry for support bundle: " + bundleUUID.toString());
      } else {
        throw new PlatformServiceException(
            INTERNAL_SERVER_ERROR, "Unable to delete the Support Bundle");
      }
    }
  }
}
