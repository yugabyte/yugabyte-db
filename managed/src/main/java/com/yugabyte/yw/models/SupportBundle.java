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
import io.ebean.annotation.DbJson;
import java.io.File;
import java.io.InputStream;
import java.nio.file.Path;
import java.util.Date;
import java.util.UUID;
import javax.persistence.Column;
import javax.persistence.Entity;
import javax.persistence.Id;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

@Entity
public class SupportBundle extends Model {

  private static final Logger LOG = LoggerFactory.getLogger(SupportBundle.class);

  @Id
  @Column(nullable = false, unique = true)
  public UUID bundleUUID;

  public UUID getBundleUUID() {
    return this.bundleUUID;
  }

  public void setBundleUUID(UUID uuid) {
    this.bundleUUID = uuid;
  }

  @Column(nullable = false)
  public Path path;

  public void setPath(Path path) {
    this.path = path;
  }

  public Path getPath() {
    return this.path;
  }

  @Column(nullable = false)
  public UUID scopeUUID;

  @Column
  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd HH:mm:ss")
  public Date startDate;

  @Column
  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd HH:mm:ss")
  public Date endDate;

  @Column(nullable = true)
  @DbJson
  public BundleDetails bundleDetails;

  public SupportBundle(Universe universe, Path path, SupportBundleFormData bundle) {
    this.bundleUUID = UUID.randomUUID();
    this.path = path;
    if (bundle != null) {
      this.scopeUUID = universe.universeUUID;
      this.startDate = bundle.startDate;
      this.endDate = bundle.endDate;
      this.bundleDetails = new BundleDetails(bundle.components);
    }
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
    return bundlePath.getFileName().toString();
  }
}
