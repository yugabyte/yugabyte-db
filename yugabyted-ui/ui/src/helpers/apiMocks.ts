import MockAdapter from 'axios-mock-adapter';
import { AXIOS_INSTANCE } from '@app/api/src';

const AXIOS_INSTANCE_MOCK = new MockAdapter(AXIOS_INSTANCE, { delayResponse: 3000, onNoMatch: 'passthrough' });

AXIOS_INSTANCE_MOCK.onGet('/public/users/self/accounts').reply(200, {
  data: {
    admin_accounts: [
      {
        spec: { name: 'test-acc' },
        info: {
          id: '72c03c59-275b-4ae5-8a72-3d0e54add194',
          owner_id: 'dbb1d869-0464-466a-811d-4d2879141552',
          metadata: { created_on: '2021-07-20T09:56:32.336Z', updated_on: '2021-07-20T09:56:32.336Z' }
        }
      }
    ],
    accounts: [
      {
        spec: { name: 'test-acc' },
        info: {
          id: '72c03c59-275b-4ae5-8a72-3d0e54add194',
          owner_id: 'dbb1d869-0464-466a-811d-4d2879141552',
          metadata: { created_on: '2021-07-20T09:56:32.336Z', updated_on: '2021-07-20T09:56:32.336Z' }
        }
      }
    ]
  }
});

AXIOS_INSTANCE_MOCK.onGet('/public/accounts/72c03c59-275b-4ae5-8a72-3d0e54add194/projects').reply(200, {
  data: [
    {
      id: '511a692c-0fc4-4ec2-a92c-3e777344182c',
      name: 'default',
      metadata: { created_on: '2021-07-20T09:56:32.336Z', updated_on: '2021-07-20T09:56:32.336Z' }
    }
  ],
  _metadata: {
    continuation_token: null,
    links: {
      self:
        '/api/public/accounts/72c03c59-275b-4ae5-8a72-3d0e54add194/projects?accountId=72c03c59-275b-4ae5-8a72-3d0e54add194',
      next: null
    }
  }
});

AXIOS_INSTANCE_MOCK.onGet(
  '/public/accounts/72c03c59-275b-4ae5-8a72-3d0e54add194/projects/511a692c-0fc4-4ec2-a92c-3e777344182c/clusters'
).reply(200, {
  data: [
    {
      spec: {
        name: 'sshev-test-1',
        cloud_info: { code: 'GCP', region: 'us-west2' },
        cluster_info: {
          cluster_tier: 'PAID',
          num_nodes: 1,
          fault_tolerance: 'NONE',
          node_info: { num_cores: 1, memory_mb: 3840, disk_size_gb: 250 }
        },
        network_info: {
          allow_list_ids: ['353d6d1c-241b-4f40-b1ae-aeed5cff63b1']
        }
      },
      info: {
        id: 'ffc6cb81-3300-4299-a228-28cd7d5054db',
        state: 'Active',
        endpoint: 'ffc6cb81-3300-4299-a228-28cd7d5054db.devcloud.yugabyte.com',
        project_id: '511a692c-0fc4-4ec2-a92c-3e777344182c',
        metadata: { created_on: '2021-07-13T18:36:12.189Z', updated_on: '2021-07-13T18:36:12.189Z' }
      }
    }
  ]
});

AXIOS_INSTANCE_MOCK.onGet(
  '/public/accounts/72c03c59-275b-4ae5-8a72-3d0e54add194/projects/511a692c-0fc4-4ec2-a92c-3e777344182c/clusters/ffc6cb81-3300-4299-a228-28cd7d5054db'
).reply(200, {
  data: {
    spec: {
      name: 'sshev-test-1',
      cloud_info: { code: 'GCP', region: 'us-west2' },
      cluster_info: {
        cluster_tier: 'PAID',
        num_nodes: 1,
        fault_tolerance: 'NONE',
        node_info: { num_cores: 1, memory_mb: 3840, disk_size_gb: 250 }
      },
      network_info: {
        allow_list_ids: ['353d6d1c-241b-4f40-b1ae-aeed5cff63b1']
      }
    },
    info: {
      id: 'ffc6cb81-3300-4299-a228-28cd7d5054db',
      state: 'Active',
      endpoint: 'ffc6cb81-3300-4299-a228-28cd7d5054db.devcloud.yugabyte.com',
      project_id: '511a692c-0fc4-4ec2-a92c-3e777344182c',
      metadata: { created_on: '2021-07-13T18:36:12.189Z', updated_on: '2021-07-13T18:36:12.189Z' }
    }
  }
});

// AXIOS_INSTANCE_MOCK.onGet('/public/regions/AWS', { params: { cloud: 'AWS' } }).reply(200, {
//   data: ['aws-us-east-1', 'aws-us-east-2', 'aws-us-east-3']
// });
// AXIOS_INSTANCE_MOCK.onGet('/public/regions/GCP', { params: { cloud: 'GCP' } }).reply(200, {
//   data: ['gcp-us-west-1', 'gcp-us-west-2', 'gcp-us-west-3']
// });
