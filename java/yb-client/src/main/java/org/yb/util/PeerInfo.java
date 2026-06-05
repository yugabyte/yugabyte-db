// Copyright (c) YugabyteDB, Inc.

package org.yb.util;

import com.google.common.net.HostAndPort;
import java.util.ArrayList;
import java.util.List;
import java.util.Objects;
import org.yb.consensus.Metadata;

public class PeerInfo {
  public enum MemberType {
    UNKNOWN_MEMBER_TYPE,
    PRE_VOTER,
    VOTER,
    PRE_OBSERVER,
    OBSERVER
  }

  private List<HostAndPort> lastKnownPrivateIps = new ArrayList<>();
  private List<HostAndPort> lastKnownBroadcastIps = new ArrayList<>();
  private MemberType memberType;
  private String uuid;

  public MemberType getMemberType() {
    return memberType;
  }

  public void setMemberType(Metadata.PeerMemberType pbMemberType) {
    setMemberType(MemberType.valueOf(pbMemberType.name()));
  }

  public void setMemberType(MemberType memberType) {
    this.memberType = memberType;
  }

  public List<HostAndPort> getLastKnownPrivateIps() {
    return lastKnownPrivateIps;
  }

  public void setLastKnownPrivateIps(List<HostAndPort> lastKnownPrivateIps) {
    this.lastKnownPrivateIps = lastKnownPrivateIps;
  }

  public List<HostAndPort> getLastKnownBroadcastIps() {
    return lastKnownBroadcastIps;
  }

  public void setLastKnownBroadcastIps(List<HostAndPort> lastKnownBroadcastIps) {
    this.lastKnownBroadcastIps = lastKnownBroadcastIps;
  }

  public String getUuid() {
    return uuid;
  }

  public void setUuid(String uuid) {
    this.uuid = uuid;
  }

  public boolean hasHost(String host) {
    // YBA may track a node by either its RPC address (lastKnownPrivateIps) or its
    // server_broadcast_address (lastKnownBroadcastIps) -- e.g. on K8s MCS the broadcast
    // address is what YBA stores as private_ip. Match against both so callers don't
    // need to know which one applies.
    return lastKnownPrivateIps.stream().anyMatch(hp -> Objects.equals(hp.getHost(), host))
        || lastKnownBroadcastIps.stream().anyMatch(hp -> Objects.equals(hp.getHost(), host));
  }
}
