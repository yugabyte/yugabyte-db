// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.forms.webhook;

import com.yugabyte.yw.forms.AbstractTaskParams;
import com.yugabyte.yw.models.Webhook;

public class WebhookTaskParams extends AbstractTaskParams {
  public Webhook hook;
}
