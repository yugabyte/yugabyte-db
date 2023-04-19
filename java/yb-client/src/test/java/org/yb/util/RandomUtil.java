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

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.List;
import java.util.Random;

public class RandomUtil {
  private static final Logger LOG = LoggerFactory.getLogger(RandomUtil.class);
  private static Random randomGenerator;

  public static Random getRandomGenerator() {
    return randomGenerator;
  }

  private static String NONBLOCKING_RANDOM_DEVICE = "/dev/urandom";

  static {
    long seed = System.nanoTime();
    if (new File(NONBLOCKING_RANDOM_DEVICE).exists()) {
      try {
        InputStream in = new FileInputStream(NONBLOCKING_RANDOM_DEVICE);
        for (int i = 0; i < 64; ++i) {
          seed = seed * 37 + in.read();
        }
        in.close();
      } catch (IOException ex) {
        LOG.warn("Failed to read from " + NONBLOCKING_RANDOM_DEVICE + " to seed random generator");
      }
    }
    randomGenerator = new Random(seed);
  }

  public static int randomNonNegNumber() {
    return randomGenerator.nextInt(Integer.MAX_VALUE);
  }

  public static <T> T getRandomElement(List<T> list) {
    int i = randomGenerator.nextInt(list.size());
    return list.get(i);
  }
}
