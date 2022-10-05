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
package org.yb;

import org.yb.annotations.InterfaceAudience;
import org.yb.annotations.InterfaceStability;

/**
 * It represents a secondary index.
 */
@InterfaceAudience.Public
@InterfaceStability.Evolving
final public class IndexInfo {
  private final String tableId;
  private final int version;
  private final boolean isLocal;
  private final boolean isUnique;
  private final String indexedTableId;
  private final CommonTypes.IndexPermissions indexPermissions;

  public IndexInfo(Common.IndexInfoPB indexInfoPB) {
    this.tableId = indexInfoPB.getTableId().toStringUtf8();
    this.version = indexInfoPB.getVersion();
    this.isLocal = indexInfoPB.getIsLocal();
    this.isUnique = indexInfoPB.getIsUnique();
    this.indexedTableId = indexInfoPB.getIndexedTableId().toStringUtf8();
    this.indexPermissions = indexInfoPB.getIndexPermissions();
  }

  public String getTableId() {
    return tableId;
  }

  public int getVersion() {
    return version;
  }

  public boolean isLocal() {
    return isLocal;
  }

  public boolean isUnique() {
    return isUnique;
  }

  public String getIndexedTableId() {
    return indexedTableId;
  }

  public CommonTypes.IndexPermissions getIndexPermissions() {
    return indexPermissions;
  }
}
