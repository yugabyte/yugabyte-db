package org.yb.client;

import java.util.ArrayList;
import java.util.List;
import org.yb.master.MasterReplicationOuterClass;
import org.yb.master.MasterTypes;

public class ListCDCStreamsResponse extends YRpcResponse {
  private final MasterTypes.MasterErrorPB serverError;
  private final List<CDCStreamInfo> streams;

  ListCDCStreamsResponse(
    long elapsedMillis,
    String tsUUID,
    MasterTypes.MasterErrorPB serverError,
    List<MasterReplicationOuterClass.CDCStreamInfoPB> streams) {
    super(elapsedMillis, tsUUID);
    this.serverError = serverError;
    this.streams = new ArrayList<>();
    for (MasterReplicationOuterClass.CDCStreamInfoPB stream: streams) {
      this.streams.add(
        new CDCStreamInfo(
          stream.getStreamId(),
          stream.getTableIdList(),
          stream.getOptionsList(),
          stream.getNamespaceId(),
          stream.getCdcsdkYsqlReplicationSlotName()));
    }
  }

  public boolean hasError() {
    return serverError != null;
  }

  public String errorMessage() {
    if (serverError == null) {
      return "";
    }

    return serverError.getStatus().getMessage();
  }

  public List<CDCStreamInfo> getStreams() {
    return streams;
  }
}
