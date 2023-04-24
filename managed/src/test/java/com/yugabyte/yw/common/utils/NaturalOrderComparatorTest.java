// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.utils;

import static org.junit.Assert.assertEquals;

import java.util.Arrays;
import java.util.Collections;
import java.util.List;
import junitparams.JUnitParamsRunner;
import junitparams.Parameters;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;

@RunWith(JUnitParamsRunner.class)
public class NaturalOrderComparatorTest {

  NaturalOrderComparator naturalOrderCmp;

  @Before
  public void setup() {
    naturalOrderCmp = new NaturalOrderComparator();
  }

  @Test
  @Parameters
  public void testNaturalSort(List<String> input, List<String> expectedOutput) {
    Collections.sort(input, naturalOrderCmp);
    assertEquals(input, expectedOutput);
  }

  private Object[] parametersForTestNaturalSort() {
    return new Object[] {
      new Object[] {
        Arrays.asList("hook1", "hook11", "hook2"), Arrays.asList("hook1", "hook2", "hook11")
      },
      new Object[] {
        Arrays.asList("a1b1c4", "a1b1c3", "a1b2c7", "c2b3d3", "c0b7d8", "a1d0c0", "a2b0c0"),
        Arrays.asList("a1b1c3", "a1b1c4", "a1b2c7", "a1d0c0", "a2b0c0", "c0b7d8", "c2b3d3")
      },
      new Object[] {
        Arrays.asList("abbb", "abbbbbb", "abcccc", "abc", "bbbb"),
        Arrays.asList("abbb", "abbbbbb", "abc", "abcccc", "bbbb")
      },
      new Object[] {
        Arrays.asList("12zfb", "zba12", "abc2de", "1b1b", "2b2t"),
        Arrays.asList("1b1b", "2b2t", "12zfb", "abc2de", "zba12")
      }
    };
  }
}
