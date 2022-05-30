import React, {useState} from 'react';
import { DatePicker } from 'react-widgets';

import './DateRangePicker.scss';


export const CustomDateRangePicker = ({ onRangeChange }) => {
  const [localStartDate, setLocalStartDate] = useState(new Date());

  const handleStartDateTimeChange = (timestamp) => {
    setLocalStartDate(timestamp);
  };

  const handleEndDateTimeChange = (timestamp) => {
    onRangeChange({ start:localStartDate, end: timestamp});
  };

  return (
    <span className="support-bundle-custom-date-range">
      <DatePicker
        placeholder="yyyy-MM-DD"
        defaultValue={new Date()}
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
