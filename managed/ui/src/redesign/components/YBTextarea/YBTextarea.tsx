import { FC } from 'react';
import { createStyles, FormControl, makeStyles, Theme, TextField } from '@material-ui/core';
import clsx from 'clsx';

const useStyles = makeStyles((theme: Theme) => {
  return createStyles({
    root: {
      borderRadius: '7px',
      minHeight: '42px',
      borderColor: '#dedee0',
      fontSize: '14px'
    },
    overrideMuiHelperText: {
      '& .MuiFormHelperText-root': {
        color: 'orange'
      }
    },
    margin: {
      margin: theme.spacing(1)
    }
  });
});

interface TextareaProps {
  id?: string;
  minRows?: number;
  maxRows: number;
  disabled?: boolean;
  onChange?: any;
  value?: string;
  error?: boolean;
  message?: string;
  readOnly?: boolean;
  isWarning?: boolean;
}

export const YBTextarea: FC<TextareaProps> = ({
  id,
  minRows,
  maxRows,
  value,
  onChange,
  disabled,
  readOnly,
  error,
  message,
  isWarning
}: TextareaProps) => {
  const classes = useStyles();
  const isError = error ?? false;
  const errorMessage = message ?? '';

  return (
    <FormControl fullWidth className={classes.margin}>
      <TextField
        id={id}
        minRows={minRows ?? 1}
        maxRows={maxRows}
        multiline
        onChange={onChange}
        disabled={disabled}
        value={value}
        className={clsx(classes.root, {
          [classes.overrideMuiHelperText]: !!isWarning
        })}
        error={isError}
        aria-readonly={readOnly}
        helperText={errorMessage}
      />
    </FormControl>
  );
};
