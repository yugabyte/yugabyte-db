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

const defaultErrorExtractor = (
  error: AxiosError<YBPStructuredError | YBPError>,
  customErrorLabel?: string
) => {
  const errorLabel = customErrorLabel ?? 'A request encountered an error';
  if (isYBPError(error)) {
    const errorMessageDetails = error.response?.data.error;
    return `${errorLabel}${errorMessageDetails ? `: ${errorMessageDetails}` : '.'}`;
  }
  const errorMessageDetails =
    (error as AxiosError<YBPStructuredError>).response?.data.error?.['message'] ?? error.message;
  return `${errorLabel}${errorMessageDetails ? `: ${errorMessageDetails}` : '.'}`;
};

export const handleServerError = (
  error: Error | AxiosError<YBPStructuredError | YBPError>,
  options?: {
    customErrorExtractor?: (error: AxiosError<YBPStructuredError | YBPError>) => string;
    customErrorLabel?: string;
  }
) => {
  if (isAxiosError<YBPStructuredError | YBPError>(error)) {
    toast.error(
      options?.customErrorExtractor
        ? options?.customErrorExtractor(error)
        : defaultErrorExtractor(error, options?.customErrorLabel)
    );
  } else {
    toast.error(error.message);
  }
};
