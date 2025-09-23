// Copyright (c) YugabyteDB, Inc.
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

import java.util.List;
import org.yb.annotations.InterfaceAudience;
import org.yb.master.MasterTypes;
import org.yb.util.PeerInfo;
import org.yb.util.ServerInfo;

@InterfaceAudience.Public
public class ListMasterRaftPeersResponse extends YRpcResponse {
  private final MasterTypes.MasterErrorPB error;
  private final List<PeerInfo> peersList;

  ListMasterRaftPeersResponse(
      long ellapsedMillis, String masterUUID, MasterTypes.MasterErrorPB error,
      List<PeerInfo> peersList) {
    super(ellapsedMillis, masterUUID);
    this.error = error;
    this.peersList = peersList;
  }

  public boolean hasError() {
    return error != null;
  }

  public String errorMessage() {
    if (error == null) {
      return "";
    }
    return error.getStatus().getMessage();
  }

  public List<PeerInfo> getPeersList() {
    return peersList;
  }
}
