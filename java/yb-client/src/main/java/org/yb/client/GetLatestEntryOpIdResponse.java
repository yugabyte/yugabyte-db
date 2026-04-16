// Copyright (c) YugabyteDB, Inc.

package org.yb.client;

import java.util.List;

import org.yb.annotations.InterfaceAudience;
import org.yb.cdc.CdcService.CDCErrorPB;
import org.yb.Opid.OpIdPB;

@InterfaceAudience.Public
public class GetLatestEntryOpIdResponse extends YRpcResponse {

  List<OpIdPB> op_ids;
  CDCErrorPB error;
  long bootstrapTime;

  public GetLatestEntryOpIdResponse(long ellapsedMillis, String uuid, List<OpIdPB> ids,
      CDCErrorPB error, long bootstrapTime) {
    super(ellapsedMillis, uuid);
    this.op_ids = ids;
    this.error = error;
    this.bootstrapTime = bootstrapTime;
  }

  public boolean hasError() {
    return (error != null && !error.getStatus().getMessage().isEmpty());
  }

  public List<OpIdPB> getOpIds() {
    return op_ids;
  }

  public String getError() {
    if (error == null) {
      return "";
    }
    return error.getStatus().getMessage();
  }

  public long getBootstrapTime() {
    return bootstrapTime;
  }

}
