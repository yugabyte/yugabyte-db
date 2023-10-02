import { render, screen } from '../../../test-utils';
import OnPremNodesList from './OnPremNodesList';

jest.mock('axios');
const getRegionListItems = jest.fn();
const getInstanceTypeListItems = jest.fn();
const showAddNodesDialog = jest.fn();
const fetchCustomerTasks = jest.fn();
const handleNodeSubmit = jest.fn((addNodeFn) => addNodeFn);
const PROVIDER_UUID = '9545ac0f-29ee-46ea-ba89-680720a7cb7e';
const REGION_NAME = 'us-west1';
const ZONE_CODE = 'us-west1-a';
const SSH_USER = 'test';
const MACHINE_TYPE = 'Linux';
const TEST_NODE_NAME = 'yb-test-onprem-universe-n1';

const cloudMockState = {
  nodeInstanceList: {
    data: [
      {
        nodeUuid: '1238bb57-562e-4a56-9d34-cfdf9d7cbb57',
        instanceTypeCode: 'n1-standard-4',
        nodeName: TEST_NODE_NAME,
        instanceName: '',
        zoneUuid: 'eb6f7ddd-2da6-4f8c-ad4a-d352abaca169',
        inUse: true,
        detailsJson:
          '{"ip":"10.150.1.149","sshUser":"hkandala","region":"us-west-1","zone":"z1","instanceType":"n1-standard-4","instanceName":"","nodeName":"yb-15-hkandala-onprem-universe-n1"}',
        details: {
          ip: '10.150.1.149',
          sshUser: SSH_USER,
          region: REGION_NAME,
          zone: 'z1',
          instanceType: 'n1-standard-4',
          instanceName: '',
          nodeName: TEST_NODE_NAME
        }
      }
    ]
  },
  providers: {
    data: [
      {
        active: true,
        awsHostedZoneName: null,
        cloudParams: {
          errorString: null,
          providerUUID: null,
          perRegionMetadata: {},
          keyPairName: null
        },
        code: 'onprem',
        config: { USE_HOSTNAME: 'false' },
        customerUUID: 'f33e3c9b-75ab-4c30-80ad-cba85646ea39',
        hostedZoneId: null,
        instanceTypes: [{ providerUuid: PROVIDER_UUID, instanceTypeCode: 'Linux' }],
        name: 'fewaijgeaf',
        priceComponents: [],
        uuid: PROVIDER_UUID
      }
    ]
  },
  accessKeys: {
    data: [
      {
        idKey: {
          providerUUID: PROVIDER_UUID
        },
        keyInfo: {
          sshUser: SSH_USER,
          sshPort: 22
        }
      }
    ]
  },
  supportedRegionList: {
    data: [
      {
        code: REGION_NAME,
        provider: {
          code: 'onprem'
        },
        zones: [
          {
            code: ZONE_CODE,
            uuid: 'b31285cf-5509-4145-9481-991522317160',
            name: ZONE_CODE
          }
        ]
      }
    ]
  },
  instanceTypes: {
    data: [
      {
        instanceTypeCode: MACHINE_TYPE
      }
    ]
  }
};
const mockUniverseList = {
  data: [
    {
      universeUUID: 'a3a31551-a6cb-4b6a-b808-fa2a34860ff6',
      name: 'test-universe-1',
      creationDate: 1617617731775,
      version: 1,
      resources: {
        pricePerHour: 0.1836,
        ebsPricePerHour: 0,
        numCores: 3,
        memSizeGB: 11.25,
        volumeCount: 3,
        volumeSizeGB: 1125,
        numNodes: 3
      },
      pricePerHour: 0.1836,
      universeDetails: {
        nodeDetailsSet: [
          {
            nodeName: TEST_NODE_NAME
          }
        ]
      }
    }
  ],
  promiseState: {
    name: 'SUCCESS',
    ordinal: 1
  }
};
const tasks = {
  customerTaskList: []
};

beforeEach(() => {
  render(
    <OnPremNodesList
      cloud={cloudMockState}
      universeList={mockUniverseList}
      getRegionListItems={getRegionListItems}
      getInstanceTypeListItems={getInstanceTypeListItems}
      handleSubmit={handleNodeSubmit}
      fetchCustomerTasks={fetchCustomerTasks}
      tasks={tasks}
      showAddNodesDialog={showAddNodesDialog}
    />
  );
});

describe('On-Premise node instance list tests', () => {
  it('render node instance in table', () => {
    expect(screen.getAllByRole('row')).toHaveLength(2);
  });

  // TODO: Migrate to Formik and add proper unit tests
});
