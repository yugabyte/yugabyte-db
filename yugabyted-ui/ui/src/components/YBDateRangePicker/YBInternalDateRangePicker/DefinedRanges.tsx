import React from 'react';
import { List, ListItem, ListItemText } from '@material-ui/core';
import { isSameDay } from 'date-fns';
import type { DefinedRange, DateRange } from '@app/components/YBDateRangePicker/YBInternalDateRangePickerUtill/types';

type DefinedRangesProps = {
  setRange: (range: DateRange) => void;
  selectedRange: DateRange;
  ranges: DefinedRange[];
};

const isSameRange = (first: DateRange, second: DateRange) => {
  const { startDate: fStart, endDate: fEnd } = first;
  const { startDate: sStart, endDate: sEnd } = second;
  if (fStart && sStart && fEnd && sEnd) {
    return isSameDay(fStart, sStart) && isSameDay(fEnd, sEnd);
  }
  return false;
};

export const DefinedRanges: React.FunctionComponent<DefinedRangesProps> = ({
  ranges,
  setRange,
  selectedRange
}: DefinedRangesProps) => (
  <List>
    {ranges.map((range, idx) => (
      // eslint-disable-next-line react/no-array-index-key
      <ListItem button key={idx} onClick={() => setRange(range)}>
        <ListItemText
          primaryTypographyProps={{
            variant: 'body2',
            style: {
              fontWeight: isSameRange(range, selectedRange) ? 'bold' : 'normal'
            }
          }}
        >
          {range.label}
        </ListItemText>
      </ListItem>
    ))}
  </List>
);
