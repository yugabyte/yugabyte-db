// Copyright (c) YugaByte, Inc.

package org.yb.client;

import java.util.List;
import java.util.stream.Collectors;
import org.yb.annotations.InterfaceAudience;
import org.yb.master.MasterClientOuterClass;
import org.yb.master.MasterClientOuterClass.GetTabletLocationsResponsePB.Error;
import org.yb.master.MasterTypes;

@InterfaceAudience.Public
public class GetTabletLocationsResponse extends YRpcResponse {

  private final MasterTypes.MasterErrorPB serverError;
  private final List<MasterClientOuterClass.TabletLocationsPB> tabletLocations;
  private final List<MasterClientOuterClass.GetTabletLocationsResponsePB.Error> tabletErrors;
  private final int partitionListVersion;

  GetTabletLocationsResponse(long elapsedMillis,
                            String tsUUID,
                            MasterTypes.MasterErrorPB serverError,
                            List<MasterClientOuterClass.TabletLocationsPB> tabletLocations,
                            List<Error> tabletErrors,
                            int partitionListVersion) {
    super(elapsedMillis, tsUUID);
    this.serverError = serverError;
    this.tabletLocations = tabletLocations;
    this.tabletErrors = tabletErrors;
    this.partitionListVersion = partitionListVersion;
  }

  public boolean hasServerError() {
    return serverError != null;
  }

  public String serverErrorMessage() {
    if (serverError == null) {
      return "";
    }

    return serverError.getStatus().getMessage();
  }

  // Errors for specific tablet Ids, example: table id does not exist.
  public boolean hasTabletErrors() {
    return tabletErrors != null && !tabletErrors.isEmpty();
  }

  public List<MasterClientOuterClass.GetTabletLocationsResponsePB.Error> getTabletErrors() {
    return tabletErrors;
  }

  public String tabletErrorsMessage() {
    if (!hasTabletErrors()) {
      return "";
    }
    List<String> errorMessages = tabletErrors
      .stream()
      .map(t -> String.format("\"%s\": \"%s\"",
                              t.getTabletId().toStringUtf8(),
                              t.getStatus().getMessage()))
      .collect(Collectors.toList());

    return "{" + String.join(", ", errorMessages) + "}";
  }

  public List<MasterClientOuterClass.TabletLocationsPB> getTabletLocations() {
    return tabletLocations;
  }

  public int getPartitionListVersion() {
    return partitionListVersion;
  }
}
