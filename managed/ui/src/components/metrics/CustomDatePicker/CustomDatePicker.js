import { useSelector } from 'react-redux';
import { DateTimePicker } from 'react-widgets';
import moment from 'moment-timezone';
import { YBButtonLink } from '../../common/forms/fields';
import './CustomDatePicker.scss';

const TIMESTAMP_FORMAT = 'YYYY-MM-DD[T]HH:mm:ss';

export const CustomDatePicker = ({
  startMoment,
  endMoment,
  handleTimeframeChange,
  setStartMoment,
  setEndMoment
}) => {
  const currentUser = useSelector((state) => state.customer.currentUser);
  let newStartMoment = moment(startMoment).format(TIMESTAMP_FORMAT);
  let newEndMoment = moment(endMoment).format(TIMESTAMP_FORMAT);
  let maxDate = new Date();
  if (currentUser.data.timezone) {
    // Convert the local time moments to user preference timezone, and remove the timezone information
    //  so that when converting to a Date object, it doesn't convert back to local time
    newStartMoment = moment(startMoment).tz(currentUser.data.timezone).format(TIMESTAMP_FORMAT);
    newEndMoment = moment(endMoment).tz(currentUser.data.timezone).format(TIMESTAMP_FORMAT);

    // Set maximum date of the date pickers to current time in user preference timezone
    maxDate = new Date(moment().tz(currentUser.data.timezone).format(TIMESTAMP_FORMAT));
  }

  const handleStartDateTimeChange = (timestamp) => {
    let newMoment = timestamp;
    // Change the timezone without changing the timestamp
    if (currentUser.data.timezone) {
      newMoment =
        moment(timestamp).format(TIMESTAMP_FORMAT) +
        moment.tz(currentUser.data.timezone).format('ZZ');
    }
    setStartMoment(newMoment);
  };

  const handleEndDateTimeChange = (timestamp) => {
    let newMoment = timestamp;
    // Change the timezone without changing the timestamp
    if (currentUser.data.timezone) {
      newMoment =
        moment(timestamp).format(TIMESTAMP_FORMAT) +
        moment.tz(currentUser.data.timezone).format('ZZ');
    }
    setEndMoment(newMoment);
  };

  return (
    <span className="graph-filter-custom">
      <DateTimePicker
        placeholder="MMM dd, yyyy, hh:mm a"
        defaultValue={new Date(newStartMoment)}
        onChange={handleStartDateTimeChange}
        max={maxDate}
      />
      &ndash;
      <DateTimePicker
        placeholder="MMM dd, yyyy, hh:mm a"
        defaultValue={new Date(newEndMoment)}
        onChange={handleEndDateTimeChange}
        max={maxDate}
        min={new Date(newStartMoment)}
      />
      <YBButtonLink
        btnClass={'btn btn-default custom-filter-btn'}
        btnIcon={'fa fa-caret-right'}
        onClick={handleTimeframeChange}
      />
    </span>
  );
};
