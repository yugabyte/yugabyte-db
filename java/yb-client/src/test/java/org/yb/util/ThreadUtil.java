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
package org.yb.util;

import java.util.Collection;

import org.apache.commons.lang3.ThreadUtils;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

/**
 * Utility class for stuff related to Java threads.
 */
public class ThreadUtil {
  private static final Logger LOG = LoggerFactory.getLogger(ThreadUtil.class);

  /**
   * Get thread groups, threads and their respective stack trace details of all threads except those
   * in system thread group (finalizer, etc.).
   */
  public static String getAllThreadsInfo() {
    // Get all "user" (non-system) root-ish thread groups.
    // System thread group is not very interesting.
    Collection<ThreadGroup> nonSystemRootThreadGroups = ThreadUtils.findThreadGroups(
        ThreadUtils.getSystemThreadGroup(),
        false /* recurse */,
        ThreadUtils.ALWAYS_TRUE_PREDICATE);
    StringBuilder sb = new StringBuilder();
    for (ThreadGroup threadGroup : nonSystemRootThreadGroups) {
      sb.append(getAllThreadsInfo(threadGroup));
    }
    return sb.toString();
  }

  /**
   * Get thread groups, threads and their respective stack trace details of the given thread group,
   * recursively traversing child thread groups.
   */
  public static String getAllThreadsInfo(ThreadGroup threadGroup) {
    StringBuilder sb = new StringBuilder();
    sb.append(">> Thread group '" + threadGroup.getName() + "'\n\n");

    Thread[] threads = // Slightly grow the array size
        new Thread[(int) (threadGroup.activeCount() * 1.5) + 1];
    int threadsCount = threadGroup.enumerate(threads, false /* recurse */);
    if (threadsCount == threads.length) {
      LOG.warn("Some threads might've been omitted!"); // Very unlikely to happen
    }
    for (int i = 0; i < threadsCount; ++i) {
      sb.append("> Thread '" + threads[i].getName() + "' (id = " + threads[i].getId() + ")\n");
      for (StackTraceElement frame : threads[i].getStackTrace()) {
        sb.append("\tat " + frame + "\n");
      }
      sb.append("\n");
    }

    ThreadGroup[] innerThreadGroups = // Slightly grow the array size
        new ThreadGroup[(int) (threadGroup.activeGroupCount() * 1.5) + 1];
    int threadGroupsCount = threadGroup.enumerate(innerThreadGroups, false /* recurse */);
    if (threadGroupsCount == innerThreadGroups.length) {
      LOG.warn("Some thread groups might've been omitted!"); // Very unlikely to happen
    }
    for (int i = 0; i < threadGroupsCount; ++i) {
      sb.append(getAllThreadsInfo(innerThreadGroups[i]).trim());
      sb.append("\n");
    }

    return sb.toString().trim();
  }
}
