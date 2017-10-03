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
package org.kududb.client;

import org.kududb.WireProtocol;
import org.kududb.annotations.InterfaceAudience;
import org.kududb.annotations.InterfaceStability;
import org.kududb.rpc.RpcHeader;

/**
 * This class is used for errors sent in response to a RPC.
 */
@InterfaceAudience.Public
@InterfaceStability.Evolving
@SuppressWarnings("serial")
public class KuduServerException extends KuduException {

  KuduServerException(String serverUuid, RpcHeader.ErrorStatusPB errorStatus) {
    this(serverUuid, errorStatus.getMessage(), errorStatus.getCode().toString(),
        errorStatus.getCode().getNumber(), null);
  }

  KuduServerException(String serverUuid, WireProtocol.AppStatusPB appStatus) {
    this(serverUuid, appStatus.getMessage(), appStatus.getCode().toString(),
        appStatus.getCode().getNumber(), null);
  }

  KuduServerException(String serverUuid, String message, String errorDesc,
                      int errCode, Throwable cause) {
    super("Server[" + serverUuid + "] "
        + errorDesc + "[code " + errCode + "]: "  + message, cause);
  }
}