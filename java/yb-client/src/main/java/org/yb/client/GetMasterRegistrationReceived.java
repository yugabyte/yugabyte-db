// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//
package org.yb.client;

import com.google.common.net.HostAndPort;
import com.google.protobuf.ByteString;
import com.stumbleupon.async.Callback;
import com.stumbleupon.async.Deferred;
import org.yb.annotations.InterfaceAudience;
import org.yb.Common;
import org.yb.CommonTypes;
import org.yb.consensus.Metadata;
import org.yb.master.MasterClientOuterClass;
import org.yb.util.NetUtil;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicInteger;

/**
 * Class grouping the callback and the errback for GetMasterRegistration calls
 * made in getMasterTableLocationsPB.
 */
@InterfaceAudience.Private
final class GetMasterRegistrationReceived {

  private static final Logger LOG = LoggerFactory.getLogger(GetMasterRegistrationReceived.class);

  private final List<HostAndPort> masterAddrs;
  private final Deferred<MasterClientOuterClass.GetTableLocationsResponsePB> responseD;
  private final int numMasters;

  // Used to avoid calling 'responseD' twice.
  private final AtomicBoolean responseDCalled = new AtomicBoolean(false);

  // Number of responses we've receives: used to tell whether or not we've received
  // errors/replies from all of the masters, or if there are any
  // GetMasterRegistrationRequests still pending.
  private final AtomicInteger countResponsesReceived = new AtomicInteger(0);

  // Exceptions received so far: kept for debugging purposes.
  // (see: NoLeaderMasterFoundException#create() for how this is used).
  private final List<Exception> exceptionsReceived =
      Collections.synchronizedList(new ArrayList<Exception>());

  /**
   * Creates an object that holds the state needed to retrieve master table's location.
   * @param masterAddrs Addresses of all master replicas that we want to retrieve the
   *                    registration from.
   * @param responseD Deferred object that will hold the GetTableLocationsResponsePB object for
   *                  the master table.
   */
  public GetMasterRegistrationReceived(
      List<HostAndPort> masterAddrs,
      Deferred<MasterClientOuterClass.GetTableLocationsResponsePB> responseD) {
    this.masterAddrs = masterAddrs;
    this.responseD = responseD;
    this.numMasters = masterAddrs.size();
  }

  /**
   * Creates a callback for a GetMasterRegistrationRequest that was sent to 'hostAndPort'.
   * @see GetMasterRegistrationCB
   * @param hostAndPort Host and part for the RPC we're attaching this to. Host and port must
   *                    be valid.
   * @return The callback object that can be added to the RPC request.
   */
  public Callback<Void, GetMasterRegistrationResponse> callbackForNode(HostAndPort hostAndPort) {
    return new GetMasterRegistrationCB(hostAndPort);
  }

  /**
   * Creates an errback for a GetMasterRegistrationRequest that was sent to 'hostAndPort'.
   * @see GetMasterRegistrationErrCB
   * @param hostAndPort Host and port for the RPC we're attaching this to. Used for debugging
   *                    purposes.
   * @return The errback object that can be added to the RPC request.
   */
  public Callback<Void, Exception> errbackForNode(HostAndPort hostAndPort) {
    return new GetMasterRegistrationErrCB(hostAndPort);
  }

  /**
   * Checks if we've already received a response or an exception from every master that
   * we've sent a GetMasterRegistrationRequest to. If so -- and no leader has been found
   * (that is, 'responseD' was never called) -- pass a {@link NoLeaderMasterFoundException}
   * to responseD.
   */
  private void incrementCountAndCheckExhausted() {
    if (countResponsesReceived.incrementAndGet() == numMasters) {
      if (responseDCalled.compareAndSet(false, true)) {
        boolean allUnrecoverable = true;
        // When there are no exceptions, default to retry semantics.
        if (exceptionsReceived.isEmpty()) {
          allUnrecoverable = false;
        }

        for (Exception ex : exceptionsReceived) {
          if (!(ex instanceof NonRecoverableException)) {
            allUnrecoverable = false;
            break;
          }
        }
        for (Exception ex : exceptionsReceived) {
          if (!(ex instanceof NoLeaderMasterFoundException)) {
            allUnrecoverable = false;
            break;
          }
        }
        String allHosts = NetUtil.hostsAndPortsToString(masterAddrs);
        // Doing a negative check because allUnrecoverable stays true if there are no exceptions.
        if (!allUnrecoverable) {
          if (exceptionsReceived.isEmpty()) {
            LOG.warn("None of the provided masters (" + allHosts + ") is a leader, will retry.");
          } else {
            LOG.warn("Unable to find the leader master (" + allHosts + "), will retry");
          }
          responseD.callback(NoLeaderMasterFoundException.create(
              "Master config (" + allHosts + ") has no leader.",
              exceptionsReceived));
        } else {
          // This will stop retries.
          responseD.callback(new NonRecoverableException("Couldn't find a valid master in (" +
              allHosts + "), exceptions: " + exceptionsReceived));
        }
      }
    }
  }

