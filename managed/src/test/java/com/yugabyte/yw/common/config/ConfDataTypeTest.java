package com.yugabyte.yw.common.config;

import static com.yugabyte.yw.common.AssertHelper.assertPlatformException;
import static com.yugabyte.yw.common.config.ConfDataType.parseTagsList;
import static org.junit.Assert.assertTrue;

import com.yugabyte.yw.common.config.ConfKeyInfo.ConfKeyTags;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import org.junit.Test;

public class ConfDataTypeTest {

  @Test
  public void tagListParse() {
    List<ConfKeyTags> list =
        new ArrayList<>(Arrays.asList(ConfKeyTags.PUBLIC, ConfKeyTags.BETA, ConfKeyTags.UIDriven));
    assertTrue(list.equals(parseTagsList("[\"PUBLIC\",\"BETA\",\"UIDriven\"]")));
    // Strings should be enclosed within double quotes
    assertPlatformException(() -> parseTagsList("[Three,Sample,String]"));
    assertPlatformException(() -> parseTagsList("[\"Invalid\",\"tags\"]"));
  }
}
