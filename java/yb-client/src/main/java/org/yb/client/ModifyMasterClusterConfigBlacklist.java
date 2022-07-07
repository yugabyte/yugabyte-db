// Copyright (c) YugaByte, Inc.
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

import java.util.Comparator;
import java.util.List;
import java.util.TreeSet;

import org.yb.annotations.InterfaceAudience;
import org.yb.CommonNet.HostPortPB;
import org.yb.master.CatalogEntityInfo;

@InterfaceAudience.Public
public class ModifyMasterClusterConfigBlacklist extends AbstractModifyMasterClusterConfig {
  private List<HostPortPB> addHosts;
  private List<HostPortPB> removeHosts;
  private boolean isLeaderBlacklist;


  // This constructor is retained for backward compatibility.
  public ModifyMasterClusterConfigBlacklist(YBClient client, List<HostPortPB> modifyHosts,
      boolean isAdd) {
    this(client, modifyHosts, isAdd, false);
  }

  // This constructor is retained for backward compatibility.
  public ModifyMasterClusterConfigBlacklist(YBClient client, List<HostPortPB> modifyHosts,
      boolean isAdd, boolean isLeaderBlacklist) {
    super(client);
    if (isAdd) {
      this.addHosts = modifyHosts;
    } else {
      this.removeHosts = modifyHosts;
    }
    this.isLeaderBlacklist = isLeaderBlacklist;
  }

  public ModifyMasterClusterConfigBlacklist(YBClient client, List<HostPortPB> addHosts,
      List<HostPortPB> removeHosts) {
    this(client, addHosts, removeHosts, false);
  }

  public ModifyMasterClusterConfigBlacklist(YBClient client, List<HostPortPB> addHosts,
      List<HostPortPB> removeHosts, boolean isLeaderBlacklist) {
    super(client);
    this.addHosts = addHosts;
    this.removeHosts = removeHosts;
    this.isLeaderBlacklist = isLeaderBlacklist;
  }

  @Override
  protected CatalogEntityInfo.SysClusterConfigEntryPB modifyConfig(
      CatalogEntityInfo.SysClusterConfigEntryPB config) {
    // Modify the blacklist.
    CatalogEntityInfo.SysClusterConfigEntryPB.Builder configBuilder =
        CatalogEntityInfo.SysClusterConfigEntryPB.newBuilder(config);

    // Use a TreeSet so we can prune duplicates while keeping HostPortPB as storage.
    TreeSet<HostPortPB> finalHosts =
      new TreeSet<HostPortPB>(new Comparator<HostPortPB>() {
      @Override
      public int compare(HostPortPB a, HostPortPB b) {
        if (a.getHost().equals(b.getHost())) {
          int portA = a.getPort();
          int portB = b.getPort();
          if (portA < portB) {
            return -1;
          } else if (portA == portB) {
            return 0;
          } else {
            return 1;
          }
        } else {
          return a.getHost().compareTo(b.getHost());
        }
      }
    });
    // Add up the current list.
    if (isLeaderBlacklist) {
      finalHosts.addAll(config.getLeaderBlacklist().getHostsList());
    } else {
      finalHosts.addAll(config.getServerBlacklist().getHostsList());
    }
    if (addHosts != null) {
      finalHosts.addAll(addHosts);
    }
    if (removeHosts != null) {
      finalHosts.removeAll(removeHosts);
    }
    // Change the blacklist in the local config copy.
    CatalogEntityInfo.BlacklistPB blacklist =
        CatalogEntityInfo.BlacklistPB.newBuilder().addAllHosts(finalHosts).build();

    if (isLeaderBlacklist) {
      configBuilder.setLeaderBlacklist(blacklist);
    } else {
      configBuilder.setServerBlacklist(blacklist);
    }
    return configBuilder.build();
  }
}
