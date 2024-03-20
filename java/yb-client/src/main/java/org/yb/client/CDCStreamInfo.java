package org.yb.client;

import com.google.protobuf.ByteString;
import java.util.List;
import java.util.Map;
import java.util.stream.Collectors;
import org.yb.master.CatalogEntityInfo;

final public class CDCStreamInfo {
  private final String streamId;
  private final List<String> tableIds;
  private final Map<String, String> options;
  private final String namespaceId;
  private final String cdcsdkYsqlReplicationSlotName;

  public CDCStreamInfo(ByteString streamId,
                       List<ByteString> tableIds,
                       List<CatalogEntityInfo.CDCStreamOptionsPB> options,
                       ByteString namespaceId,
                       String cdcsdkYsqlReplicationSlotName) {
    this.streamId = streamId.toStringUtf8();
    this.tableIds = tableIds.stream().map(ByteString::toStringUtf8).collect(Collectors.toList());
    this.options =
        options.stream().collect(
            Collectors.toMap(
                CatalogEntityInfo.CDCStreamOptionsPB::getKey,
                option -> option.getValue().toStringUtf8()));
    this.namespaceId = namespaceId.toStringUtf8();
    this.cdcsdkYsqlReplicationSlotName = cdcsdkYsqlReplicationSlotName;
  }

  public String getStreamId() {
    return streamId;
  }

  public List<String> getTableIds() {
    return tableIds;
  }

  public Map<String, String> getOptions() {
    return options;
  }

  public String getNamespaceId() {
    return namespaceId;
  }

  public String getCdcsdkYsqlReplicationSlotName() {
    return  cdcsdkYsqlReplicationSlotName;
  }
}
