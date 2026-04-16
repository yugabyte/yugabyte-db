import type { Mock } from 'vitest';
import { browserHistory } from 'react-router';
import userEvent from '@testing-library/user-event';
import { toast } from 'react-toastify';
import { render, waitFor } from '@testing-library/react';
import { QueryClient, QueryClientProvider } from 'react-query';
import { Provider } from 'react-redux';
import { createStore } from 'redux';
import { api } from '../../../redesign/helpers/api';
import { PromoteInstanceModal } from './PromoteInstanceModal';
import { ThemeProvider } from '@material-ui/core';
import { mainTheme } from '../../../redesign/theme/mainTheme';

vi.mock('../../../redesign/helpers/api');

const fakeConfigId = 'aaa-111';
const fakeInstanceId = 'bbb-222';
const fakeBackupsList = [
  'backup_21-03-24-21-30.tgz',
  'backup_21-03-24-21-29.tgz',
  'backup_21-03-24-21-27.tgz'
];
const PROMOTE_CONFIRMATION_STRING = 'PROMOTE';

const setup = () => {
  const queryClient = new QueryClient({
    defaultOptions: { queries: { retry: false } }
  });
  const store = createStore(() => ({
    customer: { currentUser: { data: { timezone: 'UTC' } } }
  }));
  const onClose = vi.fn();
  const component = render(
    <Provider store={store}>
      <QueryClientProvider client={queryClient}>
        <ThemeProvider theme={mainTheme}>
          <PromoteInstanceModal
            visible
            onClose={onClose}
            configId={fakeConfigId}
            instanceId={fakeInstanceId}
          />
        </ThemeProvider>
      </QueryClientProvider>
    </Provider>
  );

  return { component, onClose };
};

/** Helper: wait for the loading spinner to disappear */
const waitForLoaded = async (component: ReturnType<typeof render>) => {
  await waitFor(() => {
    expect(component.queryByText(/loading/i)).not.toBeInTheDocument();
  });
};

describe('HA promote instance modal', () => {
  it('should render and show loading indicator initially', () => {
    const { component } = setup();
    expect(component.getByTestId('ha-make-active-modal')).toBeInTheDocument();
    expect(component.getByText(/loading/i)).toBeInTheDocument();
  });

  it('should trigger onClose by Cancel button click', async () => {
    const { component, onClose } = setup();
    await userEvent.click(component.getByRole('button', { name: /cancel/i }));
    expect(onClose).toBeCalled();
  });

  it('should load backups and pre-select first item', async () => {
    (api.getHABackups as Mock).mockResolvedValue(fakeBackupsList);
    const { component } = setup();
    // Wait for loading to complete and the API to be called
    await waitForLoaded(component);
    await waitFor(() => expect(api.getHABackups).toBeCalledWith(fakeConfigId));
    // After loading, the first backup should be pre-selected in the hidden react-select input
    await waitFor(() => {
      expect(document.querySelector('input[name="backupFile"]')).toHaveValue(fakeBackupsList[0]);
    });
  });

  it('should show validation error when no backup selected', async () => {
    (api.getHABackups as Mock).mockResolvedValue([]);
    const { component } = setup();
    // Wait for loading to complete before interacting with form
    await waitForLoaded(component);
    await userEvent.type(
      component.getByTestId('PromoteInstanceModal-ConfirmTextInputField'),
      PROMOTE_CONFIRMATION_STRING
    );
    await userEvent.click(component.getByRole('button', { name: /continue/i }));
    expect(await component.findByText(/backup file is required/i)).toBeInTheDocument();
  });

  it('should make API call and close modal', async () => {
    // mock environment
    const browserHistoryPush = vi.fn();
    vi.spyOn(browserHistory, 'push').mockImplementation(browserHistoryPush);
    // Use a deferred promise so the mutation stays pending until we resolve it
    let resolvePromote!: () => void;
    const pendingPromise = new Promise<void>((resolve) => {
      resolvePromote = resolve;
    });
    (api.promoteHAInstance as Mock).mockReturnValue(pendingPromise);
    (api.getHABackups as Mock).mockResolvedValue(fakeBackupsList);

    // render component and wait for backups to load
    const { component, onClose } = setup();
    await waitForLoaded(component);

    // click continue button without entering confirmation text - button is disabled
    await userEvent.click(component.getByRole('button', { name: /continue/i }));
    expect(api.promoteHAInstance).not.toBeCalled();
    expect(browserHistoryPush).not.toBeCalled();

    // enter confirmation text and then click continue button
    await userEvent.type(
      component.getByTestId('PromoteInstanceModal-ConfirmTextInputField'),
      PROMOTE_CONFIRMATION_STRING
    );
    await userEvent.click(component.getByRole('button', { name: /continue/i }));

    // make sure modal can't be closed while API response is pending
    await userEvent.click(component.getByRole('button', { name: /cancel/i }));
    expect(onClose).not.toBeCalled();

    // resolve the mocked API call
    resolvePromote();
    await waitFor(() => {
      expect(api.promoteHAInstance).toBeCalledWith(
        fakeConfigId,
        fakeInstanceId,
        false /* isForcePromote */,
        { backup_file: fakeBackupsList[0] }
      );
      expect(browserHistoryPush).toBeCalledWith('/login');
    });
  });

  it('should show an error toast on promotion API call failure', async () => {
    const browserHistoryPush = vi.fn();
    vi.spyOn(browserHistory, 'push').mockImplementation(browserHistoryPush);
    const toastError = vi.fn();
    vi.spyOn(toast, 'error').mockImplementation(toastError);
    (api.promoteHAInstance as Mock).mockRejectedValue(new Error('Could not find leader instance'));
    (api.getHABackups as Mock).mockResolvedValue(fakeBackupsList);

    const { component } = setup();
    await waitForLoaded(component);

    await userEvent.type(
      component.getByTestId('PromoteInstanceModal-ConfirmTextInputField'),
      PROMOTE_CONFIRMATION_STRING
    );
    await userEvent.click(component.getByRole('button', { name: /continue/i }));

    await waitFor(() => {
      expect(api.promoteHAInstance).toBeCalledWith(
        fakeConfigId,
        fakeInstanceId,
        false /* isForcePromote */,
        { backup_file: fakeBackupsList[0] }
      );
      expect(browserHistoryPush).not.toBeCalled();
      expect(toastError).toBeCalled();
    });
  });
});
