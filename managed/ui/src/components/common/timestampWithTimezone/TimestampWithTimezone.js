import { useSelector } from 'react-redux';
import moment from 'moment';

export const TimestampWithTimezone = (props) => {
  const currentUser = useSelector((state) => state.customer.currentUser);
  const { timestamp, timeFormat } = props;
  if (currentUser.data.timezone) {
    return moment(timestamp).tz(currentUser.data.timezone).format(timeFormat);
  } else {
    return moment(timestamp).format(timeFormat);
  }
};
