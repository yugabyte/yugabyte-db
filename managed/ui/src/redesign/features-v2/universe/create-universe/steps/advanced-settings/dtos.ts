export interface InstanceTag {
  name: string;
  value: string;
  id?: string;
}

export interface OtherAdvancedProps {
  masterHttpPort: number;
  masterRpcPort: number;
  tserverHttpPort: number;
  tserverRpcPort: number;
  yqlServerHttpPort: number;
  yqlServerRpcPort: number;
  ysqlServerHttpPort: number;
  ysqlServerRpcPort: number;
  internalYsqlServerRpcPort: number;
  redisServerHttpPort: number;
  redisServerRpcPort: number;
  nodeExporterPort: number;
  ybControllerrRpcPort: number;
  instanceTags: InstanceTag[];
  useTimeSync: boolean;
  awsArnString: string;
  useSystemd: boolean;
  accessKeyCode: string;
  enableIPV6: boolean;
  enableExposingService: boolean;
}

export interface AccessKey {
  idKey: {
    keyCode: string;
    providerUUID: string;
  };
  keyInfo: {
    publicKey: string;
    privateKey: string;
    vaultPasswordFile: string;
    vaultFile: string;
    sshUser: string;
    sshPort: number;
    airGapInstall: boolean;
    passwordlessSudoAccess: boolean;
    provisionInstanceScript: string;
  };
}

export interface ProxyAdvancedProps {
  enableProxyServer: boolean;
  secureWebProxy: boolean;
  secureWebProxyServer?: string;
  secureWebProxyPort?: number | undefined;
  webProxy: boolean;
  webProxyServer?: string;
  webProxyPort?: number;
  byPassProxyList: boolean;
  byPassProxyListValues: string[];
}
