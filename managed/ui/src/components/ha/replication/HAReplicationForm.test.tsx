import { toast } from 'react-toastify';
import userEvent from '@testing-library/user-event';

import { render, fireEvent, waitFor } from '../../../test-utils';
import { FREQUENCY_MULTIPLIER, HAInstanceTypes, HAReplicationForm } from './HAReplicationForm';
import { HAConfig, HAReplicationSchedule } from '../../../redesign/helpers/dtos';
import { api } from '../../../redesign/helpers/api';
import { MOCK_HA_WS_RUNTIME_CONFIG, MOCK_HA_WS_RUNTIME_CONFIG_WITH_PEER_CERTS } from './mockUtils';

jest.mock('../../../redesign/helpers/api');

const mockConfig = {
  cluster_key: 'fake-key',
  instances: [
    {
      uuid: 'instance-id-1',
      address: 'http://fake.address',
      is_leader: true,
      is_local: true
    }
  ]
} as HAConfig;
const mockSchedule: HAReplicationSchedule = {
  frequency_milliseconds: 5 * FREQUENCY_MULTIPLIER,
  is_running: false // intentionally set enable replication toggle fo "off" to test all edge cases
};

const setup = (hasPeerCerts: boolean, config?: HAConfig, schedule?: HAReplicationSchedule) => {
  const backToView = jest.fn();
  const fetchRuntimeConfigs = jest.fn();
  const setRuntimeConfig = jest.fn();

  const mockRuntimeConfigPromise = {
    data: hasPeerCerts ? MOCK_HA_WS_RUNTIME_CONFIG_WITH_PEER_CERTS : MOCK_HA_WS_RUNTIME_CONFIG,
    error: null,
    promiseState: 'SUCCESS'
  };

  const component = render(
    <HAReplicationForm
      config={config}
      schedule={schedule}
      backToViewMode={backToView}
      runtimeConfigs={mockRuntimeConfigPromise}
      fetchRuntimeConfigs={fetchRuntimeConfigs}
      setRuntimeConfig={setRuntimeConfig}
    />
  );

  const form = component.getByRole('form');
  const formFields = {
    // eslint-disable-next-line @typescript-eslint/no-non-null-assertion
    instanceType: form.querySelector<HTMLInputElement>('input[name="instanceType"]:checked')!,
    // eslint-disable-next-line @typescript-eslint/no-non-null-assertion
    instanceAddress: form.querySelector<HTMLInputElement>('input[name="instanceAddress"]')!,
    // eslint-disable-next-line @typescript-eslint/no-non-null-assertion
    clusterKey: form.querySelector<HTMLInputElement>('input[name="clusterKey"]')!,
    // eslint-disable-next-line @typescript-eslint/no-non-null-assertion
    replicationFrequency: form.querySelector<HTMLInputElement>(
      'input[name="replicationFrequency"]'
    )!,
    // eslint-disable-next-line @typescript-eslint/no-non-null-assertion
    replicationEnabled: form.querySelector<HTMLInputElement>('input[name="replicationEnabled"]')!
  };
  const formValues = {
    instanceType: formFields.instanceType.value,
    instanceAddress: formFields.instanceAddress.value,
    clusterKey: formFields.clusterKey.value,
    replicationFrequency: formFields.replicationFrequency.value,
    replicationEnabled: formFields.replicationEnabled.checked
  };

  return { component, formFields, formValues, backToView };
};

