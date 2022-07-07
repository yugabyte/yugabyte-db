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
package org.yb.client;

import org.yb.ColumnSchema;
import org.yb.Type;
import org.yb.annotations.InterfaceAudience;
import org.yb.annotations.InterfaceStability;

import static org.yb.master.MasterDdlOuterClass.AlterTableRequestPB;

/**
 * This builder must be used to alter a table. At least one change must be specified.
 */
@InterfaceAudience.Public
@InterfaceStability.Unstable
public class AlterTableOptions {

  AlterTableRequestPB.Builder pb = AlterTableRequestPB.newBuilder();

  /**
   * Change a table's name.
   * @param newName new table's name, must be used to check progress
   * @return this instance
   */
  public AlterTableOptions renameTable(String newName) {
    pb.setNewTableName(newName);
    return this;
  }

  /**
   * Add a new column that's nullable.
   * @param name name of the new column
   * @param type type of the new column
   * @return this instance
   */
  public AlterTableOptions addColumn(String name, Type type) {
    AlterTableRequestPB.Step.Builder step = pb.addAlterSchemaStepsBuilder();
    step.setType(AlterTableRequestPB.StepType.ADD_COLUMN);
    step.setAddColumn(AlterTableRequestPB.AddColumn.newBuilder().setSchema(ProtobufHelper
        .columnToPb(new ColumnSchema.ColumnSchemaBuilder(name, type)
        .nullable(true)
        .build())));
    return this;
  }

  /**
   * Drop a column.
   * @param name name of the column
   * @return this instance
   */
  public AlterTableOptions dropColumn(String name) {
    AlterTableRequestPB.Step.Builder step = pb.addAlterSchemaStepsBuilder();
    step.setType(AlterTableRequestPB.StepType.DROP_COLUMN);
    step.setDropColumn(AlterTableRequestPB.DropColumn.newBuilder().setName(name));
    return this;
  }

  /**
   * Change the name of a column.
   * @param oldName old column's name, must exist
   * @param newName new name to use
   * @return this instance
   */
  public AlterTableOptions renameColumn(String oldName, String newName) {
    AlterTableRequestPB.Step.Builder step = pb.addAlterSchemaStepsBuilder();
    step.setType(AlterTableRequestPB.StepType.RENAME_COLUMN);
    step.setRenameColumn(AlterTableRequestPB.RenameColumn.newBuilder().setOldName(oldName)
        .setNewName(newName));
    return this;
  }
}
