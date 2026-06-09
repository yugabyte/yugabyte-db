import { Universe } from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';

export type GenerateUniverseMockResponseOptions = {
  universeUuid?: string;
};

export const generateUniverseMockResponse = (
  options?: GenerateUniverseMockResponseOptions
): Universe => {
  const universeUuid = options?.universeUuid ?? 'mock-universe-uuid';
  const PRIMARY_CLUSTER_UUID = 'mock-cluster-uuid';
  const CUSTOMER_UUID = 'customer-uuid';
  return {
    spec: {
      name: 'Multi-region Universe',
      yb_software_version: '2.24.0.0',
      use_time_sync: true,
      ysql: {
        enable: true,
        enable_auth: false,
        enable_connection_pooling: false
      },
      ycql: {
        enable: true,
        enable_auth: false
      },
      encryption_in_transit_spec: {
        enable_node_to_node_encrypt: true,
        enable_client_to_node_encrypt: true,
        root_ca: 'root-ca-uuid',
        client_root_ca: 'client-root-ca-uuid'
      },
      encryption_at_rest_spec: {
        kms_config_uuid: undefined
      },
      networking_spec: {
        assign_public_ip: true,
        assign_static_public_ip: false,
        enable_ipv6: false,
        communication_ports: {
          master_http_port: 7000,
          master_rpc_port: 7100,
          tserver_http_port: 9000,
          tserver_rpc_port: 9100,
          redis_server_http_port: 11000,
          redis_server_rpc_port: 6379,
          yql_server_http_port: 12000,
          yql_server_rpc_port: 9042,
          ysql_server_http_port: 5433,
          ysql_server_rpc_port: 5434,
          node_exporter_port: 9300
        }
      },
      clusters: [
        {
          uuid: PRIMARY_CLUSTER_UUID,
          cluster_type: 'PRIMARY',
          num_nodes: 3,
          replication_factor: 3,
          node_spec: {
            instance_type: 'c5.xlarge',
            storage_spec: {
              volume_size: 100,
              num_volumes: 1,
              storage_type: 'GP3',
              disk_iops: 3000,
              throughput: 125
            }
          },
          provider_spec: {
            provider: 'aws-cloud-provider-uuid',
            region_list: ['us-west-2'],
            access_key_code: 'access-key'
          },
          placement_spec: {
            cloud_list: [
              {
                uuid: 'aws-cloud-provider-uuid',
                code: 'aws',
                region_list: [
                  {
                    uuid: 'us-west-2-region-uuid',
                    name: 'US West (Oregon)',
                    code: 'us-west-2',
                    az_list: [
                      {
                        uuid: 'us-west-2a-uuid',
                        name: 'us-west-2a',
                        replication_factor: 1,
                        num_nodes_in_az: 1
                      },
                      {
                        uuid: 'us-west-2b-uuid',
                        name: 'us-west-2b',
                        replication_factor: 1,
                        num_nodes_in_az: 1
                      }
                    ]
                  },
                  {
                    uuid: 'sa-east-1-region-uuid',
                    name: 'South America (São Paulo)',
                    code: 'sa-east-1',
                    az_list: [
                      {
                        uuid: 'sa-east-1a-uuid',
                        name: 'sa-east-1a',
                        replication_factor: 1,
                        num_nodes_in_az: 1
                      }
                    ]
                  }
                ],
                masters_in_default_region: true
              }
            ]
          },
          networking_spec: {
            enable_exposing_service: 'NONE'
          }
        }
      ]
    },
    info: {
      universe_uuid: universeUuid,
      version: 2,
      creation_date: '2024-06-15T10:00:00Z',
      creating_user: {
        spec: { email: 'admin@example.com', role: 'Admin' },
        info: {
          uuid: 'user-uuid',
          user_type: 'local',
          creation_date: '2024-01-01T00:00:00Z',
          customer_uuid: CUSTOMER_UUID,
          is_primary: true
        }
      },
      arch: 'x86_64',
      dns_name: 'yb-admin-story-universe.example.com',
      ybc_software_version: '0.0.1.0',
      yba_url: 'http://localhost:9000',
      node_prefix: 'yb-admin-story-universe',
      encryption_at_rest_info: {
        encryption_at_rest_status: false
      },
      update_in_progress: false,
      updating_task: undefined,
      updating_task_uuid: undefined,
      update_succeeded: true,
      previous_task_uuid: undefined,
      universe_paused: false,
      placement_modification_task_uuid: undefined,
      software_upgrade_state: 'Ready',
      is_software_rollback_allowed: true,
      previous_yb_software_details: {
        yb_software_version: '2.20.0.0',
        auto_flag_config_version: 0
      },
      allowed_tasks_on_failure: undefined,
      nodes_resize_available: true,
      is_kubernetes_operator_controlled: false,
      otel_collector_enabled: false,
      fips_enabled: false,
      x_cluster_info: {
        source_x_cluster_configs: [],
        target_x_cluster_configs: []
      },
      roll_max_batch_size: {
        primary_batch_size: 3,
        read_replica_batch_size: undefined
      },
      clusters: [
        {
          uuid: PRIMARY_CLUSTER_UUID,
          spot_price: undefined
        }
      ],
      node_details_set: [
        {
          node_uuid: 'node-uuid-1',
          node_name: 'node-name-1',
          node_idx: 0,
          az_uuid: 'us-west-2a-uuid',
          placement_uuid: PRIMARY_CLUSTER_UUID,
          state: 'Live',
          is_master: true,
          is_tserver: true,
          is_ysql_server: true,
          is_yql_server: true,
          is_redis_server: true,
          master_state: 'Configured',
          cloud_info: {
            cloud: 'aws',
            region: 'us-west-2',
            az: 'us-west-2a',
            private_ip: '10.0.1.11',
            public_ip: '54.1.2.11',
            instance_type: 'c5.xlarge',
            subnet_id: 'subnet-1'
          }
        },
        {
          node_uuid: 'node-uuid-2',
          node_name: 'node-name-2',
          node_idx: 1,
          az_uuid: 'us-west-2b-uuid',
          placement_uuid: PRIMARY_CLUSTER_UUID,
          state: 'Live',
          is_master: true,
          is_tserver: true,
          is_ysql_server: true,
          is_yql_server: true,
          is_redis_server: true,
          master_state: 'Configured',
          cloud_info: {
            cloud: 'aws',
            region: 'us-west-2',
            az: 'us-west-2b',
            private_ip: '10.0.1.12',
            public_ip: '54.1.2.12',
            instance_type: 'c5.xlarge',
            subnet_id: 'subnet-2'
          }
        },
        {
          node_uuid: 'node-uuid-3',
          node_name: 'node-name-3',
          node_idx: 2,
          az_uuid: 'sa-east-1a-uuid',
          placement_uuid: PRIMARY_CLUSTER_UUID,
          state: 'Live',
          is_master: true,
          is_tserver: true,
          is_ysql_server: true,
          is_yql_server: true,
          is_redis_server: true,
          master_state: 'Configured',
          cloud_info: {
            cloud: 'aws',
            region: 'sa-east-1',
            az: 'sa-east-1a',
            private_ip: '10.0.1.13',
            public_ip: '54.1.2.13',
            instance_type: 'c5.xlarge',
            subnet_id: 'subnet-3'
          }
        }
      ],
      dr_config_uuids_as_source: [],
      dr_config_uuids_as_target: [],
      resources: {
        az_list: ['us-west-2a', 'us-west-2b', 'sa-east-1a'],
        num_nodes: 3,
        num_cores: 4,
        mem_size_gb: 8,
        volume_size_gb: 100,
        volume_count: 3,
        price_per_hour: 0.42,
        ebs_price_per_hour: 0.1,
        pricing_known: true
      },
      sample_app_command_txt: undefined
    }
  };
};
