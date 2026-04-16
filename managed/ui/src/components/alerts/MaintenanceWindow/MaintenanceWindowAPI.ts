import axios from 'axios';
import moment from 'moment';
import { ROOT_URL } from '../../../config';
import { MaintenanceWindowSchema } from './IMainenanceWindow';

const DATE_FORMAT = 'YYYY-MM-DD HH:mm:ss';

/**
 * get customer id
 * @returns customerID
 */
const getCustomerId = (): string => {
  const customerId = localStorage.getItem('customerId');
  return customerId ?? '';
};

/**
 * formate date to YYYY-MM-DD hh:mm:ss
 * @param date date object to format
 * @returns string
 */
export const formatDateToUTC = (date: Date) => {
  return moment(date).utc().format(DATE_FORMAT);
};

/**
 * Convert string(utc) to local time
 * @param dateString utc time stamp to convert
 * @returns Date
 */
export const convertUTCStringToDate = (dateString: string) => {
  return moment.utc(dateString).local().toDate();
};

/**
 * Convert UTC string to moment
 * @param dateString dateString to convert
 * @returns moment
 */
export const convertUTCStringToMoment = (dateString: string) => {
  return moment(convertUTCStringToDate(dateString));
};

/**
 * format UTC string to local timezone
 * @param dateString date String to convert
 * @returns return date in 'YYYY-MM-DD HH:mm:ss' format
 */
export const formatUTCStringToLocal = (dateString: string) => {
  return moment(convertUTCStringToDate(dateString)).format(DATE_FORMAT);
};
/**
 * fetches list of maintenance windows
 * @returns Maintenance Windows List
 */
export const getMaintenanceWindowList = (): Promise<MaintenanceWindowSchema[]> => {
  const requestURL = `${ROOT_URL}/customers/${getCustomerId()}/maintenance_windows/list`;
  return axios.post<MaintenanceWindowSchema[]>(requestURL, {}).then((res) => res.data);
};

/**
 * Create maintenance Windows
 * @param name name for the maintenance window
 * @param startTime starting time of the maintenance windows
 * @param endTime end time of the maintenance windows
 * @returns api response
 */
export const createMaintenanceWindow = (
  payload: Pick<
    MaintenanceWindowSchema,
    | 'name'
    | 'startTime'
    | 'endTime'
    | 'description'
    | 'alertConfigurationFilter'
    | 'suppressHealthCheckNotificationsConfig'
  >
): Promise<MaintenanceWindowSchema> => {
  const customerUUID = getCustomerId();
  const requestURL = `${ROOT_URL}/customers/${customerUUID}/maintenance_windows`;
  return axios
    .post<MaintenanceWindowSchema>(requestURL, {
      ...payload,
      customerUUID
    })
    .then((res) => res.data);
};

/**
 * Delete the maintenance window
 * @param windowUUID uuid of the maintenance windows
 * @returns api response
 */
export const deleteMaintenanceWindow = (windowUUID: string) => {
  const requestURL = `${ROOT_URL}/customers/${getCustomerId()}/maintenance_windows/${windowUUID}`;
  return axios.delete(requestURL).then((res) => res.data);
};

/**
 * Updated the maintenance window
 * @param window maintenance window to edit
 * @returns api response
 */
export const updateMaintenanceWindow = (window: MaintenanceWindowSchema) => {
  const customerUUID = getCustomerId();
  const requestURL = `${ROOT_URL}/customers/${customerUUID}/maintenance_windows/${window.uuid}`;
  return axios.put(requestURL, { ...window, customerUUID }).then((res) => res.data);
};
