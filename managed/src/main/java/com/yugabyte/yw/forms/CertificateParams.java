// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.forms;

import com.yugabyte.yw.common.certmgmt.CertConfigType;
import com.yugabyte.yw.common.kms.util.hashicorpvault.HashicorpVaultConfigParams;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import play.data.validation.Constraints;

/**
 * This class will be used by the API and UI Form Elements to validate constraints for the custom
 * certificate Data.
 */
@ApiModel(
    description =
        "Certificate Params is used to validate constraints for the custom certificate Data")
public class CertificateParams {
  @Constraints.Required() public String label;

  @Constraints.Required() public long certStart;

  @Constraints.Required() public long certExpiry;

  @Constraints.Required() public String certContent;

  @ApiModelProperty(required = false)
  public String keyContent;

  public CertConfigType certType = CertConfigType.SelfSigned;

  /**
   * This is used for params for custom cert path information (on prem) provided by user while
   * creating custom cert entry.
   */
  public static class CustomCertInfo {
    public String nodeCertPath;
    public String nodeKeyPath;
    public String rootCertPath;
    public String clientCertPath;
    public String clientKeyPath;
  }

  @ApiModelProperty(required = false)
  public CustomCertInfo customCertInfo;

  /** This is used for accepting custom server certificates for Node-to-client communication. */
  public static class CustomServerCertData {
    public String serverCertContent;
    public String serverKeyContent;
  }

  @ApiModelProperty(required = false)
  public CustomServerCertData customServerCertData;

  @ApiModelProperty(required = false)
  public HashicorpVaultConfigParams hcVaultCertParams;
}