describe('HA replication configuration form', () => {
  it('should render form with values matching INITIAL_VALUES when no data provided', () => {
    const { component, formValues } = setup(true);
    expect(formValues).toEqual({
      instanceType: HAInstanceTypes.Active,
      instanceAddress: 'http://localhost',
      clusterKey: '',
      replicationFrequency: '1',
      replicationEnabled: true
    });
    expect(component.getByRole('button', { name: /create/i })).toBeDisabled();
  });

  it('should render form with values provided in config and schedule mocks', () => {
    const { component, formValues } = setup(true, mockConfig, mockSchedule);
    expect(formValues).toEqual({
      instanceType: mockConfig.instances[0].is_leader
        ? HAInstanceTypes.Active
        : HAInstanceTypes.Standby,
      instanceAddress: mockConfig.instances[0].address,
      clusterKey: mockConfig.cluster_key,
      replicationFrequency: String(mockSchedule.frequency_milliseconds / FREQUENCY_MULTIPLIER),
      replicationEnabled: mockSchedule.is_running
    });
    // although form is valid - the submit button should be initially disabled as form is pristine
    expect(component.getByRole('button', { name: /save/i })).toBeDisabled();
  });

  it('should show error toast on incorrect config', () => {
    const config = { instances: [{}] } as HAConfig;
    const toastError = jest.fn();
    jest.spyOn(toast, 'error').mockImplementation(toastError);
    setup(true, config, {} as HAReplicationSchedule);
    expect(toastError).toBeCalled();
  });

  it('should change the view on switching from active to standy mode', () => {
    const { component } = setup(true);

    // check active mode view
    expect(component.queryByRole('alert')).not.toBeInTheDocument();
    expect(component.getByTestId('ha-replication-config-form-schedule-section')).toBeVisible();

    // switch to standby mode
    userEvent.click(component.getByText(/standby/i));

    // check standby mode view
    expect(component.getByRole('alert')).toBeInTheDocument();
    expect(component.queryByRole('button', { name: /generate key/i })).not.toBeInTheDocument();
    expect(component.getByTestId('ha-replication-config-form-schedule-section')).not.toBeVisible();
  });

  it('should disable all form fields in edit mode when replication toggle is off', () => {
    const { component } = setup(true, mockConfig, mockSchedule);
    component.getAllByRole('radio').forEach((element) => expect(element).toBeDisabled());
    component.getAllByRole('textbox').forEach((element) => expect(element).toBeDisabled());
  });

  it('should not show any validation messages initially', () => {
    const { component } = setup(true);
    component
      .queryAllByTestId('yb-label-validation-error')
      .forEach((element) => expect(element).not.toBeInTheDocument());
  });

  // formik validation is async, therefore use async/await
  it('should validate address field', async () => {
    const { component, formFields } = setup(true);

    userEvent.clear(formFields.instanceAddress);
    fireEvent.blur(formFields.instanceAddress);
    expect(await component.findByText(/required field/i)).toBeInTheDocument();

    userEvent.clear(formFields.instanceAddress);
    userEvent.type(formFields.instanceAddress, 'lorem ipsum');
    expect(await component.findByText(/should be a valid url/i)).toBeInTheDocument();

    userEvent.clear(formFields.instanceAddress);
    userEvent.type(formFields.instanceAddress, 'http://valid.url');
    expect(await component.findByText(/should be a valid url/i)).not.toBeInTheDocument();
  });

  it('should validate cluster key field', async () => {
    const { component, formFields } = setup(true);

    // that fields is editable in standby mode only
    userEvent.click(component.getByText(/standby/i));

    fireEvent.blur(formFields.clusterKey);
    expect(await component.findByText(/required field/i)).toBeInTheDocument();

    userEvent.type(formFields.clusterKey, 'some-fake-key');
    expect(await component.findByText(/required field/i)).not.toBeInTheDocument();
  });

  it('should validate replication frequency field', async () => {
    const { component, formFields } = setup(true);

    // check for required value validation
    userEvent.clear(formFields.replicationFrequency);
    fireEvent.blur(formFields.replicationFrequency);
    expect(await component.findByText(/required field/i)).toBeInTheDocument();

    // disable frequency input and make sure previously visible error message is gone
    userEvent.click(formFields.replicationEnabled);
    expect(formFields.replicationFrequency).toBeDisabled();
    expect(await component.findByText(/required field/i)).not.toBeInTheDocument();

    // enable frequency input back and assure it's enabled
    userEvent.click(formFields.replicationEnabled);
    expect(formFields.replicationFrequency).toBeEnabled();

    // check for min value validation
    userEvent.clear(formFields.replicationFrequency);
    userEvent.type(formFields.replicationFrequency, '-1');
    expect(await component.findByText(/minimum value is 1/i)).toBeInTheDocument();

    // check if it filters non-number input and validation message is gone
    userEvent.clear(formFields.replicationFrequency);
    userEvent.type(formFields.replicationFrequency, 'qwerty 123');
    expect(formFields.replicationFrequency).toHaveValue(123);
    expect(await component.findByTestId('yb-label-validation-error')).not.toBeInTheDocument();
  });

  it('should disable submit button on validation failure', async () => {
    const { component, formFields } = setup(true, mockConfig, mockSchedule);

    // enable frequency field, type smth and check that submit button become enabled
    userEvent.click(formFields.replicationEnabled);
    userEvent.type(formFields.replicationFrequency, '10');
    fireEvent.blur(formFields.replicationFrequency);
    expect(component.getByRole('button', { name: /save/i })).toBeEnabled();

    // force validation error and check if submit button become disabled
    userEvent.clear(formFields.replicationFrequency);
    fireEvent.blur(formFields.replicationFrequency);
    expect(await component.findByRole('button', { name: /save/i })).toBeDisabled();
  });

  it('should check active config creation happy path', async () => {
    const fakeValues = {
      configId: 'fake-config-id',
      instanceAddress: 'http://fake-address',
      clusterKey: 'fake-key',
      replicationFrequency: '30'
    };

    (api.generateHAKey as jest.Mock).mockResolvedValue({ cluster_key: fakeValues.clusterKey });
    (api.createHAConfig as jest.Mock).mockResolvedValue({ uuid: fakeValues.configId });
    (api.createHAInstance as jest.Mock).mockResolvedValue({});
    (api.startHABackupSchedule as jest.Mock).mockResolvedValue({});

    const { component, formFields, backToView } = setup(true);

    // enter address
    userEvent.clear(formFields.instanceAddress);
    userEvent.type(formFields.instanceAddress, fakeValues.instanceAddress);

    // generate cluster key and check form value
    userEvent.click(component.queryByRole('button', { name: /generate key/i })!);
    await waitFor(() => expect(api.generateHAKey).toBeCalled());
    expect(formFields.clusterKey).toHaveValue(fakeValues.clusterKey);

    // set replication frequency
    userEvent.clear(formFields.replicationFrequency);
    userEvent.type(formFields.replicationFrequency, fakeValues.replicationFrequency);

    // click the submit button
    expect(component.getByRole('button', { name: /create/i })).toBeEnabled();
    userEvent.click(component.getByRole('button', { name: /create/i }));

    await waitFor(() => {
      expect(api.createHAConfig).toBeCalledWith(fakeValues.clusterKey);
      expect(api.createHAInstance).toBeCalledWith(
        fakeValues.configId,
        fakeValues.instanceAddress,
        true,
        true
      );
      expect(api.startHABackupSchedule).toBeCalledWith(
        fakeValues.configId,
        Number(fakeValues.replicationFrequency) * FREQUENCY_MULTIPLIER
      );
    });

    expect(backToView).toBeCalled();
  });

  it('should check standby config creation happy path', async () => {
    const fakeValues = {
      configId: 'fake-config-id',
      instanceAddress: 'http://fake-address',
      clusterKey: 'fake-key'
    };

    (api.createHAConfig as jest.Mock).mockResolvedValue({ uuid: fakeValues.configId });
    (api.createHAInstance as jest.Mock).mockResolvedValue({});

    const { component, formFields, backToView } = setup(true);

    // select standby mode
    userEvent.click(component.getByText(/standby/i));

    // enter address
    userEvent.clear(formFields.instanceAddress);
    userEvent.type(formFields.instanceAddress, fakeValues.instanceAddress);

    // enter cluster key
    userEvent.type(formFields.clusterKey, fakeValues.clusterKey);

    // click the submit button
    expect(component.getByRole('button', { name: /create/i })).toBeEnabled();
    userEvent.click(component.getByRole('button', { name: /create/i }));

    await waitFor(() => {
      expect(api.createHAConfig).toBeCalledWith(fakeValues.clusterKey);
      expect(api.createHAInstance).toBeCalledWith(
        fakeValues.configId,
        fakeValues.instanceAddress,
        false,
        true
      );
    });

    expect(backToView).toBeCalled();
  });
  it('should disable the submit button for active config if peer certs do not exist and using https', async () => {
    const fakeValues = {
      configId: 'fake-config-id',
      instanceAddress: 'https://fake-address',
      clusterKey: 'fake-key',
      replicationFrequency: '30'
    };
    (api.generateHAKey as jest.Mock).mockResolvedValue({ cluster_key: fakeValues.clusterKey });

    const { component, formFields } = setup(false);

    // enter address
    userEvent.clear(formFields.instanceAddress);
    userEvent.type(formFields.instanceAddress, fakeValues.instanceAddress);

    // generate cluster key and check form value
    userEvent.click(component.queryByRole('button', { name: /generate key/i })!);
    await waitFor(() => expect(api.generateHAKey).toBeCalled());
    expect(formFields.clusterKey).toHaveValue(fakeValues.clusterKey);

    // set replication frequency
    userEvent.clear(formFields.replicationFrequency);
    userEvent.type(formFields.replicationFrequency, fakeValues.replicationFrequency);

    // Verify the submit button is disabled (since no peer certs were added).
    expect(component.getByRole('button', { name: /create/i })).toBeDisabled();
  });
  it('should not disable the submit button for active config if peer certs do not exist and not using https', async () => {
    const fakeValues = {
      configId: 'fake-config-id',
      instanceAddress: 'http://fake-address',
      clusterKey: 'fake-key',
      replicationFrequency: '30'
    };
    (api.generateHAKey as jest.Mock).mockResolvedValue({ cluster_key: fakeValues.clusterKey });

    const { component, formFields } = setup(false);

    // enter address
    userEvent.clear(formFields.instanceAddress);
    userEvent.type(formFields.instanceAddress, fakeValues.instanceAddress);

    // generate cluster key and check form value
    userEvent.click(component.queryByRole('button', { name: /generate key/i })!);
    await waitFor(() => expect(api.generateHAKey).toBeCalled());
    expect(formFields.clusterKey).toHaveValue(fakeValues.clusterKey);

    // set replication frequency
    userEvent.clear(formFields.replicationFrequency);
    userEvent.type(formFields.replicationFrequency, fakeValues.replicationFrequency);

    // Verify the submit button is disabled (since no peer certs were added).
    expect(component.getByRole('button', { name: /create/i })).toBeEnabled();
  });
  it('should not disable the submit button for standby config if peer certs do not exist and https instance', async () => {
    const fakeValues = {
      configId: 'fake-config-id',
      instanceAddress: 'https://fake-address',
      clusterKey: 'fake-key'
    };
    const { component, formFields } = setup(false);

    // select standby mode
    userEvent.click(component.getByText(/standby/i));

    // enter address
    userEvent.clear(formFields.instanceAddress);
    userEvent.type(formFields.instanceAddress, fakeValues.instanceAddress);

    // enter cluster key
    userEvent.type(formFields.clusterKey, fakeValues.clusterKey);

    // Verify the submit button is disabled (since no peer certs were added).
    expect(component.getByRole('button', { name: /create/i })).toBeEnabled();
  });

  it('should check disabling replication happy flow', async () => {
    (api.stopHABackupSchedule as jest.Mock).mockResolvedValue({});

    const { component, formFields, backToView } = setup(true, mockConfig, mockSchedule);

    // make dummy changes to form to enable submit button with turned off replication toggle
    userEvent.click(formFields.replicationEnabled);
    userEvent.type(formFields.replicationFrequency, '1');
    userEvent.click(formFields.replicationEnabled);

    expect(component.getByRole('button', { name: /save/i })).toBeEnabled();

    userEvent.click(component.getByRole('button', { name: /save/i }));
    await waitFor(() => expect(api.stopHABackupSchedule).toBeCalledWith(undefined));
    expect(backToView).toBeCalled();
  });

  it('should show toast on api call failure', async () => {
    (api.startHABackupSchedule as jest.Mock).mockRejectedValue({});
    const toastError = jest.fn();
    jest.spyOn(toast, 'error').mockImplementation(toastError);
    const consoleError = jest.fn();
    jest.spyOn(console, 'error').mockImplementation(consoleError);

    const { component, formFields, backToView } = setup(true, mockConfig, mockSchedule);

    userEvent.click(formFields.replicationEnabled);
    userEvent.click(component.getByRole('button', { name: /save/i }));
    await waitFor(() => {
      expect(toastError).toBeCalled();
      expect(consoleError).toBeCalled();
    });
    expect(backToView).not.toBeCalled();
  });
});
