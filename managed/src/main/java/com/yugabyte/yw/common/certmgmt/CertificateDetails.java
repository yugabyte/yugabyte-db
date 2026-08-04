package com.yugabyte.yw.common.certmgmt;

import com.fasterxml.jackson.annotation.JsonProperty;
import lombok.Getter;
import lombok.Setter;

public class CertificateDetails {
  @JsonProperty(CertificateHelper.CLIENT_CERT)
  @Getter
  @Setter
  String crt;

  @JsonProperty(CertificateHelper.CLIENT_KEY)
  @Getter
  @Setter
  String key;
}
