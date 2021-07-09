package com.yugabyte.yw.common;

import com.fasterxml.jackson.annotation.JsonProperty;

public class CertificateDetails {
  @JsonProperty("yugabytedb.crt")
  String crt;

  @JsonProperty("yugabytedb.key")
  String key;
}
