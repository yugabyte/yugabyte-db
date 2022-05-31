// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models;

import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.fasterxml.jackson.annotation.JsonIgnore;
import com.fasterxml.jackson.annotation.JsonInclude;
import com.fasterxml.jackson.databind.annotation.JsonSerialize;
import com.yugabyte.yw.common.PlatformServiceException;
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
        "Access key for the cloud provider. This helps to "
            + "authenticate the user and get access to the provider.")
public class AccessKey extends Model {
  @ApiModel
  @JsonInclude(JsonInclude.Include.NON_NULL)
  public static class KeyInfo {
    @ApiModelProperty public String publicKey;
    @ApiModelProperty public String privateKey;
    @ApiModelProperty public String vaultPasswordFile;
    @ApiModelProperty public String vaultFile;
    @ApiModelProperty public String sshUser;
    @ApiModelProperty public Integer sshPort;
    @ApiModelProperty public boolean airGapInstall = false;
    @ApiModelProperty public boolean passwordlessSudoAccess = true;
    @ApiModelProperty public String provisionInstanceScript = "";
    @ApiModelProperty public boolean installNodeExporter = true;
    @ApiModelProperty public Integer nodeExporterPort = 9300;
    @ApiModelProperty public String nodeExporterUser = "prometheus";
    @ApiModelProperty public boolean skipProvisioning = false;
    @ApiModelProperty public boolean deleteRemote = true;
    @ApiModelProperty public boolean setUpChrony = false;
    @ApiModelProperty public List<String> ntpServers;

    // Indicates whether the provider was created before or after PLAT-3009
    // True if it was created after, else it was created before.
    // Dictates whether or not to show the set up NTP option in the provider UI
    @ApiModelProperty public boolean showSetUpChrony = false;
  }

  public static String getDefaultKeyCode(Provider provider) {
    String sanitizedProviderName = provider.name.replaceAll("\\s+", "-").toLowerCase();
    return String.format(
        "yb-%s-%s_%s-key",
        Customer.get(provider.customerUUID).code, sanitizedProviderName, provider.uuid);
  }

  @ApiModelProperty(required = true)
  @EmbeddedId
  @Constraints.Required
  public AccessKeyId idKey;

  @ApiModelProperty(required = false, hidden = true)
  @JsonIgnore
  public String getKeyCode() {
    return this.idKey.keyCode;
  }

  @ApiModelProperty(required = false, hidden = true)
  @JsonIgnore
  public UUID getProviderUUID() {
    return this.idKey.providerUUID;
  }

  @Constraints.Required
  @Column(nullable = false, columnDefinition = "TEXT")
  @ApiModelProperty(value = "Cloud provider key information", required = true)
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
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR, "Delete unsuccessful for: " + this.idKey);
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
      throw new PlatformServiceException(BAD_REQUEST, "KeyCode not found: " + keyCode);
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
