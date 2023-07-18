import userEvent from '@testing-library/user-event';
import { ChangeOrAddProvider } from './ChangeOrAddProvider';
import { render } from '../../../../../test-utils';
import { reduxForm } from 'redux-form';

const configuredProviderMock = {
  data: [
    {
      uuid: '8c340543-b57e-463c-9c28-97d0953b250b',
      code: 'azu',
      name: 'azure-itest'
    },
    {
      uuid: '72bcd87d-cb7d-490d-b925-9847a75dba51',
      code: 'kubernetes',
      name: 'openshiftconfig'
    },
    {
      uuid: '109e95b5-bf08-4a8f-a7fb-2d2866865e15',
      code: 'gcp',
      name: 'GCP-config'
    },
    {
      uuid: '129e95b5-bf08-4a8f-a7fb-2d2866865e15',
      code: 'gcp',
      name: 'GCP-config-2'
    },
    {
      uuid: '62198ba4-6497-4d5f-986c-eec246dc822a',
      code: 'other',
      name: 'My Cloud'
    },
    {
      uuid: '17186989-633d-407c-a5c6-4c7ee7d18aad',
      code: 'cloud-1',
      name: 'Cloud-1'
    },
    {
      uuid: '08c0ba0e-3558-40fc-94e5-a87a627de8c5',
      code: 'aws',
      name: 'aws-portal-1'
    }
  ]
};

const setup = () => {
  const setCurrentViewCreateConfig = jest.fn();
  const selectProvider = jest.fn();
  const mock = jest.fn();
  const ChangeOrAddProviderWithReduxForm = reduxForm({
    form: 'ChangeOrAddProvider',
    fields: [],
    mock
  })(ChangeOrAddProvider);
  const component = render(
    <ChangeOrAddProviderWithReduxForm
      configuredProviders={configuredProviderMock}
      providerType="gcp"
      selectProvider={selectProvider}
      setCurrentViewCreateConfig={setCurrentViewCreateConfig}
    />
  );
  return { component, setCurrentViewCreateConfig, selectProvider };
};

describe('ChangeOrAddComponent tests', () => {
  it('should render ChangeOrAddProvider component', () => {
    const { component } = setup();
    expect(component.getByTestId('change-or-add-provider')).toBeInTheDocument();
  });

  it('should validate that combobox is rendered and selected with appropriate value', () => {
    const { component, selectProvider } = setup();
    userEvent.selectOptions(
      component.getByRole('combobox', { name: '' }),
      configuredProviderMock.data[3].uuid
    );
    expect(selectProvider).toBeCalledWith(configuredProviderMock.data[3].uuid);
  });

  it('should trigger onClose by Cancel button click', () => {
    const { component, setCurrentViewCreateConfig } = setup();
    userEvent.click(component.getByRole('button', { name: /Add Configuration/i }));
    expect(setCurrentViewCreateConfig).toBeCalled();
  });
});
