import _ from 'lodash';
import userEvent from '@testing-library/user-event';
import { render } from '@testing-library/react';
import { QueryClient, QueryClientProvider } from 'react-query';
import { Provider } from 'react-redux';
import { createStore } from 'redux';

// Mock the container to break circular dependency:
// HAReplicationView -> PromoteInstanceModal -> ... -> HAReplication -> HAReplicationViewContainer -> HAReplicationView
vi.mock('./HAReplicationViewContainer', () => ({ HAReplicationViewContainer: () => null }));

import { HAReplicationView } from './HAReplicationView';
import { HaConfig, HaReplicationSchedule } from '../dtos';
import { MOCK_HA_WS_RUNTIME_CONFIG } from './mockUtils';

const mockConfig: HaConfig = {
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
const mockSchedule: HaReplicationSchedule = {
  frequency_milliseconds: 60000,
  is_running: true
};

const setup = (config?: HaConfig) => {
  const fetchRunTimeConfigs = vi.fn();
  const setRunTimeConfig = vi.fn();
  const queryClient = new QueryClient({
    defaultOptions: { queries: { retry: false } }
  });
  const store = createStore(() => ({
    customer: { currentUser: { data: { timezone: 'UTC' } } }
  }));
  return render(
    <Provider store={store}>
      <QueryClientProvider client={queryClient}>
        <HAReplicationView
          haConfig={config ?? mockConfig}
          schedule={mockSchedule}
          editConfig={() => {}}
          runtimeConfigs={MOCK_HA_WS_RUNTIME_CONFIG}
          fetchRuntimeConfigs={fetchRunTimeConfigs}
          setRuntimeConfig={setRunTimeConfig}
        />
      </QueryClientProvider>
    </Provider>
  );
};

describe('HA replication configuration overview', () => {
  it('should render active configuration properly', () => {
    const component = setup();
    expect(component.getByText(/replication frequency/i)).toBeInTheDocument();
    expect(component.getByText(/enable replication/i)).toBeInTheDocument();
    expect(component.getByRole('button', { name: /actions/i })).toBeInTheDocument();
    expect(component.queryByRole('button', { name: /make active/i })).not.toBeInTheDocument();
  });

  it('should render standby configuration properly', () => {
    const config = _.cloneDeep(mockConfig);
    config.instances.forEach((item) => (item.is_leader = false)); // mark all instances as standby
    const component = setup(config);

    expect(component.queryByText(/replication frequency/i)).not.toBeInTheDocument();
    expect(component.queryByText(/enable replication/i)).not.toBeInTheDocument();
    expect(component.queryByRole('button', { name: /actions/i })).not.toBeInTheDocument();
    expect(component.getByRole('button', { name: /make active/i })).toBeInTheDocument();
  });

  it('should render cluster topology in correct order', () => {
    const component = setup();
    const instances = Array.from(
      component.container.querySelectorAll('.ha-replication-view__topology-col--address')
    ).map((item) => item.textContent);

    // should put active instance on top and sort standby instances by address
    expect(instances).toEqual([
      mockConfig.instances[1].address,
      mockConfig.instances[2].address,
      mockConfig.instances[0].address
    ]);
  });

  it('should render a modal on when clicking the delete configuration menu item', async () => {
    const component = setup();

    // check if modal opens
    await userEvent.click(component.getByRole('button', { name: /actions/i }));
    await userEvent.click(component.getByRole('menuitem', { name: /delete configuration/i }));
    expect(component.getByTestId('ha-delete-confirmation-modal')).toBeInTheDocument();

    // check if modal closes
    await userEvent.click(component.getByRole('button', { name: /cancel/i }));
    expect(component.queryByTestId('ha-delete-confirmation-modal')).not.toBeInTheDocument();
  });

  it('should render a modal on click at the promotion button', async () => {
    const config = _.cloneDeep(mockConfig);
    config.instances.forEach((item) => (item.is_leader = false)); // mark all instances as standby
    const component = setup(config);

    // check if modal opens
    await userEvent.click(component.getByRole('button', { name: /make active/i }));
    expect(component.getByTestId('ha-make-active-modal')).toBeInTheDocument();

    // check if modal closes
    await userEvent.click(component.getByRole('button', { name: /cancel/i }));
    expect(component.queryByTestId('ha-make-active-modal')).not.toBeInTheDocument();
  });

  it('should show generic error message on incorrect config', () => {
    const consoleError = vi.fn();
    vi.spyOn(console, 'error').mockImplementation(consoleError);
    const component = setup({} as HaConfig);

    expect(component.getByTestId('ha-generic-error')).toBeInTheDocument();
    expect(consoleError).toBeCalled();
  });
});
