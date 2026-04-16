import type { Mock } from 'vitest';
import userEvent from '@testing-library/user-event';
import { toast } from 'react-toastify';
import { render, waitFor } from '@testing-library/react';
import { QueryClient, QueryClientProvider } from 'react-query';
import { AddStandbyInstanceModal } from './AddStandbyInstanceModal';
import { api } from '../../../redesign/helpers/api';
import { MOCK_HA_WS_RUNTIME_CONFIG_WITH_PEER_CERTS } from '../replication/mockUtils';

vi.mock('../../../redesign/helpers/api');

const fakeConfigId = 'aaa-111';

const setup = () => {
  const onClose = vi.fn();
  const fetchRuntimeConfigs = vi.fn();
  const setRuntimeConfig = vi.fn();

  const mockRuntimeConfigPromise = {
    data: MOCK_HA_WS_RUNTIME_CONFIG_WITH_PEER_CERTS,
    error: null,
    promiseState: 'SUCCESS'
  };

  const queryClient = new QueryClient({
    defaultOptions: { queries: { retry: false } }
  });
  const component = render(
    <QueryClientProvider client={queryClient}>
      <AddStandbyInstanceModal
        visible
        onClose={onClose}
        configId={fakeConfigId}
        runtimeConfigs={mockRuntimeConfigPromise}
        fetchRuntimeConfigs={fetchRuntimeConfigs}
        setRuntimeConfig={setRuntimeConfig}
      />
    </QueryClientProvider>
  );
  return { component, onClose };
};

describe('HA add standby instance modal', () => {
  it('should render', () => {
    const { component } = setup();
    expect(component.getByTestId('ha-add-standby-instance-modal')).toBeInTheDocument();
  });

  it('should trigger onClose by Cancel button click', async () => {
    const { component, onClose } = setup();
    await userEvent.click(component.getByRole('button', { name: /cancel/i }));
    expect(onClose).toBeCalled();
  });

  it('should validate instance address input', async () => {
    const { component, onClose } = setup();

    await userEvent.click(component.getByRole('button', { name: /continue/i }));
    expect(await component.findByText(/required field/i)).toBeInTheDocument();
    expect(onClose).not.toBeCalled();

    await userEvent.type(component.getByRole('textbox'), 'lorem ipsum');
    expect(await component.findByText(/must be a valid URL/i)).toBeInTheDocument();
    expect(onClose).not.toBeCalled();
  });

  it('should make an API call and close modal', async () => {
    let resolveApi!: () => void;
    const pendingPromise = new Promise<void>((resolve) => {
      resolveApi = resolve;
    });
    (api.createHAInstance as Mock).mockReturnValue(pendingPromise);
    const fakeAddress = 'http://valid.url';

    const { component, onClose } = setup();

    await userEvent.type(component.getByRole('textbox'), fakeAddress);
    await userEvent.click(component.getByRole('button', { name: /continue/i }));

    // make sure modal can't be closed while API response is pending
    await userEvent.click(component.getByRole('button', { name: /cancel/i }));
    expect(onClose).not.toBeCalled();

    resolveApi();
    await waitFor(() => {
      expect(api.createHAInstance).toBeCalledWith(fakeConfigId, fakeAddress, false, false);
      expect(onClose).toBeCalled();
    });
  });

  it('should show an error toast on API failure', async () => {
    const toastError = vi.fn();
    vi.spyOn(toast, 'error').mockImplementation(toastError);
    const consoleError = vi.fn();
    vi.spyOn(console, 'error').mockImplementation(consoleError);
    (api.createHAInstance as Mock).mockRejectedValue({});
    const fakeAddress = 'http://valid.url';

    const { component, onClose } = setup();

    await userEvent.type(component.getByRole('textbox'), fakeAddress);
    await userEvent.click(component.getByRole('button', { name: /continue/i }));

    await waitFor(() => {
      expect(api.createHAInstance).toBeCalledWith(fakeConfigId, fakeAddress, false, false);
      expect(toastError).toBeCalled();
      expect(consoleError).toBeCalled();
      expect(onClose).toBeCalled();
    });
  });
});
