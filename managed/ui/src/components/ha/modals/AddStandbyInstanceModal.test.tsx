import React from 'react';
import userEvent from '@testing-library/user-event';
import { toast } from 'react-toastify';
import { render, waitFor } from '../../../test-utils';
import { AddStandbyInstanceModal } from './AddStandbyInstanceModal';
import { api } from '../../../redesign/helpers/api';

jest.mock('../../../redesign/helpers/api');

const fakeConfigId = 'aaa-111';

const setup = () => {
  const onClose = jest.fn();
  const component = render(
    <AddStandbyInstanceModal
      visible
      onClose={onClose}
      configId={fakeConfigId}
    />
  );
  return { component, onClose };
};

describe('HA add standby instance modal', () => {
  it('should render', () => {
    const { component } = setup();
    expect(component.getByTestId('ha-add-standby-instance-modal')).toBeInTheDocument();
  });

  it('should trigger onClose by Cancel button click', () => {
    const { component, onClose } = setup();
    userEvent.click(component.getByRole('button', { name: /cancel/i }));
    expect(onClose).toBeCalled();
  });

  it('should validate instance address input', async () => {
    const { component, onClose } = setup();

    userEvent.click(component.getByRole('button', { name: /continue/i }));
    expect(await component.findByText(/required field/i)).toBeInTheDocument();
    expect(onClose).not.toBeCalled();

    userEvent.type(component.getByRole('textbox'), 'lorem ipsum');
    expect(await component.findByText(/should be a valid url/i)).toBeInTheDocument();
    expect(onClose).not.toBeCalled();
  });

  it('should make an API call and close modal', async () => {
    const promise = Promise;
    (api.createHAInstance as jest.Mock).mockReturnValue(promise);
    const fakeAddress = 'http://valid.url';

    const { component, onClose } = setup();

    userEvent.type(component.getByRole('textbox'), fakeAddress);
    userEvent.click(component.getByRole('button', { name: /continue/i }));

    // make sure modal can't be closed while API response is pending
    userEvent.click(component.getByRole('button', { name: /cancel/i }));
    expect(onClose).not.toBeCalled();

    await waitFor(() => {
      promise.resolve();
      expect(api.createHAInstance).toBeCalledWith(fakeConfigId, fakeAddress, false, false);
      expect(onClose).toBeCalled();
    });
  });

  it('should show an error toast on API failure', async () => {
    const toastError = jest.fn();
    jest.spyOn(toast, 'error').mockImplementation(toastError);
    const consoleError = jest.fn();
    jest.spyOn(console, 'error').mockImplementation(consoleError);
    (api.createHAInstance as jest.Mock).mockRejectedValue({});
    const fakeAddress = 'http://valid.url';

    const { component, onClose } = setup();

    userEvent.type(component.getByRole('textbox'), fakeAddress);
    userEvent.click(component.getByRole('button', { name: /continue/i }));

    await waitFor(() => {
      expect(api.createHAInstance).toBeCalledWith(fakeConfigId, fakeAddress, false, false);
      expect(toastError).toBeCalled();
      expect(consoleError).toBeCalled();
      expect(onClose).toBeCalled();
    });
  });
});
