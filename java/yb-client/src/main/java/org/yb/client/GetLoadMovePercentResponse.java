// Copyright (c) YugaByte, Inc.

package org.yb.client;

import org.yb.annotations.InterfaceAudience;
import org.yb.master.Master;

@InterfaceAudience.Public
public class GetLoadMovePercentResponse extends YRpcResponse {
  private double percentCompleted;
  private Master.MasterErrorPB serverError;
  private boolean hasRetriableErr = false; 

  GetLoadMovePercentResponse(
      long ellapsedMillis, String masterUUID, double percent,
      Master.MasterErrorPB error) {
    super(ellapsedMillis, masterUUID);
    serverError = error;
    percentCompleted = percent;
    // This check for a specific string can be centralized for all retriable  errors. 
    if (hasError() && serverError.getCode() == Master.MasterErrorPB.Code.IN_TRANSITION_CAN_RETRY) {
      hasRetriableErr = true;
    }
  }

  public double getPercentCompleted() {
    return percentCompleted;
  }

  public boolean hasError() {
    return serverError != null;
  }

  public boolean hasRetriableError() {
    return hasRetriableErr;
  }

  public String errorMessage() {
    if (serverError == null) {
      return "";
    }

    return serverError.getStatus().getMessage();
  }
}
