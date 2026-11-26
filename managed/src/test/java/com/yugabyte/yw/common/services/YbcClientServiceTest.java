// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.services;

import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.Mockito.RETURNS_SELF;
import static org.mockito.Mockito.doReturn;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.mockStatic;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.yugabyte.yw.common.certmgmt.RpcClientCert;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.models.Universe;
import java.util.Optional;
import org.junit.Before;
import org.junit.Test;
import org.mockito.MockedStatic;
import org.yb.client.YbcClient;
import org.yb.client.YbcClient.YbcClientBuilder;

public class YbcClientServiceTest {

  private static final String CA_FILE = "/certs/ca.crt";

  private RpcClientCert rpcClientCert;
  private Universe universe;
  private YbcClientService ybcClientService;

  @Before
  public void setUp() {
    rpcClientCert = mock(RpcClientCert.class);
    universe = mock(Universe.class);
    when(universe.getCertificateNodetoNode()).thenReturn(CA_FILE);
    RuntimeConfGetter confGetter = mock(RuntimeConfGetter.class);
    doReturn(0).when(confGetter).getGlobalConf(any());
    ybcClientService = new YbcClientService(confGetter, rpcClientCert);
  }

  private YbcClientBuilder buildClient() {
    YbcClientBuilder builder = mock(YbcClientBuilder.class, RETURNS_SELF);
    try (MockedStatic<YbcClient> ybcClient = mockStatic(YbcClient.class)) {
      ybcClient.when(YbcClient::builder).thenReturn(builder);
      ybcClientService.getNewClient("10.0.0.1", 18018, universe);
    }
    return builder;
  }

  @Test
  public void testPresentsClientCertWhenUniverseRequiresOne() {
    when(rpcClientCert.getFiles(universe))
        .thenReturn(Optional.of(new RpcClientCert.CertAndKey("/c/client.crt", "/c/client.key")));
    YbcClientBuilder builder = buildClient();
    verify(builder).withCertsFilepath(CA_FILE);
    verify(builder).withClientCertFilepath("/c/client.crt");
    verify(builder).withClientKeyFilepath("/c/client.key");
  }

  @Test
  public void testNoClientCertWhenUniverseDoesNotRequireOne() {
    when(rpcClientCert.getFiles(universe)).thenReturn(Optional.empty());
    YbcClientBuilder builder = buildClient();
    verify(builder).withCertsFilepath(CA_FILE);
    verify(builder, never()).withClientCertFilepath(anyString());
    verify(builder, never()).withClientKeyFilepath(anyString());
  }
}
