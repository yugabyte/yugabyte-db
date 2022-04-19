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
import org.yb.consensus.Metadata;
import org.yb.master.MasterTypes;
import org.yb.CommonTypes;

/**
 * Response for {@link GetMasterRegistrationRequest}.
 */
@InterfaceAudience.Private
public class GetMasterRegistrationResponse extends YRpcResponse {

  private final CommonTypes.PeerRole role;
  private final WireProtocol.ServerRegistrationPB serverRegistration;
  private final WireProtocol.NodeInstancePB instanceId;

  /**
   * Describes a response to a {@link GetMasterRegistrationRequest}, built from
   * {@link Master.GetMasterRegistrationResponsePB}.
   *
   * @param role Master's role in the config.
   * @param serverRegistration server registration (RPC and HTTP addresses) for this master.
   * @param instanceId Node instance (permanent uuid and
   */
  public GetMasterRegistrationResponse(long elapsedMillis, String tsUUID,
                                       CommonTypes.PeerRole role,
                                       WireProtocol.ServerRegistrationPB serverRegistration,
                                       WireProtocol.NodeInstancePB instanceId) {
    super(elapsedMillis, tsUUID);
    this.role = role;
    this.serverRegistration = serverRegistration;
    this.instanceId = instanceId;
  }

  /**
   * Returns this master's role in the config.
   *
   * @see CommonTypes.PeerRole
   * @return Node's role in the cluster, or FOLLOWER if the node is not initialized.
   */
  public CommonTypes.PeerRole getRole() {
    return role;
  }

  /**
   * Returns the server registration (list of RPC and HTTP ports) for this master.
   *
   * @return The {@link WireProtocol.ServerRegistrationPB} object for this master.
   */
  public WireProtocol.ServerRegistrationPB getServerRegistration() {
    return serverRegistration;
  }

  /**
   * The node instance (initial sequence number and permanent uuid) for this master.
   *
   * @return The {@link WireProtocol.NodeInstancePB} object for this master.
   */
  public WireProtocol.NodeInstancePB getInstanceId() {
    return instanceId;
  }

  @Override
  public String toString() {
    return "GetMasterRegistrationResponse{" +
        "role=" + role +
        ", serverRegistration=" + serverRegistration +
        ", instanceId=" + instanceId +
        '}';
  }
}
