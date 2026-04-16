import React from 'react';
import { StylesProvider } from '@material-ui/core/styles';
import { DateRangePickerWrapper, DateRangePickerWrapperProps } from './DateRangePickerWrapper';
import { generateClassName } from '@app/components/YBDateRangePicker/YBInternalDateRangePickerUtill/generateClassName';

export const DateRangePickerExporter: React.FunctionComponent<DateRangePickerWrapperProps> = (
  props: DateRangePickerWrapperProps
) => (
  <StylesProvider generateClassName={generateClassName}>
    <DateRangePickerWrapper {...props} />
  </StylesProvider>
);
