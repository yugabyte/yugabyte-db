import axios, { AxiosError } from 'axios';
import { toast } from 'react-toastify';

export const assertUnreachableCase = (value: never) => {
  throw new Error(`Encountered unhandled value: ${value}`);
};

export const isAxiosError = <TError>(error: unknown): error is AxiosError<TError> =>
  axios.isAxiosError(error);

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
