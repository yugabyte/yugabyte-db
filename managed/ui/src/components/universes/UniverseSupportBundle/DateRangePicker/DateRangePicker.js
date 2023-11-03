import { useState } from 'react';
import { DatePicker } from 'react-widgets';

import './DateRangePicker.scss';

export const CustomDateRangePicker = ({ onRangeChange }) => {
  const yesterday = new Date();
  yesterday.setDate(new Date().getDate() - 1);
  const [localStartDate, setLocalStartDate] = useState(yesterday);
  const [localEndDate, setLocalEndDate] = useState(new Date());

  const handleStartDateTimeChange = (timestamp) => {
    setLocalStartDate(timestamp);
    onRangeChange({ start: localEndDate, end: timestamp });
  };

  const handleEndDateTimeChange = (timestamp) => {
    setLocalEndDate(timestamp);
    onRangeChange({ start: localStartDate, end: timestamp });
  };

  return (
    <span className="support-bundle-custom-date-range">
      <DatePicker
        placeholder="yyyy-MM-DD"
        defaultValue={yesterday}
        onChange={handleStartDateTimeChange}
        max={new Date()}
      />
      &ndash;
      <DatePicker
        placeholder="yyyy-MM-DD"
        defaultValue={new Date()}
        onChange={handleEndDateTimeChange}
        max={new Date()}
        min={localStartDate}
      />
    </span>
  );
};
