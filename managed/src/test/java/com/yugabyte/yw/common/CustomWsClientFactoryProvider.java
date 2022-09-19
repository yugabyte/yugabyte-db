/*
 * Copyright 2022 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.common;

import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;

import com.google.inject.Inject;
import play.libs.ws.WSClient;

/** Bind to this provider in tests. The provider will return the mock config */
public class CustomWsClientFactoryProvider
    implements com.google.inject.Provider<CustomWsClientFactory> {

  CustomWsClientFactory mockFactory = mock(CustomWsClientFactory.class);

  @Inject
  public CustomWsClientFactoryProvider(WSClient defaultWsClient) {
    when(mockFactory.forCustomConfig(any())).thenReturn(defaultWsClient);
  }

  @Override
  public CustomWsClientFactory get() {
    return mockFactory;
  }
}
