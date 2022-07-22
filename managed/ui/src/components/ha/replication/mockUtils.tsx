export const mockRuntimeConfigs: any = {
  type: 'GLOBAL',
  uuid: '00000000-0000-0000-0000-000000000000',
  mutableScope: true,
  configEntries: [
    {
      inherited: false,
      key: 'yb.ha.ws',
      value:
        '{"ahc":{"disableUrlEncoding":false,"idleConnectionInPoolTimeout":"1 minute","keepAlive":true,"maxConnectionLifetime":null,"maxConnectionsPerHost":-1,"maxConnectionsTotal":-1,"maxNumberOfRedirects":5,"maxRequestRetry":5},"cache":{"cacheManagerResource":null,"cacheManagerURI":null,"cachingProviderName":"","enabled":false,"heuristics":{"enabled":false},"name":"play-ws-cache"},"compressionEnabled":false,"followRedirects":true,"ssl":{"checkRevocation":null,"debug":{"all":false,"certpath":false,"data":false,"defaultctx":false,"handshake":false,"keygen":false,"keymanager":false,"ocsp":false,"packet":false,"plaintext":false,"pluggability":false,"record":false,"session":false,"sessioncache":false,"ssl":false,"sslctx":false,"trustmanager":false,"verbose":false},"default":false,"disabledKeyAlgorithms":["RSA keySize < 2048","DSA keySize < 2048","EC keySize < 224"],"disabledSignatureAlgorithms":["MD2","MD4","MD5"],"enabledCipherSuites":[],"enabledProtocols":["TLSv1.2","TLSv1.1","TLSv1"],"hostnameVerifierClass":null,"keyManager":{"algorithm":null,"prototype":{"stores":{"data":null,"password":null,"path":null,"type":null}},"stores":[]},"loose":{"acceptAnyCertificate":false,"allowLegacyHelloMessages":null,"allowUnsafeRenegotiation":null,"allowWeakCiphers":false,"allowWeakProtocols":false,"disableHostnameVerification":false,"disableSNI":false},"protocol":"TLSv1.2","revocationLists":[],"sslParameters":{"clientAuth":"default","protocols":[]},"trustManager":{"algorithm":null,"prototype":{"stores":{"data":null,"path":null,"type":null}},"stores":[]}},"timeout":{"connection":"2 minutes","idle":"2 minutes","request":"2 minutes"},"useProxyProperties":true,"useragent":null}'
    }
  ]
};
