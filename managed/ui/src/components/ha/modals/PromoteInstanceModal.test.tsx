import { browserHistory } from 'react-router';
import userEvent from '@testing-library/user-event';
import { toast } from 'react-toastify';
import { render, waitFor } from '../../../test-utils';
import { api } from '../../../redesign/helpers/api';
import { PromoteInstanceModal } from './PromoteInstanceModal';
import { ThemeProvider } from '@material-ui/core';
import { mainTheme } from '../../../redesign/theme/mainTheme';

jest.mock('../../../redesign/helpers/api');

const fakeConfigId = 'aaa-111';
const fakeInstanceId = 'bbb-222';
const fakeBackupsList = [
  'backup_21-03-24-21-30.tgz',
  'backup_21-03-24-21-29.tgz',
  'backup_21-03-24-21-27.tgz'
];

const setup = () => {
  const onClose = jest.fn();
  const component = render(
    <ThemeProvider theme={mainTheme}>
      <PromoteInstanceModal
        visible
        onClose={onClose}
        configId={fakeConfigId}
        instanceId={fakeInstanceId}
      />
    </ThemeProvider>
  );

  return { component, onClose };
};

describe('HA promote instance modal', () => {
  it('should render and show loading indicator initially', () => {
    const { component } = setup();
    expect(component.getByTestId('ha-make-active-modal')).toBeInTheDocument();
    expect(component.getByText(/loading/i)).toBeInTheDocument();
  });

  it('should trigger onClose by Cancel button click', () => {
    const { component, onClose } = setup();
    userEvent.click(component.getByRole('button', { name: /cancel/i }));
    expect(onClose).toBeCalled();
  });

  it('should load backups and pre-select first item', async () => {
    (api.getHABackups as jest.Mock).mockResolvedValue(fakeBackupsList);
    setup();
    // for some reason hidden input could be accessed via document.querySelector() only
    expect(document.querySelector('input[name="backupFile"]')).toHaveValue('');
    await waitFor(() => expect(api.getHABackups).toBeCalledWith(fakeConfigId));
    expect(document.querySelector('input[name="backupFile"]')).toHaveValue(fakeBackupsList[0]);
  });

  it('should show validation error when no backup selected', async () => {
    (api.getHABackups as jest.Mock).mockResolvedValue([]);
    const { component } = setup();
    userEvent.click(component.getByRole('button', { name: /continue/i }));
    expect(await component.findByText(/backup file is required/i)).toBeInTheDocument();
  });

  it('should make API call and close modal', async () => {
    // mock environment
    const browserHistoryPush = jest.fn();
    jest.spyOn(browserHistory, 'push').mockImplementation(browserHistoryPush);
    const promise = Promise;
    (api.promoteHAInstance as jest.Mock).mockReturnValue(promise);
    (api.getHABackups as jest.Mock).mockResolvedValue(fakeBackupsList);

    // render component and resolve backups list mocked api call
    const { component, onClose } = setup();
    await waitFor(() => api.getHABackups);

    // click continue button without clicking confirmation checkbox
    userEvent.click(component.getByRole('button', { name: /continue/i }));
    await waitFor(() => expect(api.promoteHAInstance).not.toBeCalled());
    expect(browserHistoryPush).not.toBeCalled();

    // click confirmation checkbox and then click continue button
    userEvent.click(component.getByTestId('confirmedCheckbox'));
    userEvent.click(component.getByRole('button', { name: /continue/i }));

    // make sure modal can't be closed while API response is pending
    userEvent.click(component.getByRole('button', { name: /cancel/i }));
    expect(onClose).not.toBeCalled();

    // resolve mocked api call
    await waitFor(() => {
      promise.resolve();
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
    const browserHistoryPush = jest.fn();
    jest.spyOn(browserHistory, 'push').mockImplementation(browserHistoryPush);
    const toastError = jest.fn();
    jest.spyOn(toast, 'error').mockImplementation(toastError);
    (api.promoteHAInstance as jest.Mock).mockRejectedValue(
      new Error('Could not find leader instance')
    );
    (api.getHABackups as jest.Mock).mockResolvedValue(fakeBackupsList);

    const { component } = setup();
    await waitFor(() => api.getHABackups);

    userEvent.click(component.getByTestId('confirmedCheckbox'));
    userEvent.click(component.getByRole('button', { name: /continue/i }));

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
