import { useState } from 'react';
import { DateTimePicker } from 'react-widgets';

import './DateRangePicker.scss';

export const DATE_FORMAT = 'YYYY-MM-DD HH:mm:ss';

export const CustomDateRangePicker = ({ onRangeChange }) => {
  const yesterday = new Date();
  yesterday.setDate(new Date().getDate() - 1);
  const [localStartDate, setLocalStartDate] = useState(yesterday);
  const [localEndDate, setLocalEndDate] = useState(new Date());

  const handleStartDateTimeChange = (timestamp) => {
    setLocalStartDate(timestamp);
    onRangeChange({ start: timestamp, end: localEndDate });
  };

  const handleEndDateTimeChange = (timestamp) => {
    setLocalEndDate(timestamp);
    onRangeChange({ start: localStartDate, end: timestamp });
  };

  return (
    <span className="support-bundle-custom-date-range">
      <DateTimePicker
        placeholder="Pick a start time"
        step={10}
        defaultValue={yesterday}
        formats={DATE_FORMAT}
        onChange={handleStartDateTimeChange}
        max={new Date()}
      />
      &ndash;
      <DateTimePicker
        placeholder="Pick an end time"
        defaultValue={new Date()}
        onChange={handleEndDateTimeChange}
        max={new Date()}
        formats={DATE_FORMAT}
        min={localStartDate}
      />
    </span>
  );
};
