// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//
package org.yb.client;

import org.yb.WireProtocol;
import org.yb.annotations.InterfaceAudience;
import org.yb.annotations.InterfaceStability;
import org.yb.rpc.RpcHeader;
import org.yb.tserver.TserverTypes;

/**
 * This exception is thrown by Tablet Servers when something goes wrong processing a request.
 */
@InterfaceAudience.Public
@InterfaceStability.Evolving
@SuppressWarnings("serial")
public class TabletServerErrorException extends YBServerException {
  private TserverTypes.TabletServerErrorPB tserverError = null;

  TabletServerErrorException(String serverUuid, TserverTypes.TabletServerErrorPB error) {
    super(serverUuid, error.getStatus());
    this.tserverError = error;
  }

  TabletServerErrorException(String serverUuid, RpcHeader.ErrorStatusPB errorStatus) {
    super(serverUuid, errorStatus);
  }

  public TserverTypes.TabletServerErrorPB getTServerError() { return tserverError; }
}
