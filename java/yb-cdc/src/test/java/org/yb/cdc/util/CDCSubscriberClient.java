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

package org.yb.cdc.util;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.cdc.CdcService;
import org.yb.cdc.OutputClient;
import org.yb.client.YBTable;

import java.util.ArrayList;
import java.util.List;

public class CDCSubscriberClient implements OutputClient {
  private static final Logger LOG = LoggerFactory.getLogger(CDCSubscriberClient.class);

  public CDCSubscriberClient(List records) {
    this.records = records;
  }

  public List getRecords() {
    return records;
  }

  private List records = new ArrayList<>();

  @Override
  public void applyChange(YBTable table,
                          CdcService.CDCSDKProtoRecordPB changeRecord) throws Exception {
    records.add(changeRecord);
  }
}
