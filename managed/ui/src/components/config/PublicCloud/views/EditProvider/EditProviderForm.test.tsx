import userEvent from '@testing-library/user-event';
import EditProviderForm from './EditProviderForm';
import { render } from '../../../../../test-utils';
import { getInitialState } from '../../../../../utils/PromiseUtils';
import { reduxForm } from 'redux-form';

jest.mock('../../../../../redesign/helpers/api');

const initialUniverseListMock = getInitialState([]);

// @ts-ignore
const EditProviderFormWithReduxForm = reduxForm({ form: 'EditProviderForm' })(EditProviderForm);

const universeListMockData = [
  {
    universeDetails: {
      clusters: [
        {
          clusterType: 'PRIMARY',
          userIntent: {
            provider: '12581bba-b6f7-4a35-8694-060436dcafc1',
            providerType: 'aws'
          }
        }
      ]
    }
  },
  {
    universeDetails: {
      clusters: [
        {
          clusterType: 'PRIMARY',
          userIntent: {
            provider: '12581bba-b6f7-4a35-8694-060436dcafc2',
            providerType: 'gcp'
          }
        }
      ]
    }
  }
];

const providerToEdit = {
  uuid: '12581bba-b6f7-4a35-8694-060436dcafc2',
  code: 'aws',
  hostedZoneId: '',
  accountName: 'test-aws',
  accountUUID: '12581bba-b6f7-4a35-8694-060436dcafc2',
  secretKey: 'yb-admin-test-aws_12581bba-b6f7-4a35-8694-060436dcafcc-key'
};

const setup = (universeListMock = initialUniverseListMock, provider?: any) => {
  const handleSubmit = jest.fn();
  const fetchUniverseList = jest.fn();
  const switchToResultView = jest.fn();
  const reloadCloudMetadata = jest.fn();
  const component = render(
    <EditProviderFormWithReduxForm
      reloadCloudMetadata={reloadCloudMetadata}
      switchToResultView={switchToResultView}
      fetchUniverseList={fetchUniverseList}
      handleSubmit={handleSubmit}
      editProvider={provider}
      universeList={universeListMock || initialUniverseListMock}
      {...providerToEdit}
      initialValues={providerToEdit}
    />
  );
  return { component, handleSubmit, fetchUniverseList, switchToResultView };
};

describe('EditProviderForm components tests', () => {
  it('should render', () => {
    const { component } = setup();
    expect(component.getByTestId('edit-provider-form')).toBeInTheDocument();
  });

  it('should trigger switchToResultView by Cancel button click', () => {
    const { component, switchToResultView } = setup();
    userEvent.click(component.getByRole('button', { name: /Cancel/i }));
    expect(switchToResultView).toBeCalled();
  });

  it('should validate for readonly sshKey', async () => {
    const universeListMock = getInitialState();
    universeListMock.promiseState = { name: 'SUCCESS', ordinal: 1 };
    universeListMock.data = universeListMockData;
    const editProviderMock = getInitialState();
    editProviderMock.promiseState = { name: 'SUCCESS', ordinal: 1 };
    editProviderMock.data = providerToEdit;
    const { component } = setup(universeListMock, editProviderMock);
    expect(
      component.getByDisplayValue('yb-admin-test-aws_12581bba-b6f7-4a35-8694-060436dcafcc-key')
    ).toBeInTheDocument();
    expect(
      component.getByDisplayValue('yb-admin-test-aws_12581bba-b6f7-4a35-8694-060436dcafcc-key')
    ).toHaveAttribute('readonly');
  });

  it('should validate for sshKey is editable', async () => {
    const universeListMock = getInitialState();
    universeListMock.promiseState = { name: 'SUCCESS', ordinal: 1 };
    universeListMock.data = universeListMockData;
    providerToEdit.uuid = providerToEdit.uuid + 'test';
    const editProviderMock = getInitialState();
    editProviderMock.promiseState = { name: 'SUCCESS', ordinal: 1 };
    editProviderMock.data = providerToEdit;
    const { component } = setup(universeListMock, editProviderMock);
    expect(
      component.getByDisplayValue('yb-admin-test-aws_12581bba-b6f7-4a35-8694-060436dcafcc-key')
    ).toBeInTheDocument();
    expect(
      component.getByDisplayValue('yb-admin-test-aws_12581bba-b6f7-4a35-8694-060436dcafcc-key')
    ).not.toHaveAttribute('readonly');
  });
});
