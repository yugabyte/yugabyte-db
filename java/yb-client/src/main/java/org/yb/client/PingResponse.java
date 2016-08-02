// Copyright (c) YugaByte, Inc.

package org.yb.client;

import org.yb.annotations.InterfaceAudience;

@InterfaceAudience.Public
public class PingResponse extends YRpcResponse {
  PingResponse(long ellapsedMillis, String uuid) {
    super(ellapsedMillis, uuid);
  }
}
