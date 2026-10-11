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
  private final boolean xClusterWalAnchor;

  public CDCStreamInfo(ByteString streamId,
                       List<ByteString> tableIds,
                       List<CatalogEntityInfo.CDCStreamOptionsPB> options,
                       ByteString namespaceId,
                       String cdcsdkYsqlReplicationSlotName) {
    this(streamId, tableIds, options, namespaceId, cdcsdkYsqlReplicationSlotName,
        false /* xClusterWalAnchor */);
  }

  public CDCStreamInfo(ByteString streamId,
                       List<ByteString> tableIds,
                       List<CatalogEntityInfo.CDCStreamOptionsPB> options,
                       ByteString namespaceId,
                       String cdcsdkYsqlReplicationSlotName,
                       boolean xClusterWalAnchor) {
    this.streamId = streamId.toStringUtf8();
    this.tableIds = tableIds.stream().map(ByteString::toStringUtf8).collect(Collectors.toList());
    this.options =
        options.stream().collect(
            Collectors.toMap(
                CatalogEntityInfo.CDCStreamOptionsPB::getKey,
                option -> option.getValue().toStringUtf8()));
    this.namespaceId = namespaceId.toStringUtf8();
    this.cdcsdkYsqlReplicationSlotName = cdcsdkYsqlReplicationSlotName;
    this.xClusterWalAnchor = xClusterWalAnchor;
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

  /**
   * Whether this is an xCluster WAL anchor stream. In automatic DDL mode, the source creates one
   * for each new table and deletes it once the target has committed the table's CREATE TABLE.
   */
  public boolean isXClusterWalAnchor() {
    return xClusterWalAnchor;
  }
}
