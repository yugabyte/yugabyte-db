import type { Mock } from 'vitest';
import userEvent from '@testing-library/user-event';
import { render, waitFor } from '@testing-library/react';
import { toast } from 'react-toastify';
import { QueryClient, QueryClientProvider } from 'react-query';
import { api } from '../../../redesign/helpers/api';
import { DeleteModal } from './DeleteModal';

vi.mock('../../../redesign/helpers/api');

const fakeConfigId = 'aaa-111';

const setup = (instanceId?: string) => {
  const onClose = vi.fn();
  const queryClient = new QueryClient({
    defaultOptions: { queries: { retry: false } }
  });
  const component = render(
    <QueryClientProvider client={queryClient}>
      <DeleteModal visible onClose={onClose} configId={fakeConfigId} instanceId={instanceId} />
    </QueryClientProvider>
  );
  return { component, onClose };
};

describe('HA delete modal', () => {
  it('should render', () => {
    const { component } = setup();
    expect(component.getByTestId('ha-delete-confirmation-modal')).toBeInTheDocument();
  });

  it('should trigger onClose by Cancel button click', async () => {
    const { component, onClose } = setup();
    await userEvent.click(component.getByRole('button', { name: /cancel/i }));
    expect(onClose).toBeCalled();
  });

  it('should make an API call to delete config and close modal', async () => {
    let resolveDelete!: () => void;
    const pendingPromise = new Promise<void>((resolve) => {
      resolveDelete = resolve;
    });
    (api.deleteHAConfig as Mock).mockReturnValue(pendingPromise);

    const { component, onClose } = setup();
    await userEvent.click(component.getByRole('button', { name: /continue/i }));

    // make sure modal can't be closed while API response is pending
    await userEvent.click(component.getByRole('button', { name: /cancel/i }));
    expect(onClose).not.toBeCalled();

    resolveDelete();
    await waitFor(() => {
      expect(api.deleteHAConfig).toBeCalledWith(fakeConfigId);
      expect(onClose).toBeCalled();
    });
  });

  it('should make an API call to delete instance and close modal', async () => {
    (api.deleteHAInstance as Mock).mockResolvedValue({});
    const fakeInstanceId = 'fake-instance-id-111';

    const { component, onClose } = setup(fakeInstanceId);
    await userEvent.click(component.getByRole('button', { name: /continue/i }));

    await waitFor(() => {
      expect(api.deleteHAInstance).toBeCalledWith(fakeConfigId, fakeInstanceId);
      expect(onClose).toBeCalled();
    });
  });

  it('should show an error toast on API failure', async () => {
    const toastError = vi.fn();
    vi.spyOn(toast, 'error').mockImplementation(toastError);
    const consoleError = vi.fn();
    vi.spyOn(console, 'error').mockImplementation(consoleError);
    (api.deleteHAConfig as Mock).mockRejectedValue({});

    const { component, onClose } = setup();
    await userEvent.click(component.getByRole('button', { name: /continue/i }));

    await waitFor(() => {
      expect(api.deleteHAConfig).toBeCalledWith(fakeConfigId);
      expect(toastError).toBeCalled();
      expect(onClose).toBeCalled();
    });
  });
});
