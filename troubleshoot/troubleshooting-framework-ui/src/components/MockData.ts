// TODO: Will remove the mock data later
export const YBM_SINGLE_CLUSTER_DATA = {
    "data": {
        "spec": {
            "name": "gorgeous-mole",
            "cloud_info": {
                "code": "AWS",
                "region": "us-east-1"
            },
            "cluster_info": {
                "cluster_tier": "FREE",
                "cluster_type": "SYNCHRONOUS",
                "num_nodes": 1,
                "fault_tolerance": "NONE",
                "num_faults_to_tolerate": 0,
                "node_info": {
                    "num_cores": 2,
                    "memory_mb": 4096,
                    "disk_size_gb": 10,
                    "disk_iops": 3000
                },
                "is_production": false,
                "enterprise_security": false,
                "version": 7
            },
            "network_info": {
                "single_tenant_vpc_id": null
            },
            "software_info": {
                "track_id": "f423a93e-e8b7-4531-8acd-1d024a07760f"
            },
            "cluster_region_info": [
                {
                    "placement_info": {
                        "cloud_info": {
                            "code": "AWS",
                            "region": "us-east-1"
                        },
                        "num_nodes": 1,
                        "vpc_id": null,
                        "num_replicas": null,
                        "multi_zone": false
                    },
                    "is_default": true,
                    "is_affinitized": false,
                    "accessibility_types": [
                        "PUBLIC"
                    ],
                    "private_service_endpoint_info": null
                }
            ]
        }
    }
};

export const YBM_MUTLI_REGION_DATA = {
    "data": {
        "spec": {
            "name": "puppy-food-multi-rr",
            "cloud_info": {
                "code": "GCP",
                "region": "europe-west3"
            },
            "cluster_info": {
                "cluster_tier": "PAID",
                "cluster_type": "SYNCHRONOUS",
                "num_nodes": 6,
                "fault_tolerance": "REGION",
                "num_faults_to_tolerate": 1,
                "node_info": {
                    "num_cores": 4,
                    "memory_mb": 16384,
                    "disk_size_gb": 410,
                    "disk_iops": null
                },
                "is_production": true,
                "enterprise_security": false,
                "version": 1992
            },
            "network_info": {
                "single_tenant_vpc_id": "5b5e1180-df59-4f7f-8178-397dff2188c4"
            },
            "software_info": {
                "track_id": "abacc28c-cd4a-4cbf-8edd-e3018393d94f"
            },
            "cluster_region_info": [
                {
                    "placement_info": {
                        "cloud_info": {
                            "code": "GCP",
                            "region": "europe-west3"
                        },
                        "num_nodes": 2,
                        "vpc_id": "5b5e1180-df59-4f7f-8178-397dff2188c4",
                        "num_replicas": null,
                        "multi_zone": false
                    },
                    "is_default": false,
                    "is_affinitized": true,
                    "accessibility_types": [
                        "PRIVATE"
                    ],
                    "private_service_endpoint_info": null
                },
                {
                    "placement_info": {
                        "cloud_info": {
                            "code": "GCP",
                            "region": "asia-south1"
                        },
                        "num_nodes": 2,
                        "vpc_id": "5b5e1180-df59-4f7f-8178-397dff2188c4",
                        "num_replicas": null,
                        "multi_zone": false
                    },
                    "is_default": false,
                    "is_affinitized": true,
                    "accessibility_types": [
                        "PRIVATE"
                    ],
                    "private_service_endpoint_info": null
                },
                {
                    "placement_info": {
                        "cloud_info": {
                            "code": "GCP",
                            "region": "us-west1"
                        },
                        "num_nodes": 2,
                        "vpc_id": "5b5e1180-df59-4f7f-8178-397dff2188c4",
                        "num_replicas": null,
                        "multi_zone": false
                    },
                    "is_default": false,
                    "is_affinitized": true,
                    "accessibility_types": [
                        "PRIVATE"
                    ],
                    "private_service_endpoint_info": null
                }
            ]
        }
    }
};

