import React, { MouseEvent, ReactElement, useState } from 'react';
import { format as dateFnSFormat } from 'date-fns';
import { makeStyles, IconButton, Modal, Fade, Backdrop, Box, Theme } from '@material-ui/core';
import { useController } from 'react-hook-form';
import { YBInput, YBInputFieldProps } from '@app/components';
import { DateRangePicker } from '@app/components/YBDateRangePicker/YBInternalDateRangePickerUtill';
import CalenderEmpty from '@app/assets/CalenderEmpty.svg';
import Clear from '@app/assets/Clear.svg';
import { useTranslation } from 'react-i18next';

export type MuiDateRangePickerProps<T> = YBInputFieldProps<T> & {
  clearable?: boolean;
  sameDateSelectable?: boolean;
  defaultDateRange: DateRange;
  onChangePartial?: (dateRange: DateRange) => void;
  format?: string;
  minDate?: Date | string;
  maxDate?: Date | string;
  name: string;
};

export type DateRange = {
  startDate?: Date;
  endDate?: Date;
};

const useStyles = makeStyles((theme: Theme) => ({
  modal: {
    display: 'flex',
    alignItems: 'center',
    justifyContent: 'center'
  },
  pointer: {
    width: theme.spacing(4),
    cursor: 'pointer',
    position: 'relative',
    right: theme.spacing(0.5),

    '& .MuiInputBase-input': {
      cursor: 'pointer'
    }
  }
}));

export const YBDateRangePicker = <T,>(props: MuiDateRangePickerProps<T>): ReactElement => {
  const classes = useStyles();
  const {
    clearable,
    format,
    defaultDateRange,
    minDate,
    maxDate,
    name,
    rules,
    defaultValue,
    control,
    shouldUnregister,
    sameDateSelectable,
    onChangePartial,
    ...ybInputFieldProps
  } = props;
  const [dateRange, setDateRange] = useState<DateRange>({ ...defaultDateRange });

  const {
    field: { ref, ...fieldProps },
    fieldState
  } = useController({ name, rules, defaultValue, control, shouldUnregister });

  const { t } = useTranslation();

  const [open, setOpen] = useState(false);

  const toggle = () => setOpen(!open);

  const getDisplayDateRange = (): string => {
    const currentDateRange = dateRange;
    const startDate = currentDateRange?.startDate ? dateFnSFormat(currentDateRange.startDate, format ?? '') : undefined;
    const endDate = currentDateRange?.endDate ? dateFnSFormat(currentDateRange.endDate, format ?? '') : undefined;
    return startDate && endDate
      ? `${startDate} - ${endDate}`
      : `${t('common.dateRangePicker.startDate')} - ${t('common.dateRangePicker.endDate')}`;
  };

  const clearSelection = (event: MouseEvent) => {
    event.stopPropagation();
    const emptyRange = {};
    setDateRange(emptyRange);
    fieldProps.onChange(emptyRange);
    if (onChangePartial) {
      onChangePartial(emptyRange);
    }
  };

  return (
    <>
      <YBInput
        {...ybInputFieldProps}
        name={name}
        value={getDisplayDateRange()}
        className={classes.pointer}
        onClick={toggle}
        InputProps={{
          readOnly: true,
          startAdornment: (
            <IconButton>
              <CalenderEmpty />
            </IconButton>
          ),
          endAdornment: clearable && dateRange?.endDate && dateRange?.startDate && (
            <Clear onClick={clearSelection} className={classes.pointer} />
          )
        }}
        inputRef={ref}
        error={!!fieldState.error}
        helperText={fieldState.error?.message ?? ybInputFieldProps.helperText}
      />
      <Modal
        className={classes.modal}
        open={open}
        closeAfterTransition
        BackdropComponent={Backdrop}
        BackdropProps={{
          timeout: 500
        }}
      >
        <Fade in={open}>
          <Box>
            <DateRangePicker
              sameDateSelectable={sameDateSelectable}
              format={format}
              dateRange={dateRange}
              setDateRange={setDateRange}
              showDefinedRange={false}
              minDate={minDate}
              maxDate={maxDate}
              onChangePartial={onChangePartial}
              open={true}
              toggle={toggle}
              onChange={(range) => {
                fieldProps.onChange(range);
                toggle();
              }}
            />
          </Box>
        </Fade>
      </Modal>
    </>
  );
};

YBDateRangePicker.defaultProps = {
  format: 'yyyy-MM-dd'
};
