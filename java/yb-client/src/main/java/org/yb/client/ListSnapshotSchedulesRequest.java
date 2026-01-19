package org.yb.client;

import com.google.protobuf.Message;
import io.netty.buffer.ByteBuf;
import java.util.ArrayList;
import java.util.List;
import java.util.UUID;
import org.yb.annotations.InterfaceAudience;
import org.yb.master.MasterBackupOuterClass.ListSnapshotSchedulesRequestPB;
import org.yb.master.MasterBackupOuterClass.ListSnapshotSchedulesResponsePB;
import org.yb.master.MasterBackupOuterClass.SnapshotInfoPB;
import org.yb.master.MasterBackupOuterClass.SnapshotScheduleFilterPB;
import org.yb.master.MasterBackupOuterClass.SnapshotScheduleInfoPB;
import org.yb.master.MasterBackupOuterClass.SnapshotScheduleOptionsPB;
import org.yb.master.MasterBackupOuterClass.TableIdentifiersPB;
import org.yb.master.MasterTypes;
import org.yb.util.CommonUtil;
import org.yb.util.Pair;
import org.yb.util.SnapshotUtil;
import org.yb.CommonTypes.YQLDatabase;

@InterfaceAudience.Public
public class ListSnapshotSchedulesRequest extends YRpc<ListSnapshotSchedulesResponse> {

  private UUID snapshotScheduleUUID;

  ListSnapshotSchedulesRequest(YBTable table, UUID snapshotScheduleUUID) {
    super(table);
    this.snapshotScheduleUUID = snapshotScheduleUUID;
  }

  @Override
  ByteBuf serialize(Message header) {
    assert header.isInitialized();
    final ListSnapshotSchedulesRequestPB.Builder builder =
        ListSnapshotSchedulesRequestPB.newBuilder();
    if (snapshotScheduleUUID != null) {
      builder.setSnapshotScheduleId(CommonUtil.convertToByteString(snapshotScheduleUUID));
    }
    return toChannelBuffer(header, builder.build());
  }

  @Override
  String serviceName() {
    return MASTER_BACKUP_SERVICE_NAME;
  }

  @Override
  String method() {
    return "ListSnapshotSchedules";
  }

  @Override
  Pair<ListSnapshotSchedulesResponse, Object> deserialize(
      CallResponse callResponse, String masterUUID) throws Exception {

    final ListSnapshotSchedulesResponsePB.Builder respBuilder =
        ListSnapshotSchedulesResponsePB.newBuilder();
    readProtobuf(callResponse.getPBMessage(), respBuilder);
    boolean hasErr = respBuilder.hasError();
    MasterTypes.MasterErrorPB serverError = hasErr ? respBuilder.getError() : null;

    List<SnapshotScheduleInfo> snapshotScheduleInfoList = new ArrayList<>();
    if (!hasErr) {
      for (SnapshotScheduleInfoPB schedule : respBuilder.getSchedulesList()) {
        SnapshotScheduleInfo snapshotScheduleInfo;
        UUID scheduleUUID = CommonUtil.convertToUUID(schedule.getId());
        SnapshotScheduleOptionsPB snapshotScheduleOptions = schedule.getOptions();
        SnapshotScheduleFilterPB snapshotScheduleFilter = snapshotScheduleOptions.getFilter();
        long intervalInSecs = snapshotScheduleOptions.getIntervalSec();
        long retentionDurationInSecs = snapshotScheduleOptions.getRetentionDurationSec();

        String namespaceName = null;
        UUID namespaceUUID = null;
        YQLDatabase dbType = null;
        TableIdentifiersPB tables = snapshotScheduleFilter.getTables();

        if (tables.getTablesCount() > 0) {
          MasterTypes.TableIdentifierPB firstTable = tables.getTables(0);
          if (firstTable.hasNamespace()) {
            MasterTypes.NamespaceIdentifierPB ns = firstTable.getNamespace();

            if (ns.hasName()) {
              namespaceName = ns.getName();
            }
            if (ns.hasId() && ns.getId().size() == 16) {
              namespaceUUID = CommonUtil.convertToUUID(ns.getId());
            }
            if (ns.hasDatabaseType()) {
              dbType = ns.getDatabaseType();
            }
          }
        }

        List<SnapshotInfo> snapshotInfoList = new ArrayList<>();
        for (SnapshotInfoPB snapshot : schedule.getSnapshotsList()) {
          snapshotInfoList.add(SnapshotUtil.parseSnapshotInfoPB(snapshot));
        }
        snapshotScheduleInfo =
            new SnapshotScheduleInfo(
                scheduleUUID,
                intervalInSecs,
                retentionDurationInSecs,
                snapshotInfoList,
                namespaceName,
                namespaceUUID,
                dbType);
        snapshotScheduleInfoList.add(snapshotScheduleInfo);
      }
    }
    ListSnapshotSchedulesResponse response =
        new ListSnapshotSchedulesResponse(
            deadlineTracker.getElapsedMillis(), masterUUID, serverError, snapshotScheduleInfoList);
    return new Pair<ListSnapshotSchedulesResponse, Object>(response, serverError);
  }
}