  /**
   * Callback for each GetMasterRegistrationRequest sent in getMasterTableLocations() above.
   * If a request (paired to a specific master) returns a reply that indicates it's a leader,
   * the callback in 'responseD' is invoked with an initialized GetTableLocationResponsePB
   * object containing the leader's RPC address.
   * If the master is not a leader, increment 'countResponsesReceived': if the count equals to
   * the number of masters, pass {@link NoLeaderMasterFoundException} into
   * 'responseD' if no one else had called 'responseD' before; otherwise, do nothing.
   */
  final class GetMasterRegistrationCB implements Callback<Void, GetMasterRegistrationResponse> {
    private final HostAndPort hostAndPort;

    public GetMasterRegistrationCB(HostAndPort hostAndPort) {
      this.hostAndPort = hostAndPort;
    }

    @Override
    public Void call(GetMasterRegistrationResponse r) throws Exception {
      MasterClientOuterClass.TabletLocationsPB.ReplicaPB.Builder replicaBuilder =
          MasterClientOuterClass.TabletLocationsPB.ReplicaPB.newBuilder();

      MasterClientOuterClass.TSInfoPB.Builder tsInfoBuilder =
          MasterClientOuterClass.TSInfoPB.newBuilder();
      tsInfoBuilder.addPrivateRpcAddresses(ProtobufHelper.hostAndPortToPB(hostAndPort));
      tsInfoBuilder.setPermanentUuid(r.getInstanceId().getPermanentUuid());
      replicaBuilder.setTsInfo(tsInfoBuilder);
      if (r.getRole().equals(CommonTypes.PeerRole.LEADER)) {
        replicaBuilder.setRole(r.getRole());
        MasterClientOuterClass.TabletLocationsPB.Builder locationBuilder =
            MasterClientOuterClass.TabletLocationsPB.newBuilder();
        locationBuilder.setPartition(
            Common.PartitionPB.newBuilder().setPartitionKeyStart(ByteString.EMPTY)
                                           .setPartitionKeyEnd(ByteString.EMPTY));
        locationBuilder.setTabletId(
            ByteString.copyFromUtf8(AsyncYBClient.MASTER_TABLE_NAME_PLACEHOLDER));
        locationBuilder.setStale(false);
        locationBuilder.addReplicas(replicaBuilder);
        // No one else has called this before us.
        if (responseDCalled.compareAndSet(false, true)) {
          responseD.callback(
              MasterClientOuterClass.GetTableLocationsResponsePB.newBuilder().addTabletLocations(
                  locationBuilder.build()).build()
          );
        } else {
          LOG.debug("Callback already invoked, discarding response(" + r.toString() + ") from " +
              hostAndPort.toString());
        }
      } else {
        incrementCountAndCheckExhausted();
      }
      return null;
    }

    @Override
    public String toString() {
      return "get master registration for " + hostAndPort.toString();
    }
  }

  /**
   * Errback for each GetMasterRegistrationRequest sent in getMasterTableLocations() above.
   * Stores each exception in 'exceptionsReceived'. Increments 'countResponseReceived': if
   * the count is equal to the number of masters and no one else had called 'responseD' before,
   * pass a {@link NoLeaderMasterFoundException} into 'responseD'; otherwise, do
   * nothing.
   */
  final class GetMasterRegistrationErrCB implements Callback<Void, Exception> {
    private final HostAndPort hostAndPort;

    public GetMasterRegistrationErrCB(HostAndPort hostAndPort) {
      this.hostAndPort = hostAndPort;
    }

    @Override
    public Void call(Exception e) throws Exception {
      exceptionsReceived.add(e);
      incrementCountAndCheckExhausted();
      return null;
    }

    @Override
    public String toString() {
      return "get master registration errback for " + hostAndPort.toString();
    }
  }
}
