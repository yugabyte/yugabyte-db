// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.forms.webhook;

import java.util.UUID;
import lombok.Getter;
import lombok.Setter;

@Getter
@Setter
public class XClusterWebhookRequestBody extends WebhookRequestBody {
  private UUID drConfigUuid;
  private String drConfigName;
  private String ips;
  private String recordType;
  private int ttl;
}
