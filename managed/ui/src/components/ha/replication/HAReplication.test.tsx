import userEvent from '@testing-library/user-event';
import { HAReplication } from './HAReplication';
import { render } from '../../../test-utils';
import { useLoadHAConfiguration } from '../hooks/useLoadHAConfiguration';
import { HAConfig, HAReplicationSchedule } from '../../../redesign/helpers/dtos';

jest.mock('../hooks/useLoadHAConfiguration');

type HookReturnType = Partial<ReturnType<typeof useLoadHAConfiguration>>;

const setup = (hookResponse: HookReturnType) => {
  (useLoadHAConfiguration as jest.Mock<HookReturnType>).mockReturnValue(hookResponse);
  return render(<HAReplication />);
};

const mockConfig: HAConfig = {
  uuid: 'fake-uuid-111',
  cluster_key: 'fake-key',
  last_failover: 111,
  instances: [
    {
      uuid: 'fake-uuid-222',
      config_uuid: 'fake-uuid-333',
      address: 'fake-address',
      is_leader: true,
      is_local: true,
      last_backup: null
    }
  ]
};
const mockSchedule: HAReplicationSchedule = {
  frequency_milliseconds: 60000,
  is_running: true
};

describe('HA replication configuration parent component', () => {
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
  // Skipping for now. HAReplicationFormContainer is a connected ccomponent.
  xit('should render form with create button when there is no HA configuration', () => {
    const component = setup({ isNoHAConfigExists: true });
    expect(component.getByTestId('ha-replication-config-form')).toBeInTheDocument();
    expect(component.getByRole('button', { name: /create/i })).toBeInTheDocument();
  });
  // Skipping for now. HAReplicationViewContainer is a connected ccomponent.
  xit('should render config overview when there is HA configuration', () => {
    const component = setup({ config: mockConfig, schedule: mockSchedule });
    expect(component.getByTestId('ha-replication-config-overview')).toBeInTheDocument();
  });

  xit('should switch from overview to edit mode and back', () => {
    const component = setup({ config: mockConfig, schedule: mockSchedule });

    userEvent.click(component.getByRole('button', { name: /edit configuration/i }));
    expect(component.getByTestId('ha-replication-config-form')).toBeInTheDocument();
    expect(component.getByRole('button', { name: /save/i })).toBeInTheDocument();

    userEvent.click(component.getByRole('button', { name: /cancel/i }));
    expect(component.getByTestId('ha-replication-config-overview')).toBeInTheDocument();
  });
});
