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

package org.yb.cdc;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.client.YBTable;

public class LogClient implements OutputClient {
  long inserts = 0;
  long updates = 0;
  long deletes = 0;
  long writes = 0;
  long snapshotRecords = 0;

  private static final Logger LOG = LoggerFactory.getLogger(LogClient.class);

  @Override
  public void applyChange(YBTable table, CdcService.CDCSDKProtoRecordPB changeRecord) {
    LOG.info(changeRecord.toString());
    switch (changeRecord.getRowMessage().getOp()) {
      case INSERT:
        ++inserts;
        break;
      case UPDATE:
        ++updates;
        break;
      case DELETE:
        ++deletes;
        break;
      case READ:
        ++snapshotRecords;
        break;
    }
    LOG.info(String.format("Inserts: %d, Updates: %d, Deletes: %d, Snapshot Records: %d",
        inserts, updates, deletes, snapshotRecords));
  }
}
