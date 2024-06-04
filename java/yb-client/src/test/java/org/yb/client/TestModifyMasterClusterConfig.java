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

import java.util.ArrayList;
import java.util.List;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.CommonNet.HostPortPB;
import org.mockito.invocation.InvocationOnMock;
import org.mockito.stubbing.Answer;
import org.yb.master.CatalogEntityInfo;
import org.yb.master.MasterTypes.MasterErrorPB;
import org.yb.master.MasterTypes.MasterErrorPB.Code;
import org.yb.WireProtocol.AppStatusPB;
import org.yb.YBTestRunner;

import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;
// import static org.yb.AssertionWrappers.*;

@RunWith(value=YBTestRunner.class)
public class TestModifyMasterClusterConfig {

  @Test
  public void testModifyMasterClusterConfigRetriesErrResponse() throws Exception {
    YBClient client = mock(YBClient.class);

    CatalogEntityInfo.SysClusterConfigEntryPB config =
        CatalogEntityInfo.SysClusterConfigEntryPB.newBuilder().build();
    when(client.getMasterClusterConfig()).thenReturn(
        new GetMasterClusterConfigResponse(1L, "uuid", config, null));
    // changeMasterClusterConfig to return first with CONFIG_VERSION_MISMATCH error code, then a
    // second time with no error.
    MasterErrorPB errorVersion = MasterErrorPB.newBuilder()
        .setCode(Code.CONFIG_VERSION_MISMATCH)
        .setStatus(AppStatusPB.newBuilder().setCode(AppStatusPB.ErrorCode.INVALID_ARGUMENT).build())
        .build();
    when(client.changeMasterClusterConfig(any())).thenReturn(
        new ChangeMasterClusterConfigResponse(1L, "uuid", errorVersion),
        new ChangeMasterClusterConfigResponse(1L, "uuid", null));

    HostPortPB host = HostPortPB.newBuilder().setHost("host").setPort(1).build();
    List<HostPortPB> hosts = new ArrayList<HostPortPB>();
    hosts.add(host);
    ModifyMasterClusterConfigBlacklist modifyMasterClusterConfig =
        new ModifyMasterClusterConfigBlacklist(client, hosts, true);

    // Call the modify master cluster config.
    modifyMasterClusterConfig.doCall();
    verify(client, times(2)).changeMasterClusterConfig(any());
  }

  @Test
  public void testModifyMasterClusterConfigRetriesMasterException() throws Exception {
    YBClient client = mock(YBClient.class);

    CatalogEntityInfo.SysClusterConfigEntryPB config =
        CatalogEntityInfo.SysClusterConfigEntryPB.newBuilder().build();
    when(client.getMasterClusterConfig()).thenReturn(
        new GetMasterClusterConfigResponse(1L, "uuid", config, null));

    // changeMasterClusterConfig to return first with CONFIG_VERSION_MISMATCH error code, then a
    // second time with no error.
    MasterErrorPB errorVersion = MasterErrorPB.newBuilder()
        .setCode(Code.CONFIG_VERSION_MISMATCH)
        .setStatus(AppStatusPB.newBuilder().setCode(AppStatusPB.ErrorCode.INVALID_ARGUMENT).build())
        .build();
    when(client.changeMasterClusterConfig(any())).thenAnswer(new Answer(){
        boolean didException = false; // Track if the answer raised an error
        public Object answer(InvocationOnMock invocation) throws Throwable {
          if (!didException){
            didException = true;
            throw new MasterErrorException("serverUUID", errorVersion);
          }
          return new ChangeMasterClusterConfigResponse(1L, "uuid", null);
        }
      });

    HostPortPB host = HostPortPB.newBuilder().setHost("host").setPort(1).build();
    List<HostPortPB> hosts = new ArrayList<HostPortPB>();
    hosts.add(host);
    ModifyMasterClusterConfigBlacklist modifyMasterClusterConfig =
        new ModifyMasterClusterConfigBlacklist(client, hosts, true);

    // Call the modify master cluster config.
    modifyMasterClusterConfig.doCall();
    verify(client, times(2)).changeMasterClusterConfig(any());
  }
}
