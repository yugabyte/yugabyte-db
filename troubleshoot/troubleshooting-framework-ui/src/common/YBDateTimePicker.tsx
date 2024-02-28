import { ChangeEvent } from 'react';
import { makeStyles } from '@material-ui/core/styles';
import TextField from '@material-ui/core/TextField';
import clsx from 'clsx';
import { YBTimeFormats, formatDatetime } from '../helpers/DateUtils';
import { isNonEmptyString } from '../helpers/ObjectUtils';

const useStyles = makeStyles((theme) => ({
  container: {
    display: 'flex',
    flexWrap: 'wrap'
  },
  textField: {
    marginLeft: theme.spacing(1),
    marginRight: theme.spacing(1),
    width: 200
  }
}));

interface YBDateTimePickerProps {
  defaultDateTimeValue?: string;
  dateTimeLabel: string;
  className?: any;
  errorMsg?: string;
  onChange: (selectedValue: ChangeEvent<HTMLInputElement>) => void;
  disabled?: boolean;
}

export const YBDateTimePicker = ({
  defaultDateTimeValue,
  dateTimeLabel,
  errorMsg,
  onChange,
  className,
  disabled = false
}: YBDateTimePickerProps) => {
  const classes = useStyles();
  const today = new Date();
  const maxDate = formatDatetime(today, YBTimeFormats.YB_DATE_TIME_TIMESTAMP);

  const oldDay = new Date(today);
  oldDay.setDate(oldDay.getDate() - 60);
  const minDate = formatDatetime(oldDay, YBTimeFormats.YB_DATE_TIME_TIMESTAMP);

  return (
    <form className={classes.container} noValidate>
      <TextField
        id="datetime-local"
        label={dateTimeLabel}
        type="datetime-local"
        defaultValue={defaultDateTimeValue}
        onChange={onChange}
        className={clsx(classes.textField, className)}
        InputLabelProps={{
          shrink: true
        }}
        disabled={disabled}
        inputProps={{
          min: minDate,
          max: maxDate
        }}
        error={isNonEmptyString(errorMsg!)}
        helperText={errorMsg}
      />
    </form>
  );
};
