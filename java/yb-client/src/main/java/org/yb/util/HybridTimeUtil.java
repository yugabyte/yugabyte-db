// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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

import org.yb.annotations.InterfaceAudience;

import java.util.concurrent.TimeUnit;

/**
 * Set of common utility methods to handle HybridTime and related timestamps.
 */
@InterfaceAudience.Private
public class HybridTimeUtil {

  public static final int hybridTimeNumBitsToShift = 12;
  public static final int hybridTimeLogicalBitsMask = (1 << hybridTimeNumBitsToShift) - 1;

  /**
   * Converts the provided timestamp, in the provided unit, to the HybridTime timestamp
   * format. Logical bits are set to 0.
   *
   * @param timestamp the value of the timestamp, must be greater than 0
   * @param timeUnit  the time unit of the timestamp
   * @throws IllegalArgumentException if the timestamp is less than 0
   */
  public static long clockTimestampToHTTimestamp(long timestamp, TimeUnit timeUnit) {
    if (timestamp < 0) {
      throw new IllegalArgumentException("Timestamp cannot be less than 0");
    }
    long timestampInMicros = TimeUnit.MICROSECONDS.convert(timestamp, timeUnit);
    return timestampInMicros << hybridTimeNumBitsToShift;
  }

  /**
   * Extracts the physical and logical values from an HT timestamp.
   *
   * @param htTimestamp the encoded HT timestamp
   * @return a pair of {physical, logical} long values in an array
   */
  public static long[] HTTimestampToPhysicalAndLogical(long htTimestamp) {
    long timestampInMicros = htTimestamp >> hybridTimeNumBitsToShift;
    long logicalValues = htTimestamp & hybridTimeLogicalBitsMask;
    return new long[] {timestampInMicros, logicalValues};
  }

  /**
   * Encodes separate physical and logical components into a single HT timestamp
   *
   * @param physical the physical component, in microseconds
   * @param logical  the logical component
   * @return an encoded HT timestamp
   */
  public static long physicalAndLogicalToHTTimestamp(long physical, long logical) {
    return (physical << hybridTimeNumBitsToShift) + logical;
  }
}
