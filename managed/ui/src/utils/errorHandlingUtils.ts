import axios, { AxiosError } from 'axios';
import { toast } from 'react-toastify';
import { YBPBeanValidationError, YBPError, YBPStructuredError } from '../redesign/helpers/dtos';

export const assertUnreachableCase = (value: never) => {
  throw new Error(`Encountered unhandled value: ${value}`);
};

export const isAxiosError = <TError>(error: unknown): error is AxiosError<TError> =>
  axios.isAxiosError(error);

export const isYBPError = (
  error: AxiosError<YBPError | YBPStructuredError>
): error is AxiosError<YBPError> =>
  typeof error.response?.data.error === 'string' || error.response?.data.error instanceof String;

export const isYBPBeanValidationError = (
  error: AxiosError<YBPError | YBPStructuredError>
): error is AxiosError<YBPBeanValidationError> =>
  !isYBPError(error) &&
  error.response?.status === 400 &&
  (error as AxiosError<YBPStructuredError>).response?.data.error['errorSource'].includes(
    'providerValidation'
  );

export const handleServerError = <TError>(
  error: Error | AxiosError,
  getErrorMessage?: (error: AxiosError<TError>) => string
) => {
  if (isAxiosError<TError>(error)) {
    toast.error(getErrorMessage ? getErrorMessage(error) : error.message);
  } else {
    toast.error(error.message);
  }
};
