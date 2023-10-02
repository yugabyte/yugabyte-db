// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models;

import io.swagger.annotations.ApiModel;
import lombok.Data;
import org.yb.client.GetMasterRegistrationResponse;

@Data
@ApiModel(description = "Information about master")
public class MasterInfo {
  private String privateIp;
  private Long uptimeSeconds;
  private PeerRole peerRole;

  public enum PeerRole {
    FOLLOWER,
    LEADER,
    LEARNER,
    NON_PARTICIPANT,
    READ_REPLICA,
    UNKNOWN_ROLE
  }

  public static MasterInfo convertFrom(GetMasterRegistrationResponse masterRegistrationResponse) {
    MasterInfo result = new MasterInfo();
    if (masterRegistrationResponse.getServerRegistration().getPrivateRpcAddressesCount() > 0) {
      result.setPrivateIp(
          masterRegistrationResponse
              .getServerRegistration()
              .getPrivateRpcAddressesList()
              .get(0)
              .getHost());
    }
    result.setUptimeSeconds(
        (System.currentTimeMillis()
                - masterRegistrationResponse.getInstanceId().getStartTimeUs() / 1000)
            / 1000);
    result.setPeerRole(PeerRole.valueOf(masterRegistrationResponse.getRole().name()));
    return result;
  }
}
