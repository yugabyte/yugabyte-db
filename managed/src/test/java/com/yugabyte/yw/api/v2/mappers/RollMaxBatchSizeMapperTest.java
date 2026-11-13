// Copyright (c) YugabyteDB, Inc.
package com.yugabyte.yw.api.v2.mappers;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;

import api.v2.mappers.RollMaxBatchSizeMapper;
import api.v2.mappers.UniverseEditGFlagsMapper;
import api.v2.mappers.UniverseResizeNodeParamsMapper;
import api.v2.models.RollMaxBatchSize;
import api.v2.models.UniverseEditGFlags;
import api.v2.models.UniverseResizeNodes;
import com.yugabyte.yw.forms.GFlagsUpgradeParams;
import com.yugabyte.yw.forms.ResizeNodeParams;
import java.math.BigDecimal;
import org.junit.Test;

public class RollMaxBatchSizeMapperTest {

  private static RollMaxBatchSize v2(Integer primary, Integer readReplica) {
    RollMaxBatchSize size = new RollMaxBatchSize();
    if (primary != null) {
      size.setPrimaryBatchSize(BigDecimal.valueOf(primary));
    }
    if (readReplica != null) {
      size.setReadReplicaBatchSize(BigDecimal.valueOf(readReplica));
    }
    return size;
  }

  @Test
  public void testToV1() {
    com.yugabyte.yw.forms.RollMaxBatchSize v1 =
        RollMaxBatchSizeMapper.INSTANCE.toV1RollMaxBatchSize(v2(3, 2));
    assertEquals(Integer.valueOf(3), v1.getPrimaryBatchSize());
    assertEquals(Integer.valueOf(2), v1.getReadReplicaBatchSize());
  }

  @Test
  public void testToV1KeepsV1DefaultsForUnsetSizes() {
    com.yugabyte.yw.forms.RollMaxBatchSize v1 =
        RollMaxBatchSizeMapper.INSTANCE.toV1RollMaxBatchSize(v2(4, null));
    assertEquals(Integer.valueOf(4), v1.getPrimaryBatchSize());
    assertEquals(Integer.valueOf(1), v1.getReadReplicaBatchSize());
  }

  @Test
  public void testToV2() {
    RollMaxBatchSize v2 =
        RollMaxBatchSizeMapper.INSTANCE.toV2RollMaxBatchSize(
            com.yugabyte.yw.forms.RollMaxBatchSize.of(5, 6));
    assertEquals(0, BigDecimal.valueOf(5).compareTo(v2.getPrimaryBatchSize()));
    assertEquals(0, BigDecimal.valueOf(6).compareTo(v2.getReadReplicaBatchSize()));
  }

  @Test
  public void testNull() {
    assertNull(RollMaxBatchSizeMapper.INSTANCE.toV1RollMaxBatchSize(null));
    assertNull(RollMaxBatchSizeMapper.INSTANCE.toV2RollMaxBatchSize(null));
  }

  @Test
  public void testResizeNodesUsesSharedMapper() {
    UniverseResizeNodes req = new UniverseResizeNodes();
    req.setRollMaxBatchSize(v2(2, 3));
    ResizeNodeParams params = new ResizeNodeParams();
    UniverseResizeNodeParamsMapper.INSTANCE.copyToV1ResizeNodeParams(req, params);
    assertEquals(Integer.valueOf(2), params.rollMaxBatchSize.getPrimaryBatchSize());
    assertEquals(Integer.valueOf(3), params.rollMaxBatchSize.getReadReplicaBatchSize());
  }

  @Test
  public void testGFlagsUsesSharedMapper() {
    UniverseEditGFlags req = new UniverseEditGFlags();
    req.setRollMaxBatchSize(v2(2, 3));
    GFlagsUpgradeParams params = new GFlagsUpgradeParams();
    UniverseEditGFlagsMapper.INSTANCE.copyToV1GFlagsUpgradeParams(req, params);
    assertEquals(Integer.valueOf(2), params.rollMaxBatchSize.getPrimaryBatchSize());
    assertEquals(Integer.valueOf(3), params.rollMaxBatchSize.getReadReplicaBatchSize());
  }
}
