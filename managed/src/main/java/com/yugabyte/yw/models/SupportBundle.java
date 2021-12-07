// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.fasterxml.jackson.annotation.JsonFormat;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.models.helpers.BundleDetails;
import com.yugabyte.yw.forms.SupportBundleFormData;
import io.ebean.Finder;
import io.ebean.Model;
import io.ebean.annotation.DbEnumValue;
import io.ebean.annotation.DbJson;
import java.io.File;
import java.io.InputStream;
import java.nio.file.Path;
import java.util.Date;
import java.util.UUID;
import javax.persistence.Column;
import javax.persistence.Entity;
import javax.persistence.Id;
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

  @Column @Getter @Setter private Path path;

  @Column(nullable = false)
  @Getter
  private UUID scopeUUID;

  @Column
  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd HH:mm:ss")
  private Date startDate;

  @Column
  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd HH:mm:ss")
  private Date endDate;

  @Column(nullable = true)
  @DbJson
  private BundleDetails bundleDetails;

  @Column(name = "status", nullable = false)
  @Getter
  @Setter
  private SupportBundleStatusType status;

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

  public static SupportBundle create(SupportBundleFormData bundleData, Universe universe) {
    SupportBundle supportBundle = new SupportBundle();
    supportBundle.bundleUUID = UUID.randomUUID();
    supportBundle.scopeUUID = universe.universeUUID;
    supportBundle.path = null;
    if (bundleData != null) {
      supportBundle.startDate = bundleData.startDate;
      supportBundle.endDate = bundleData.endDate;
      supportBundle.bundleDetails = new BundleDetails(bundleData.components);
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

  public static InputStream getAsInputStream(UUID bundleUUID) {
    SupportBundle supportBundle = getOrBadRequest(bundleUUID);
    Path bundlePath = supportBundle.getPath();
    File file = bundlePath.toFile();
    InputStream is = Util.getInputStreamOrFail(file);
    return is;
  }

  public String getFileName() {
    Path bundlePath = this.getPath();
    if (bundlePath == null) {
      return null;
    }
    return bundlePath.getFileName().toString();
  }
}
