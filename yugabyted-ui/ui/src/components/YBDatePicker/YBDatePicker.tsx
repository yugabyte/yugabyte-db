import React, { FC, useState } from 'react';
import DateFnsUtils from '@date-io/date-fns';
import { KeyboardDatePicker, KeyboardDatePickerProps, MuiPickersUtilsProvider } from 'material-ui-pickers-v4';
import type { MaterialUiPickersDate } from 'material-ui-pickers-v4/typings/date';

import Calendar from '@app/assets/Calendar.svg';
import type { DateType } from '@date-io/type';
import { useUpdateEffect } from 'react-use';

interface YBDatePickerProps extends KeyboardDatePickerProps {
  label?: string;
  format?: string;
  minDate?: Date | null;
  maxDate?: Date | null;
  onChange: (date: MaterialUiPickersDate) => void;
}

export const YBDatePicker: FC<YBDatePickerProps> = ({
  label,
  format = 'yyyy-MM-dd',
  minDate,
  maxDate,
  onChange,
  value
}) => {
  const [selectedDate, handleDateChange] = useState<MaterialUiPickersDate>((value as unknown) as DateType);

  useUpdateEffect(() => {
    handleDateChange(value ? ((value as unknown) as DateType) : null);
  }, [value]);

  return (
    <MuiPickersUtilsProvider utils={DateFnsUtils}>
      <KeyboardDatePicker
        disableToolbar
        autoOk={true}
        variant="inline"
        minDate={minDate}
        maxDate={maxDate}
        format={format}
        label={label}
        date={new Date()}
        value={selectedDate ?? null}
        onChange={(date) => {
          onChange(date);
          handleDateChange(date);
        }}
        keyboardIcon={<Calendar />}
      />
    </MuiPickersUtilsProvider>
  );
};
