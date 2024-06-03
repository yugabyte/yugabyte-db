// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner;

import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;

import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskDetails;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import org.apache.commons.lang3.StringUtils;
import org.junit.Test;

public class UserTaskDetailsTest {

  @Test
  public void testCreateSubTask() {
    for (SubTaskGroupType stgt : SubTaskGroupType.values()) {
      if (stgt == SubTaskGroupType.Invalid) {
        continue;
      }
      SubTaskDetails details = UserTaskDetails.createSubTask(stgt);
      assertNotNull(details);
      assertFalse(StringUtils.isEmpty(details.getTitle()));
      assertFalse(StringUtils.isEmpty(details.getDescription()));
    }
  }
}
