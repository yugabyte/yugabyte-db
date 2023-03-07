import axios, { AxiosError } from 'axios';
import { toast } from 'react-toastify';

export const assertUnreachableCase = (value: never) => {
  throw new Error(`Encountered unhandled value: ${value}`);
};

export const handleServerError = (error: Error | AxiosError) => {
  if (axios.isAxiosError(error)) {
    toast.error(error.response?.data?.error?.message ?? error.message);
  } else {
    toast.error(error.message);
  }
};
