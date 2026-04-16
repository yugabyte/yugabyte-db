// Copyright (c) YugabyteDB, Inc.

package org.yb.client;

import java.util.List;
import org.yb.annotations.InterfaceAudience;
import org.yb.tserver.Tserver.ListTabletsResponsePB.StatusAndSchemaPB;
import org.yb.tserver.TserverTypes.TabletServerErrorPB;

@InterfaceAudience.Public
public class ListTabletsResponse extends YRpcResponse {

  // Info of tablets on this tserver.
  List<StatusAndSchemaPB> statusAndSchemas;
  TabletServerErrorPB error;

  public ListTabletsResponse(
      long ellapsedMillis, String uuid, List<StatusAndSchemaPB> statusAndSchemaPBs,
      TabletServerErrorPB error) {
    super(ellapsedMillis, uuid);
    this.statusAndSchemas = statusAndSchemaPBs;
    this.error = error;
  }

  public boolean hasError() {
    return (error != null && !error.getStatus().getMessage().isEmpty());
  }

  public String getError() {
    if (error == null) {
      return "";
    }
    return error.getStatus().getMessage();
  }

  public List<StatusAndSchemaPB> getStatusAndSchemaPBs() {
    return statusAndSchemas;
  }

}
