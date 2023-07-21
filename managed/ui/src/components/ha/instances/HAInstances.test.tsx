import _ from 'lodash';
import userEvent from '@testing-library/user-event';
import { render } from '../../../test-utils';
import { HAInstances } from './HAInstances';
import { useLoadHAConfiguration } from '../hooks/useLoadHAConfiguration';
import { HAConfig } from '../../../redesign/helpers/dtos';
import { MOCK_HA_WS_RUNTIME_CONFIG_WITH_PEER_CERTS } from '../replication/mockUtils';

jest.mock('../hooks/useLoadHAConfiguration');

type HookReturnType = Partial<ReturnType<typeof useLoadHAConfiguration>>;

const mockConfig: HAConfig = {
  uuid: 'config-id-1',
  cluster_key: 'fake-key',
  last_failover: 123,
  instances: [
    {
      uuid: 'instance-id-1',
      config_uuid: 'config-id-1',
      address: 'standby-B',
      is_leader: false,
      is_local: false,
      last_backup: null
    },
    {
      uuid: 'instance-id-2',
      config_uuid: 'config-id-1',
      address: 'active',
      is_leader: true, // means this is an active instance item
      is_local: true, // means user logged into this instance
      last_backup: null
    },
    {
      uuid: 'instance-id-3',
      config_uuid: 'config-id-1',
      address: 'standby-A',
      is_leader: false,
      is_local: false,
      last_backup: null
    }
  ]
};

const setup = (hookResponse: HookReturnType) => {
  (useLoadHAConfiguration as jest.Mock<HookReturnType>).mockReturnValue(hookResponse);
  const fetchRuntimeConfigs = jest.fn();
  const setRuntimeConfig = jest.fn();

  const mockRuntimeConfigPromise = {
    data: MOCK_HA_WS_RUNTIME_CONFIG_WITH_PEER_CERTS,
    error: null,
    promiseState: 'SUCCESS'
  };
  return render(
    <HAInstances
      fetchRuntimeConfigs={fetchRuntimeConfigs}
      setRuntimeConfig={setRuntimeConfig}
      runtimeConfigs={mockRuntimeConfigPromise}
    />
  );
};

describe('HA instances list', () => {
  it('should render loading indicator', () => {
    const component = setup({ isLoading: true });
    expect(component.getByText(/loading/i)).toBeInTheDocument();
  });

  it('should render error component on failure', () => {
    const error = 'test-error-text';
    const consoleError = jest.fn();
    jest.spyOn(console, 'error').mockImplementation(consoleError);
    const component = setup({ error });

    expect(component.getByTestId('ha-generic-error')).toBeInTheDocument();
    expect(consoleError).toBeCalledWith(error);
  });

  it('should render placeholder when there is no HA configuration yet', () => {
    const component = setup({ isNoHAConfigExists: true });
    expect(component.getByTestId('ha-instances-no-config')).toBeInTheDocument();
  });

  it('should render provided instances from config in correct order', () => {
    const component = setup({ config: mockConfig });
    // instances number + 1 row with table header
    expect(component.getAllByRole('row')).toHaveLength(mockConfig.instances.length + 1);
  });

  it('should show and close add instance modal', () => {
    const component = setup({ config: mockConfig });

    userEvent.click(component.getByRole('button', { name: /add instance/i }));
    expect(component.getByTestId('ha-add-standby-instance-modal')).toBeInTheDocument();

    userEvent.click(component.getByRole('button', { name: /cancel/i }));
    expect(component.queryByTestId('ha-add-standby-instance-modal')).not.toBeInTheDocument();
  });

  it('should show and close delete modal', () => {
    const component = setup({ config: mockConfig });

    const deleteButton = component
      .getAllByRole('button', { name: /delete instance/i })
      .find((item) => !(item as HTMLButtonElement).disabled);

    if (deleteButton) {
      userEvent.click(deleteButton);
      expect(component.getByTestId('ha-delete-confirmation-modal')).toBeInTheDocument();

      userEvent.click(component.getByRole('button', { name: /close/i }));
      expect(component.queryByTestId('ha-delete-confirmation-modal')).not.toBeInTheDocument();
    } else {
      throw new Error('No enabled delete button found');
    }
  });

  it('should show and close make active modal', () => {
    const config = _.cloneDeep(mockConfig);
    config.instances.forEach((item) => (item.is_leader = false)); // mark all instances as standby
    const component = setup({ config });

    userEvent.click(component.getByRole('button', { name: /make active/i }));
    expect(component.getByTestId('ha-make-active-modal')).toBeInTheDocument();

    userEvent.click(component.getByRole('button', { name: /cancel/i }));
    expect(component.queryByTestId('ha-make-active-modal')).not.toBeInTheDocument();
  });
});
