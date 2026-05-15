// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.api.v2.mappers;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import api.v2.mappers.UniverseDetailSubsetMapper;
import api.v2.models.UniverseDetailSubset;
import com.yugabyte.yw.common.Util;
import java.time.Instant;
import java.time.ZoneOffset;
import java.util.UUID;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class UniverseDetailSubsetMapperTest {

  @Test
  public void toV2_mapsFields() {
    UUID u = UUID.randomUUID();
    Util.UniverseDetailSubset src =
        Util.UniverseDetailSubset.builder()
            .uuid(u)
            .name("my-uni")
            .updateInProgress(true)
            .updateSucceeded(false)
            .creationDate(99_000L)
            .universePaused(true)
            .build();

    UniverseDetailSubset dst = UniverseDetailSubsetMapper.INSTANCE.toV2(src);

    assertEquals(u, dst.getUuid());
    assertEquals("my-uni", dst.getName());
    assertTrue(dst.getUpdateInProgress());
    assertFalse(dst.getUpdateSucceeded());
    assertEquals(Instant.ofEpochMilli(99_000L).atOffset(ZoneOffset.UTC), dst.getCreationDate());
    assertTrue(dst.getUniversePaused());
  }
}
