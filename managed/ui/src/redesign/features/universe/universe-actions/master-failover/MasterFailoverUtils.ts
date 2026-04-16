export const getDiffUTCDuration = (scheduledDate: string) => {
  // Assuming current time is also in UTC
  const targetTime = new Date(scheduledDate).valueOf();
  const currentTime = new Date().valueOf(); // This gets the current time in UTC

  // Calculate the difference in milliseconds
  const differenceInMilliseconds = targetTime - currentTime;

  // Convert milliseconds to a more readable format (e.g., days, hours, minutes, seconds)
  const differenceInSeconds = Math.floor(differenceInMilliseconds / 1000);
  const differenceInMinutes = Math.floor(differenceInSeconds / 60);
  const differenceInHours = Math.floor(differenceInMinutes / 60);
  const differenceInDays = Math.floor(differenceInHours / 24);

  const days = differenceInDays > 0 ? `${differenceInDays} days ` : '';
  const hours = differenceInHours % 24 > 0 ? `${differenceInHours % 24} hours ` : '';
  const minutes = differenceInMinutes % 60 > 0 ? `${differenceInMinutes % 60} minutes ` : '';
  const seconds = differenceInSeconds % 60 > 0 ? `${differenceInSeconds % 60} seconds` : '';

  return `${days}${hours}${minutes}${seconds}`;
};

export enum FailoverJobType {
  SYNC_MASTER_ADDRS = 'SYNC_MASTER_ADDRS',
  MASTER_FAILOVER = 'MASTER_FAILOVER'
}
