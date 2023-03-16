export const mockProps = {
  universe: {
    universeList: {
      data: [
        {
          universeUUID: 'd0e7fa91-1bff-482e-acd4-be0272748e96',
          name: 'mock-universe',
          creationDate: 'Mon Oct 18 21:38:43 EDT 2021',
          version: 1,
          resources: {
            pricePerHour: 0,
            ebsPricePerHour: 0,
            numCores: 0,
            memSizeGB: 0,
            volumeCount: 0,
            volumeSizeGB: 0,
            numNodes: 2,
            gp3FreePiops: 3000,
            gp3FreeThroughput: 125,
            azList: []
          },
          universeDetails: {
            nodeExporterUser: 'prometheus',
            universeUUID: 'd0e7fa91-1bff-482e-acd4-be0272748e96',
            expectedUniverseVersion: -1,
            encryptionAtRestConfig: {
              encryptionAtRestEnabled: false,
              opType: 'UNDEFINED',
              type: 'DATA_KEY'
            },
            communicationPorts: {
              masterHttpPort: 7000,
              masterRpcPort: 7100,
              tserverHttpPort: 9000,
              tserverRpcPort: 9100,
              redisServerHttpPort: 11000,
              redisServerRpcPort: 6379,
              yqlServerHttpPort: 12000,
              yqlServerRpcPort: 9042,
              ysqlServerHttpPort: 13000,
              ysqlServerRpcPort: 5433,
              nodeExporterPort: 9300
            },
            extraDependencies: {
              installNodeExporter: true
            },
            firstTry: true,
            clusters: [
              {
                uuid: 'f017e45a-ebdb-4282-82c3-7e46c7029fdb',
                clusterType: 'PRIMARY',
                userIntent: {
                  universeName: 'mock-universe',
                  provider: 'ac474e0d-fdf1-4a21-acc7-adcf051011d6',
                  providerType: 'aws',
                  replicationFactor: 1,
                  regionList: ['e3307dfd-1cb9-4852-b971-4a63cec8d1d4'],
                  instanceType: 'c5.large',
                  numNodes: 2,
                  ybSoftwareVersion: '2.9.0.0-b4',
                  deviceInfo: {
                    volumeSize: 250,
                    numVolumes: 1,
                    diskIops: 3000,
                    throughput: 125,
                    storageClass: '',
                    storageType: 'GP3'
                  },
                  assignPublicIP: true,
                  assignStaticPublicIP: false,
                  useTimeSync: true,
                  enableYCQL: true,
                  enableYSQLAuth: false,
                  enableYCQLAuth: false,
                  enableYSQL: true,
                  enableYEDIS: false,
                  enableNodeToNodeEncrypt: true,
                  enableClientToNodeEncrypt: true,
                  enableVolumeEncryption: false,
                  enableIPV6: false,
                  enableExposingService: 'UNEXPOSED',
                  awsArnString: '',
                  useHostname: false,
                  useSystemd: true,
                  masterGFlags: {},
                  tserverGFlags: {},
                  instanceTags: {}
                },
                placementInfo: {
                  cloudList: [
                    {
                      uuid: 'ac474e0d-fdf1-4a21-acc7-adcf051011d6',
                      code: 'aws',
                      regionList: [
                        {
                          uuid: 'e3307dfd-1cb9-4852-b971-4a63cec8d1d4',
                          code: 'us-west-2',
                          name: 'US West (Oregon)',
                          azList: [
                            {
                              uuid: '7c205585-6803-4505-a95d-2ad8e386dee8',
                              name: 'us-west-2a',
                              replicationFactor: 1,
                              subnet: 'subnet-6553f513',
                              numNodesInAZ: 2,
                              isAffinitized: true
                            }
                          ]
                        }
                      ]
                    }
                  ]
                },
                index: 0,
                regions: [
                  {
                    uuid: 'e3307dfd-1cb9-4852-b971-4a63cec8d1d4',
                    code: 'us-west-2',
                    name: 'US West (Oregon)',
                    ybImage: 'ami-b63ae0ce',
                    longitude: -120.554201,
                    latitude: 43.804133,
                    zones: [
                      {
                        uuid: '7c205585-6803-4505-a95d-2ad8e386dee8',
                        code: 'us-west-2a',
                        name: 'us-west-2a',
                        active: true,
                        subnet: 'subnet-6553f513'
                      },
                      {
                        uuid: '7dff4466-9f82-4dc8-b347-d5a042d3fd64',
                        code: 'us-west-2b',
                        name: 'us-west-2b',
                        active: true,
                        subnet: 'subnet-f840ce9c'
                      },
                      {
                        uuid: '7481e0a9-2557-4a8f-9ccc-2cfb5ad4f675',
                        code: 'us-west-2c',
                        name: 'us-west-2c',
                        active: true,
                        subnet: 'subnet-01ac5b59'
                      }
                    ]
                  }
                ]
              }
            ],
            currentClusterType: 'PRIMARY',
            nodePrefix: 'yb-admin-mock-universe',
            rootAndClientRootCASame: true,
            userAZSelected: false,
            resetAZConfig: false,
            updateInProgress: false,
            backupInProgress: false,
            updateSucceeded: true,
            universePaused: true,
            nextClusterIndex: 1,
            allowInsecure: false,
            setTxnTableWaitCountFlag: true,
            itestS3PackagePath: '',
            remotePackagePath: '',
            importedState: 'NONE',
            capability: 'EDITS_ALLOWED',
            targetXClusterConfigs: [],
            sourceXClusterConfigs: [],
            nodeDetailsSet: [
              {
                nodeIdx: 1,
                nodeName: 'yb-admin-mock-universe-n1',
                cloudInfo: {
                  private_ip: '10.9.107.212',
                  secondary_private_ip: 'null',
                  public_ip: '52.32.59.119',
                  public_dns: 'ec2-52-32-59-119.us-west-2.compute.amazonaws.com',
                  private_dns: 'ip-10-9-107-212.us-west-2.compute.internal',
                  instance_type: 'c5.large',
                  subnet_id: 'subnet-6553f513',
                  az: 'us-west-2a',
                  region: 'us-west-2',
                  cloud: 'aws',
                  assignPublicIP: true,
                  useTimeSync: true
                },
                azUuid: '7c205585-6803-4505-a95d-2ad8e386dee8',
                placementUuid: 'f017e45a-ebdb-4282-82c3-7e46c7029fdb',
                state: 'Stopped',
                isMaster: true,
                masterHttpPort: 7000,
                masterRpcPort: 7100,
                isTserver: true,
                tserverHttpPort: 9000,
                tserverRpcPort: 9100,
                isRedisServer: true,
                redisServerHttpPort: 11000,
                redisServerRpcPort: 6379,
                isYqlServer: true,
                yqlServerHttpPort: 12000,
                yqlServerRpcPort: 9042,
                isYsqlServer: true,
                ysqlServerHttpPort: 13000,
                ysqlServerRpcPort: 5433,
                nodeExporterPort: 9300,
                cronsActive: true,
                allowedActions: ['START', 'QUERY']
              },
              {
                nodeIdx: 2,
                nodeName: 'yb-admin-mock-universe-n2',
                azUuid: '7c205585-6803-4505-a95d-2ad8e386dee8',
                placementUuid: 'f017e45a-ebdb-4282-82c3-7e46c7029fdb',
                state: 'Stopped',
                isMaster: false,
                masterHttpPort: 7000,
                masterRpcPort: 7100,
                isTserver: true,
                tserverHttpPort: 9000,
                tserverRpcPort: 9100,
                isRedisServer: true,
                redisServerHttpPort: 11000,
                redisServerRpcPort: 6379,
                isYqlServer: true,
                yqlServerHttpPort: 12000,
                yqlServerRpcPort: 9042,
                isYsqlServer: true,
                ysqlServerHttpPort: 13000,
                ysqlServerRpcPort: 5433,
                nodeExporterPort: 9300,
                cronsActive: true,
                allowedActions: ['START', 'QUERY', 'REMOVE']
              }
            ]
          },
          universeConfig: {
            takeBackups: 'true'
          },
          pricePerHour: 0
        }
      ]
    }
  },
  customer: {
    currentCustomer: {
      data: {
        uuid: 'f33e3c9b-75ab-4c30-80ad-cba85646ea39',
        code: 'admin',
        name: 'admin',
        creationDate: '2021-09-14T22:26:49+0000',
        features: {},
        universeUUIDs: ['d0e7fa91-1bff-482e-acd4-be0272748e96'],
        customerId: 1
      }
    }
  },
  tasks: {
    customerTaskList: []
  },
  modal: {
    showModal: false,
    visibleModal: ''
  },
  providers: {
    data: [
      {
        uuid: 'ac474e0d-fdf1-4a21-acc7-adcf051011d6',
        code: 'aws',
        name: 'jmak-aws',
        active: true,
        customerUUID: 'f33e3c9b-75ab-4c30-80ad-cba85646ea39',
        regions: [
          {
            uuid: 'e3307dfd-1cb9-4852-b971-4a63cec8d1d4',
            code: 'us-west-2',
            name: 'US West (Oregon)',
            ybImage: 'ami-b63ae0ce',
            longitude: -120.554201,
            latitude: 43.804133,
            zones: [
              {
                uuid: '7c205585-6803-4505-a95d-2ad8e386dee8',
                code: 'us-west-2a',
                name: 'us-west-2a',
                active: true,
                subnet: 'subnet-6553f513'
              },
              {
                uuid: '7dff4466-9f82-4dc8-b347-d5a042d3fd64',
                code: 'us-west-2b',
                name: 'us-west-2b',
                active: true,
                subnet: 'subnet-f840ce9c'
              },
              {
                uuid: '7481e0a9-2557-4a8f-9ccc-2cfb5ad4f675',
                code: 'us-west-2c',
                name: 'us-west-2c',
                active: true,
                subnet: 'subnet-01ac5b59'
              }
            ],
            details: {
              sg_id: 'sg-139dde6c'
            },
            securityGroupId: 'sg-139dde6c',
            config: {}
          }
        ],
        airGapInstall: false,
        sshPort: 54422
      },
      {
        uuid: '61dce469-e2fb-4cd7-921f-d622a14a823f',
        code: 'gcp',
        name: 'jmak-gcp',
        active: true,
        customerUUID: 'f33e3c9b-75ab-4c30-80ad-cba85646ea39',
        regions: [
          {
            uuid: '66e0a749-b910-4a79-bc40-81253f39c9dc',
            code: 'us-west1',
            name: 'Oregon',
            zones: [
              {
                uuid: 'a4b60d28-8d95-4a97-b669-a0c315014d84',
                code: 'us-west1-a',
                name: 'us-west1-a',
                active: true,
                subnet: 'subnet-us-west1'
              },
              {
                uuid: '5c1e9937-4d78-4c2f-ad24-a715dd1fa7c7',
                code: 'us-west1-b',
                name: 'us-west1-b',
                active: true,
                subnet: 'subnet-us-west1'
              },
              {
                uuid: '9ebc22b1-0b02-4a2f-8574-6fae1143c418',
                code: 'us-west1-c',
                name: 'us-west1-c',
                active: true,
                subnet: 'subnet-us-west1'
              }
            ],
            config: {}
          }
        ]
      }
    ]
  },
  featureFlags: {
    test: {
      pausedUniverse: true,
      addListMultiProvider: true,
      adminAlertsConfig: true,
      enableNewEncryptionInTransitModal: true,
      addRestoreTimeStamp: false
    },
    released: {
      pausedUniverse: true,
      addListMultiProvider: true,
      adminAlertsConfig: true,
      enableNewEncryptionInTransitModal: true,
      addRestoreTimeStamp: false
    }
  },
  closeModal: jest.fn(),
  showToggleUniverseStateModal: jest.fn(),
  showDeleteUniverseModal: jest.fn(),
  fetchUniverseMetadata: jest.fn(),
  fetchGlobalRunTimeConfigs: jest.fn(),
  fetchUniverseTasks: jest.fn(),
  resetUniverseTasks: jest.fn()
};