export const YBM_SINGLE_REGION_INFO = {
    "data": [
        {
            "name": "gorgeous-mole-n1",
            "is_node_up": true,
            "is_master": true,
            "is_tserver": true,
            "is_read_replica": false,
            "metrics": {
                "memory_used_bytes": 31178752,
                "total_sst_file_size_bytes": 0,
                "uncompressed_sst_file_size_bytes": 0,
                "read_ops_per_sec": 0.0,
                "write_ops_per_sec": 0.0
            },
            "cloud_info": {
                "region": "us-east-1",
                "zone": "us-east-1a"
            }
        },
        {
            "name": "gorgeous-mole-n3",
            "is_node_up": true,
            "is_master": true,
            "is_tserver": true,
            "is_read_replica": false,
            "metrics": {
                "memory_used_bytes": 31178752,
                "total_sst_file_size_bytes": 0,
                "uncompressed_sst_file_size_bytes": 0,
                "read_ops_per_sec": 0.0,
                "write_ops_per_sec": 0.0
            },
            "cloud_info": {
                "region": "us-east-1",
                "zone": "us-east-1b"
            }
        },
         {
            "name": "gorgeous-mole-n4",
            "is_node_up": true,
            "is_master": true,
            "is_tserver": true,
            "is_read_replica": false,
            "metrics": {
                "memory_used_bytes": 31178752,
                "total_sst_file_size_bytes": 0,
                "uncompressed_sst_file_size_bytes": 0,
                "read_ops_per_sec": 0.0,
                "write_ops_per_sec": 0.0
            },
            "cloud_info": {
                "region": "us-east-1",
                "zone": "us-east-1b"
            }
        },
        {
            "name": "gorgeous-mole-n5",
            "is_node_up": true,
            "is_master": true,
            "is_tserver": true,
            "is_read_replica": true,
            "metrics": {
                "memory_used_bytes": 31178752,
                "total_sst_file_size_bytes": 0,
                "uncompressed_sst_file_size_bytes": 0,
                "read_ops_per_sec": 0.0,
                "write_ops_per_sec": 0.0
            },
            "cloud_info": {
                "region": "us-east-1",
                "zone": "us-east-1b"
            }
        },
         {
            "name": "gorgeous-mole-n2",
            "is_node_up": true,
            "is_master": true,
            "is_tserver": true,
            "is_read_replica": false,
            "metrics": {
                "memory_used_bytes": 31178752,
                "total_sst_file_size_bytes": 0,
                "uncompressed_sst_file_size_bytes": 0,
                "read_ops_per_sec": 0.0,
                "write_ops_per_sec": 0.0
            },
            "cloud_info": {
                "region": "us-east-2",
                "zone": "us-east-2b"
            }
        }
    ]
};

