// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.yugabyte.yw.common.AccessManager;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.models.AccessKey;
import com.yugabyte.yw.models.Provider;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.Collections;
import java.util.List;
import org.apache.commons.lang3.ObjectUtils;
import play.data.validation.Constraints;

@ApiModel
public class AccessKeyFormData {

  @ApiModelProperty(required = true)
  @Constraints.Required()
  public String keyCode;

  @ApiModelProperty(required = true)
  public AccessManager.KeyType keyType;

  @ApiModelProperty(required = true)
  public String keyContent;

  // to be set by user explicitly from UI during key creation
  @ApiModelProperty(notes = "Set access key expiration date N days out")
  public Integer expirationThresholdDays;

  @ApiModelProperty(hidden = true)
  public String sshUser;

  @ApiModelProperty(hidden = true)
  public Integer sshPort;

  // Not used anymore. This field should be passed directly to the pre-provision script.
  @ApiModelProperty(hidden = true)
  @Deprecated
  public Boolean passwordlessSudoAccess;

  @ApiModelProperty(hidden = true)
  public Boolean airGapInstall;

  @ApiModelProperty(hidden = true)
  public Boolean installNodeExporter;

  @ApiModelProperty(hidden = true)
  public String nodeExporterUser;

  @ApiModelProperty(hidden = true)
  public Integer nodeExporterPort;

  @ApiModelProperty(hidden = true)
  public Boolean skipProvisioning;

  @ApiModelProperty(hidden = true)
  public Boolean setUpChrony;

  @ApiModelProperty(hidden = true)
  public List<String> ntpServers;

  @ApiModelProperty(hidden = true)
  public Boolean skipKeyValidateAndUpload;

  // Indicates whether the provider was created before or after PLAT-3009.
  // True if it was created after, else it was created before.
  // This should be true so that all new providers are marked as true by default.
  @ApiModelProperty(hidden = true)
  public Boolean showSetUpChrony;

  public AccessKeyFormData setOrValidateRequestDataWithExistingKey(Provider provider) {
    // fill missing access key params from provider details
    // But we want to do this only if there was access key created already.
    if (!AccessKey.getAll(provider.getUuid()).isEmpty()) {
      this.sshUser = setOrValidate(this.sshUser, provider.getDetails().sshUser, "sshUser");
      this.sshPort = setOrValidate(this.sshPort, provider.getDetails().sshPort, "sshPort");
      this.nodeExporterUser =
          setOrValidate(
              this.nodeExporterUser, provider.getDetails().nodeExporterUser, "nodeExporterUser");
      this.nodeExporterPort =
          setOrValidate(
              this.nodeExporterPort, provider.getDetails().nodeExporterPort, "nodeExporterPort");

      // These fields are not required during creating a new access key. This information
      // will be missing during rotation. Therefore, copying the information present in the latest
      // access key(for the first access key creation information will be populated during provider
      // creation, which will be passed on during subsequent creation).
      this.airGapInstall = provider.getDetails().airGapInstall;
      this.skipProvisioning = provider.getDetails().skipProvisioning;
      this.setUpChrony = provider.getDetails().setUpChrony;
      this.ntpServers = provider.getDetails().ntpServers;
      this.showSetUpChrony = provider.getDetails().showSetUpChrony;
      this.passwordlessSudoAccess = provider.getDetails().passwordlessSudoAccess;
      this.installNodeExporter = provider.getDetails().installNodeExporter;
    }
    if (sshPort == null) sshPort = 22;

    if (passwordlessSudoAccess == null) passwordlessSudoAccess = true;

    if (airGapInstall == null) airGapInstall = false;

    if (installNodeExporter == null) installNodeExporter = true;

    if (nodeExporterUser == null) nodeExporterUser = "prometheus";

    if (nodeExporterPort == null) nodeExporterPort = 9300;

    if (skipProvisioning == null) skipProvisioning = false;

    if (setUpChrony == null) setUpChrony = false;

    if (ntpServers == null) ntpServers = Collections.emptyList();

    if (showSetUpChrony == null) showSetUpChrony = true;

    if (skipKeyValidateAndUpload == null) skipKeyValidateAndUpload = false;

    return this;
  }

  // for objects - set or fail if not equal
  private static <T> T setOrValidate(T formParam, T providerKeyParam, String param) {
    if (formParam == null) {
      return providerKeyParam;
    } else if (providerKeyParam == null) {
      return formParam;
    } else if (ObjectUtils.notEqual(formParam, providerKeyParam)) {
      // if provider data was already set (during previous key creation) and request data is
      // not matching
      throw new PlatformServiceException(
          BAD_REQUEST,
          "Request parameters do not match with existing keys of the provider. Alter param: "
              + param
              + " Request Paramter: "
              + formParam
              + " provider Parameter: "
              + providerKeyParam);
    }
    // were equal
    return formParam;
  }
}
