import userEvent from '@testing-library/user-event';
import { toast } from 'react-toastify';
import { render, waitFor } from '../../../test-utils';
import { api } from '../../../redesign/helpers/api';
import { DeleteModal } from './DeleteModal';

jest.mock('../../../redesign/helpers/api');

const fakeConfigId = 'aaa-111';

const setup = (instanceId?: string) => {
  const onClose = jest.fn();
  const component = render(
    <DeleteModal visible onClose={onClose} configId={fakeConfigId} instanceId={instanceId} />
  );
  return { component, onClose };
};

describe('HA delete modal', () => {
  it('should render', () => {
    const { component } = setup();
    expect(component.getByTestId('ha-delete-confirmation-modal')).toBeInTheDocument();
  });

  it('should trigger onClose by Cancel button click', () => {
    const { component, onClose } = setup();
    userEvent.click(component.getByRole('button', { name: /cancel/i }));
    expect(onClose).toBeCalled();
  });

  it('should make an API call to delete config and close modal', async () => {
    const promise = Promise;
    (api.deleteHAConfig as jest.Mock).mockReturnValue(promise);

    const { component, onClose } = setup();
    userEvent.click(component.getByRole('button', { name: /continue/i }));

    // make sure modal can't be closed while API response is pending
    userEvent.click(component.getByRole('button', { name: /cancel/i }));
    expect(onClose).not.toBeCalled();

    await waitFor(() => {
      promise.resolve();
      expect(api.deleteHAConfig).toBeCalledWith(fakeConfigId);
      expect(onClose).toBeCalled();
    });
  });

  it('should make an API call to delete instance and close modal', async () => {
    (api.deleteHAInstance as jest.Mock).mockResolvedValue({});
    const fakeInstanceId = 'fake-instance-id-111';

    const { component, onClose } = setup(fakeInstanceId);
    userEvent.click(component.getByRole('button', { name: /continue/i }));

    await waitFor(() => {
      expect(api.deleteHAInstance).toBeCalledWith(fakeConfigId, fakeInstanceId);
      expect(onClose).toBeCalled();
    });
  });

  it('should show an error toast on API failure', async () => {
    const toastError = jest.fn();
    jest.spyOn(toast, 'error').mockImplementation(toastError);
    const consoleError = jest.fn();
    jest.spyOn(console, 'error').mockImplementation(consoleError);
    (api.deleteHAConfig as jest.Mock).mockRejectedValue({});

    const { component, onClose } = setup();
    userEvent.click(component.getByRole('button', { name: /continue/i }));

    await waitFor(() => {
      expect(api.deleteHAConfig).toBeCalledWith(fakeConfigId);
      expect(toastError).toBeCalled();
      expect(consoleError).toBeCalled();
      expect(onClose).toBeCalled();
    });
  });
});