export const YBM_MULTI_REGION_INFO = {
    "data": [
        {
            "name": "puppy-food-multi-rr-n4",
            "is_node_up": true,
            "is_master": false,
            "is_tserver": true,
            "is_read_replica": false,
            "metrics": {
                "memory_used_bytes": 531947520,
                "total_sst_file_size_bytes": 953208979,
                "uncompressed_sst_file_size_bytes": 2943223156,
                "read_ops_per_sec": 1.2343464525146575,
                "write_ops_per_sec": 0.5290056225062818
            },
            "cloud_info": {
                "region": "europe-west3",
                "zone": "europe-west3-c"
            }
        },
        {
            "name": "puppy-food-multi-rr-n6",
            "is_node_up": true,
            "is_master": false,
            "is_tserver": true,
            "is_read_replica": false,
            "metrics": {
                "memory_used_bytes": 471867392,
                "total_sst_file_size_bytes": 995635395,
                "uncompressed_sst_file_size_bytes": 2636910677,
                "read_ops_per_sec": 1.1987226947991987,
                "write_ops_per_sec": 0.19978711579986647
            },
            "cloud_info": {
                "region": "asia-south1",
                "zone": "asia-south1-b"
            }
        },
        {
            "name": "puppy-food-multi-rr-n3",
            "is_node_up": true,
            "is_master": true,
            "is_tserver": true,
            "is_read_replica": false,
            "metrics": {
                "memory_used_bytes": 537755648,
                "total_sst_file_size_bytes": 953531518,
                "uncompressed_sst_file_size_bytes": 2942625036,
                "read_ops_per_sec": 0.9987311099276285,
                "write_ops_per_sec": 0.7989848879421028
            },
            "cloud_info": {
                "region": "asia-south1",
                "zone": "asia-south1-b"
            }
        },
        {
            "name": "puppy-food-multi-rr-europe-west3-read-replica-n21",
            "is_node_up": true,
            "is_master": false,
            "is_tserver": true,
            "is_read_replica": true,
            "metrics": {
                "memory_used_bytes": 166412288,
                "total_sst_file_size_bytes": 954415776,
                "uncompressed_sst_file_size_bytes": 2945434325,
                "read_ops_per_sec": 0.0,
                "write_ops_per_sec": 0.0
            },
            "cloud_info": {
                "region": "europe-west3",
                "zone": "europe-west3-b"
            }
        },
        {
            "name": "puppy-food-multi-rr-asia-south1-read-replica-n18",
            "is_node_up": true,
            "is_master": false,
            "is_tserver": true,
            "is_read_replica": true,
            "metrics": {
                "memory_used_bytes": 225918976,
                "total_sst_file_size_bytes": 952610650,
                "uncompressed_sst_file_size_bytes": 2943625390,
                "read_ops_per_sec": 0.0,
                "write_ops_per_sec": 0.0
            },
            "cloud_info": {
                "region": "asia-south1",
                "zone": "asia-south1-a"
            }
        },
        {
            "name": "puppy-food-multi-rr-us-west1-read-replica-n16",
            "is_node_up": true,
            "is_master": false,
            "is_tserver": true,
            "is_read_replica": true,
            "metrics": {
                "memory_used_bytes": 214188032,
                "total_sst_file_size_bytes": 994977359,
                "uncompressed_sst_file_size_bytes": 2638521288,
                "read_ops_per_sec": 0.0,
                "write_ops_per_sec": 0.0
            },
            "cloud_info": {
                "region": "us-west1",
                "zone": "us-west1-a"
            }
        },
        {
            "name": "puppy-food-multi-rr-asia-south1-read-replica-n20",
            "is_node_up": true,
            "is_master": false,
            "is_tserver": true,
            "is_read_replica": true,
            "metrics": {
                "memory_used_bytes": 161513472,
                "total_sst_file_size_bytes": 994698443,
                "uncompressed_sst_file_size_bytes": 2636257304,
                "read_ops_per_sec": 0.0,
                "write_ops_per_sec": 0.0
            },
            "cloud_info": {
                "region": "asia-south1",
                "zone": "asia-south1-a"
            }
        },
        {
            "name": "puppy-food-multi-rr-europe-west3-read-replica-n17",
            "is_node_up": true,
            "is_master": false,
            "is_tserver": true,
            "is_read_replica": true,
            "metrics": {
                "memory_used_bytes": 198049792,
                "total_sst_file_size_bytes": 994451017,
                "uncompressed_sst_file_size_bytes": 2637919571,
                "read_ops_per_sec": 0.0,
                "write_ops_per_sec": 0.0
            },
            "cloud_info": {
                "region": "europe-west3",
                "zone": "europe-west3-b"
            }
        },
        {
            "name": "puppy-food-multi-rr-n5",
            "is_node_up": true,
            "is_master": false,
            "is_tserver": true,
            "is_read_replica": false,
            "metrics": {
                "memory_used_bytes": 547004416,
                "total_sst_file_size_bytes": 953117304,
                "uncompressed_sst_file_size_bytes": 2945977922,
                "read_ops_per_sec": 3.118549582810367,
                "write_ops_per_sec": 0.3282683771379334
            },
            "cloud_info": {
                "region": "us-west1",
                "zone": "us-west1-c"
            }
        },
        {
            "name": "puppy-food-multi-rr-us-west1-read-replica-n19",
            "is_node_up": true,
            "is_master": false,
            "is_tserver": true,
            "is_read_replica": true,
            "metrics": {
                "memory_used_bytes": 171982848,
                "total_sst_file_size_bytes": 952398760,
                "uncompressed_sst_file_size_bytes": 2940786913,
                "read_ops_per_sec": 0.0,
                "write_ops_per_sec": 0.0
            },
            "cloud_info": {
                "region": "us-west1",
                "zone": "us-west1-a"
            }
        },
        {
            "name": "puppy-food-multi-rr-n1",
            "is_node_up": true,
            "is_master": true,
            "is_tserver": true,
            "is_read_replica": false,
            "metrics": {
                "memory_used_bytes": 454148096,
                "total_sst_file_size_bytes": 991538532,
                "uncompressed_sst_file_size_bytes": 2633345155,
                "read_ops_per_sec": 1.0542843915006923,
                "write_ops_per_sec": 0.3514281305002308
            },
            "cloud_info": {
                "region": "europe-west3",
                "zone": "europe-west3-c"
            }
        },
        {
            "name": "puppy-food-multi-rr-n2",
            "is_node_up": true,
            "is_master": true,
            "is_tserver": true,
            "is_read_replica": false,
            "metrics": {
                "memory_used_bytes": 599351296,
                "total_sst_file_size_bytes": 993395690,
                "uncompressed_sst_file_size_bytes": 2633603642,
                "read_ops_per_sec": 1.1525576861144016,
                "write_ops_per_sec": 0.3293021960326861
            },
            "cloud_info": {
                "region": "us-west1",
                "zone": "us-west1-c"
            }
        }
    ]
};

