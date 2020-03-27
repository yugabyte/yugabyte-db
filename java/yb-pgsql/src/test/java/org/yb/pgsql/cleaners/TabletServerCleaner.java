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

package org.yb.pgsql.cleaners;

import com.google.common.net.HostAndPort;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.minicluster.MiniYBCluster;

import java.sql.Connection;
import java.util.HashSet;
import java.util.Set;

/**
 * Kills all tablet server processes started after the last "snapshot".
 */
@SuppressWarnings("UnstableApiUsage")
public class TabletServerCleaner implements ClusterCleaner {
  private static final Logger LOG = LoggerFactory.getLogger(TabletServerCleaner.class);

  private final Set<HostAndPort> tserverAddrs = new HashSet<>();

  private MiniYBCluster miniCluster;

  /**
   * Takes a snapshot of the current cluster configuration. Only tablet servers
   * added after this snapshot will be killed on the next cleaning.
   *
   * @param miniCluster - the cluster whose tablet servers we should clean.
   */
  public void snapshot(MiniYBCluster miniCluster) {
    this.miniCluster = miniCluster;

    tserverAddrs.clear();
    tserverAddrs.addAll(miniCluster.getTabletServers().keySet());
  }

  @Override
  public void clean(Connection connection) throws Exception {
    LOG.info("Cleaning-up tablet server processes");

    if (miniCluster == null) {
      return;
    }

    for (HostAndPort addr : miniCluster.getTabletServers().keySet()) {
      if (!tserverAddrs.contains(addr)) {
        LOG.info("Killing tablet server at " + addr.toString());
        miniCluster.killTabletServerOnHostPort(addr);
      }
    }

    miniCluster = null;
  }
}
