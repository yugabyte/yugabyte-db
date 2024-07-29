// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.config;

import static com.yugabyte.yw.common.AssertHelper.assertPlatformException;
import static com.yugabyte.yw.common.config.ConfDataType.parseSetMultimap;
import static com.yugabyte.yw.common.config.ConfDataType.parseTagsList;
import static org.junit.Assert.assertTrue;

import com.google.common.collect.*;
import com.yugabyte.yw.common.config.ConfKeyInfo.ConfKeyTags;
import java.util.*;
import org.junit.Test;

public class ConfDataTypeTest {

  @Test
  public void tagListParse() {
    List<ConfKeyTags> list = new ArrayList<>(Arrays.asList(ConfKeyTags.PUBLIC, ConfKeyTags.BETA));
    assertTrue(list.equals(parseTagsList("[\"PUBLIC\",\"BETA\"]")));
    // Strings should be enclosed within double quotes
    assertPlatformException(() -> parseTagsList("[Three,Sample,String]"));
    assertPlatformException(() -> parseTagsList("[\"Invalid\",\"tags\"]"));
  }

  @Test
  public void userTagsValuesListParse() {
    Map<String, Set<String>> testTVMap =
        ImmutableMap.of(
            "yb_task", ImmutableSet.of("task1", "task2"), "yb_dev", ImmutableSet.of("*"));
    SetMultimap<String, String> resultTVMap =
        parseSetMultimap("[\"yb_task:task1\",\"yb_task:task2\",\"yb_dev:*\"]");
    assertTrue(resultTVMap.containsKey("yb_task") && resultTVMap.containsKey("yb_dev"));
    assertTrue(
        Sets.symmetricDifference(testTVMap.get("yb_task"), resultTVMap.get("yb_task")).isEmpty());
    assertPlatformException(
        () -> parseSetMultimap("[\"yb_task:task1:task3\",\"yb_task:task2\",\"yb_dev:*\"]"));
    assertPlatformException(
        () -> parseSetMultimap("[\"yb_task:\",\"yb_task:task2\",\"yb_dev:*\"]"));
    assertPlatformException(() -> parseSetMultimap("[\" :task1\",\"yb_task:task2\",\"yb_dev:*\"]"));
  }
}