export const YBA_UNIVERSE_PRIMAY_ASYNC_DATA = {
  "universeDetails": {
        "platformVersion": "2.21.0.0-PRE_RELEASE",
        "sleepAfterMasterRestartMillis": 180000,
        "sleepAfterTServerRestartMillis": 180000,
        "nodeExporterUser": "prometheus",
        "universeUUID": "4b96b61b-fe01-4692-8621-ef0bbbe2778e",
        "enableYbc": true,
        "ybcSoftwareVersion": "2.0.0.0-b18",
        "installYbc": false,
        "ybcInstalled": true,
        "expectedUniverseVersion": -1,
        "encryptionAtRestConfig": {
            "encryptionAtRestEnabled": false,
            "opType": "UNDEFINED",
            "type": "DATA_KEY"
        },
        "extraDependencies": {
            "installNodeExporter": true
        },
        "platformUrl": "localhost:9000",
        "clusters": [
           {
                "uuid": "a9e8848e-9e9b-427c-b1d9-d833f02725d8",
                "clusterType": "PRIMARY",
                "placementInfo": {
                    "cloudList": [
                        {
                            "uuid": "86aa9009-349e-405f-88f0-8db326971816",
                            "code": "aws",
                            "regionList": [
                                {
                                    "uuid": "aa676604-d067-4335-b72e-4b3ca84c1aef",
                                    "code": "us-west-2",
                                    "name": "US West (Oregon)",
                                    "azList": [
                                        {
                                            "uuid": "f7749417-d152-429b-bcfb-91d9aa59133c",
                                            "name": "us-west-2a",
                                            "replicationFactor": 1,
                                            "subnet": "subnet-6553f513",
                                            "numNodesInAZ": 1,
                                            "isAffinitized": true
                                        }
                                    ]
                                }
                            ]
                        }
                    ]
                },
                "index": 0
            },
            {
                "uuid": "34b93ffd-70de-4812-924d-93d1e7a59cb9",
                "clusterType": "ASYNC",
                "placementInfo": {
                    "cloudList": [
                        {
                            "uuid": "86aa9009-349e-405f-88f0-8db326971816",
                            "code": "aws",
                            "regionList": [
                                {
                                    "uuid": "aa676604-d067-4335-b72e-4b3ca84c1aef",
                                    "code": "us-west-2",
                                    "name": "US West (Oregon)",
                                    "azList": [
                                        {
                                            "uuid": "f7749417-d152-429b-bcfb-91d9aa59133c",
                                            "name": "us-west-2a",
                                            "replicationFactor": 1,
                                            "subnet": "subnet-6553f513",
                                            "numNodesInAZ": 1,
                                            "isAffinitized": true
                                        }
                                    ]
                                }
                            ]
                        }
                    ]
                },
                "index": 0
            }
        ],
        "updateOptions": [
            "UPDATE"
        ],
        "otelCollectorEnabled": false,
        "xclusterInfo": {
            "sourceRootCertDirPath": "/home/yugabyte/yugabyte-tls-producer",
            "targetXClusterConfigs": [],
            "sourceXClusterConfigs": []
        },
        "targetXClusterConfigs": [],
        "sourceXClusterConfigs": [],
        "nodeDetailsSet": [
            {
                "nodeIdx": 3,
                "nodeName": "yb-admin-test-dedicated-nodes-n3",
                "nodeUuid": "85893346-37b3-3a27-9f4d-2a6eb6533db6",
                "cloudInfo": {
                    "private_ip": "10.9.100.48",
                    "secondary_private_ip": "null",
                    "public_ip": "54.213.203.44",
                    "public_dns": "ec2-54-213-203-44.us-west-2.compute.amazonaws.com",
                    "private_dns": "ip-10-9-100-48.us-west-2.compute.internal",
                    "instance_type": "c5.large",
                    "subnet_id": "subnet-6553f513",
                    "az": "us-west-2a",
                    "region": "us-west-2",
                    "cloud": "aws",
                    "assignPublicIP": true,
                    "useTimeSync": false,
                    "lun_indexes": [],
                    "root_volume": "vol-0bc792daa8cf7f6c1"
                },
                "azUuid": "f7749417-d152-429b-bcfb-91d9aa59133c",
                "placementUuid": "a9e8848e-9e9b-427c-b1d9-d833f02725d8",
                "disksAreMountedByUUID": true
            },
            {
                "nodeIdx": 2,
                "nodeName": "yb-admin-test-dedicated-nodes-n2",
                "nodeUuid": "671be082-4c41-3c46-aff7-92fdcbdebeda",
                "cloudInfo": {
                    "private_ip": "10.9.89.145",
                    "secondary_private_ip": "null",
                    "public_ip": "54.191.213.218",
                    "public_dns": "ec2-54-191-213-218.us-west-2.compute.amazonaws.com",
                    "private_dns": "ip-10-9-89-145.us-west-2.compute.internal",
                    "instance_type": "c5.large",
                    "subnet_id": "subnet-6553f513",
                    "az": "us-west-2a",
                    "region": "us-west-2",
                    "cloud": "aws",
                    "assignPublicIP": true,
                    "useTimeSync": true,
                    "lun_indexes": [],
                    "root_volume": "vol-02650283b38695cda"
                },
                "azUuid": "f7749417-d152-429b-bcfb-91d9aa59133c",
                "placementUuid": "a9e8848e-9e9b-427c-b1d9-d833f02725d8",
                "disksAreMountedByUUID": true
            },
            {
                "nodeIdx": 1,
                "nodeName": "yb-admin-test-dedicated-nodes-readonly1-n1",
                "nodeUuid": "588d728d-c5b3-313e-b66c-fcf32bcbdb2b",
                "cloudInfo": {
                    "private_ip": "10.9.120.197",
                    "secondary_private_ip": "null",
                    "public_ip": "35.92.78.85",
                    "public_dns": "ec2-35-92-78-85.us-west-2.compute.amazonaws.com",
                    "private_dns": "ip-10-9-120-197.us-west-2.compute.internal",
                    "instance_type": "c5.large",
                    "subnet_id": "subnet-6553f513",
                    "az": "us-west-2a",
                    "region": "us-west-2",
                    "cloud": "aws",
                    "assignPublicIP": true,
                    "useTimeSync": true,
                    "lun_indexes": [],
                    "root_volume": "vol-00af870c918c8e604"
                },
                "azUuid": "f7749417-d152-429b-bcfb-91d9aa59133c",
                "placementUuid": "34b93ffd-70de-4812-924d-93d1e7a59cb9",
                "disksAreMountedByUUID": true
            }
        ]
    }
  };
