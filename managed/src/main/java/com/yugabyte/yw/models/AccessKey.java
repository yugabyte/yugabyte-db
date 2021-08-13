// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models;

import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.fasterxml.jackson.annotation.JsonBackReference;
import com.yugabyte.yw.common.YWServiceException;
import io.ebean.Finder;
import io.ebean.Model;
import io.ebean.annotation.DbJson;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.List;
import java.util.UUID;
import javax.persistence.Column;
import javax.persistence.EmbeddedId;
import javax.persistence.Entity;
import play.data.validation.Constraints;

@Entity
@ApiModel(
    description =
        "Access key for the provider. This will help to "
            + "authenticate the user and get the access to the cloud provider.")
public class AccessKey extends Model {
  public static class KeyInfo {
    public String publicKey;
    public String privateKey;
    public String vaultPasswordFile;
    public String vaultFile;
    public String sshUser;
    public Integer sshPort;
    public boolean airGapInstall = false;
    public boolean passwordlessSudoAccess = true;
    public String provisionInstanceScript = "";
    public boolean installNodeExporter = true;
    public Integer nodeExporterPort = 9300;
    public String nodeExporterUser = "prometheus";
    public boolean skipProvisioning = false;
  }

  @EmbeddedId @Constraints.Required public AccessKeyId idKey;

  @JsonBackReference
  public String getKeyCode() {
    return this.idKey.keyCode;
  }

  @JsonBackReference
  public UUID getProviderUUID() {
    return this.idKey.providerUUID;
  }

  @Constraints.Required
  @Column(nullable = false, columnDefinition = "TEXT")
  @ApiModelProperty(value = "Cloud provider key info", required = true)
  @DbJson
  private KeyInfo keyInfo;

  public void setKeyInfo(KeyInfo info) {
    this.keyInfo = info;
  }

  public KeyInfo getKeyInfo() {
    return this.keyInfo;
  }

  public static AccessKey create(UUID providerUUID, String keyCode, KeyInfo keyInfo) {
    AccessKey accessKey = new AccessKey();
    accessKey.idKey = AccessKeyId.create(providerUUID, keyCode);
    accessKey.setKeyInfo(keyInfo);
    accessKey.save();
    return accessKey;
  }

  public void deleteOrThrow() {
    if (!super.delete()) {
      throw new YWServiceException(
          INTERNAL_SERVER_ERROR, "Delete unsuccessfull for : " + this.idKey);
    }
  }

  private static final Finder<AccessKeyId, AccessKey> find =
      new Finder<AccessKeyId, AccessKey>(AccessKey.class) {};

  public static AccessKey get(AccessKeyId accessKeyId) {
    return find.byId(accessKeyId);
  }

  public static AccessKey getOrBadRequest(UUID providerUUID, String keyCode) {
    AccessKey accessKey = get(providerUUID, keyCode);
    if (accessKey == null) {
      throw new YWServiceException(BAD_REQUEST, "KeyCode not found: " + keyCode);
    }
    return accessKey;
  }

  @Deprecated
  public static AccessKey get(UUID providerUUID, String keyCode) {
    return find.byId(AccessKeyId.create(providerUUID, keyCode));
  }

  public static List<AccessKey> getAll(UUID providerUUID) {
    return find.query().where().eq("provider_uuid", providerUUID).findList();
  }

  public static List<AccessKey> getAll() {
    return find.query().findList();
  }
}
