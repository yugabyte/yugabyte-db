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
package org.kududb.client;

import org.kududb.WireProtocol;
import org.kududb.annotations.InterfaceAudience;
import org.kududb.annotations.InterfaceStability;
import org.kududb.tserver.Tserver;

/**
 * Wrapper class for a single row error.
 */
@InterfaceAudience.Public
@InterfaceStability.Evolving
public class RowError {
  private final String status;
  private final String message;
  private final Operation operation;
  private final String tsUUID;

  /**
   * Package-private for unit tests.
   */
  RowError(String errorStatus, String errorMessage, Operation operation, String tsUUID) {
    this.status = errorStatus;
    this.message = errorMessage;
    this.operation = operation;
    this.tsUUID = tsUUID;
  }

  /**
   * Get the string-representation of the error code that the tablet server returned.
   * @return A short string representation of the error.
   */
  public String getStatus() {
    return status;
  }

  /**
   * Get the error message the tablet server sent.
   * @return The error message.
   */
  public String getMessage() {
    return message;
  }

  /**
   * Get the Operation that failed.
   * @return The same Operation instance that failed.
   */
  public Operation getOperation() {
    return operation;
  }

  /**
   * Get the identifier of the tablet server that sent the error.
   * @return A string containing a UUID.
   */
  public String getTsUUID() {
    return tsUUID;
  }

  @Override
  public String toString() {
    return "Row error for primary key=" + Bytes.pretty(operation.getRow().encodePrimaryKey()) +
        ", tablet=" + operation.getTablet().getTabletIdAsString() +
        ", server=" + tsUUID +
        ", status=" + status +
        ", message=" + message;
  }

  /**
   * Converts a PerRowErrorPB into a RowError.
   * @param errorPB a row error in its pb format
   * @param operation the original operation
   * @param tsUUID a string containing the originating TS's UUID
   * @return a row error
   */
  static RowError fromRowErrorPb(Tserver.WriteResponsePB.PerRowErrorPB errorPB,
                                 Operation operation, String tsUUID) {
    WireProtocol.AppStatusPB statusPB = errorPB.getError();
    return new RowError(statusPB.getCode().toString(),
        statusPB.getMessage(), operation, tsUUID);
  }
}
