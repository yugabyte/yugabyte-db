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

package org.yb.pgsql;

import java.sql.*;
import java.util.*;
import java.util.stream.Collectors;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import static org.yb.AssertionWrappers.*;

public class Tablespace {
  private static final Logger LOG = LoggerFactory.getLogger(Tablespace.class);

  final String name;
  final int numReplicas;
  final List<PlacementBlock> placementBlocks;

  Tablespace(String name, int numReplicas, List<PlacementBlock> placementBlocks) {
    this.name = name;
    this.numReplicas = numReplicas;
    this.placementBlocks = placementBlocks;
  }

  // Creates a Tablespace object from a list of integers. Each integer X becomes a placement
  // block with a cloud, region, and zone of the form cloudX, regionX, zoneX.
  public Tablespace(String name, List<Integer> blocks) {
    this(
      name,
      blocks.size(),
      blocks.stream().map(PlacementBlock::new).collect(Collectors.toList()));
  }

  public String toJson() {
    String placementBlocksJson =
      placementBlocks.stream().map(PlacementBlock::toJson).collect(Collectors.joining(","));

    return String.format(
      "{\"num_replicas\":%d,\"placement_blocks\":[%s]}", numReplicas, placementBlocksJson);
  }

  /**
   * Generate the string representation of this tablespace, e.g.
   * cloud1.region1.zone1,cloud2.region2.zone2,cloud3.region3.zone3
   */
  @Override
  public String toString() {
    return placementBlocks.stream()
      .map(PlacementBlock::toString)
      .collect(Collectors.joining(","));
  }

  public List<Map<String, String>> toPlacementMaps() {
    return placementBlocks.stream()
      .map(PlacementBlock::toPlacementMap)
      .collect(Collectors.toList());
  }

  /** Returns the SQL command to create this tablespace. */
  private String getCreateCmd() {
    return String.format(
      "CREATE TABLESPACE %s WITH (replica_placement='%s')", name, this.toJson());
  }

  /** Executes the command to create this tablespace. */
  public void create(Connection connection) {
    try (Statement setupStatement = connection.createStatement()) {
      setupStatement.execute(this.getCreateCmd());
    } catch (SQLException e) {
      LOG.error("Unexpected exception while creating tablespace: ", e);
      fail("Unexpected exception while creating tablespace");
    }
  }

  /** Executes the command to DROP TABLESPACE IF EXISTS. */
  public void dropIfExists(Connection connection) {
    try (Statement setupStatement = connection.createStatement()) {
      setupStatement.execute(String.format("DROP TABLESPACE IF EXISTS %s", name));
    } catch (SQLException e) {
      LOG.error("Unexpected exception while dropping tablespace: ", e);
      fail("Unexpected exception while dropping tablespace");
    }
  }
}
