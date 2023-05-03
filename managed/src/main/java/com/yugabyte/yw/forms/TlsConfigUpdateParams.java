// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import com.fasterxml.jackson.annotation.JsonIgnoreProperties;
import com.fasterxml.jackson.databind.annotation.JsonDeserialize;

@JsonIgnoreProperties(ignoreUnknown = true)
@JsonDeserialize(converter = TlsConfigUpdateParams.Converter.class)
public class TlsConfigUpdateParams extends UpgradeTaskParams {

  public Boolean enableNodeToNodeEncrypt = false;
  public Boolean enableClientToNodeEncrypt = false;

  public Boolean createNewRootCA = false;
  public Boolean selfSignedServerCertRotate = false;

  public Boolean createNewClientRootCA = false;
  public Boolean selfSignedClientCertRotate = false;

  public TlsConfigUpdateParams() {}

  public static class Converter extends BaseConverter<TlsConfigUpdateParams> {}
}
