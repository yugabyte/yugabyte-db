import { FC, ReactNode } from 'react';
import clsx from 'clsx';
import {
  makeStyles,
  FormControlLabel,
  RadioGroup,
  Radio,
  RadioProps,
  RadioGroupProps,
  FormLabel,
  InputProps,
  createStyles
} from '@material-ui/core';

const useStyles = makeStyles((theme) => {
  return createStyles({
    root: {
      '&:hover': {
        backgroundColor: 'transparent'
      },
      padding: theme.spacing(0.5)
    },
    icon: {
      borderRadius: '50%',
      width: theme.spacing(2),
      height: theme.spacing(2),
      boxShadow: `inset 0 0 0 2px ${theme.palette.grey[400]}, inset 0 -1px 0 ${theme.palette.grey[400]}`,
      backgroundColor: theme.palette.grey[200],
      backgroundImage: 'linear-gradient(180deg,hsla(0,0%,100%,.8),hsla(0,0%,100%,0))'
    },
    checkedIcon: {
      backgroundColor: theme.palette.primary.main,
      backgroundImage: 'linear-gradient(180deg,hsla(0,0%,100%,.1),hsla(0,0%,100%,0))',
      boxShadow: 'none',
      '&:before': {
        display: 'block',
        width: theme.spacing(2),
        height: theme.spacing(2),
        backgroundImage: `radial-gradient(${theme.palette.common.white},${theme.palette.common.white} 28%,transparent 32%)`,
        content: '""'
      },
      'input:hover ~ &': {
        backgroundColor: theme.palette.primary.main
      }
    },
    formGroupRow: {
      flexDirection: 'row',
      margin: 0
    },
    mainLabel: {
      ...theme.typography.subtitle2,
      fontWeight: theme.typography.fontWeightMedium as number,
      fontSize: theme.typography.subtitle1.fontSize,
      textTransform: 'uppercase',
      marginBottom: theme.spacing(0.5),
      marginTop: theme.spacing(0.5)
    },
    label: {
      ...theme.typography.body2,
      marginLeft: theme.spacing(0.5)
    }
  });
});

interface YBRadioProps extends RadioProps {
  inputProps?: InputProps['inputProps']; // override type to make it accept custom attributes like "data-testid"
}

export const YBRadio: FC<YBRadioProps> = (props) => {
  const classes = useStyles();

  return (
    <Radio
      {...props}
      className={clsx(classes.root, props.className)}
      color="default"
      checkedIcon={<span className={clsx(classes.icon, classes.checkedIcon)} />}
      icon={<span className={classes.icon} />}
    />
  );
};

export interface OptionProps {
  value: string;
  label: ReactNode;
  disabled?: boolean;
  'data-testid'?: string;
}

export const RadioGroupOrientation = {
  VERTICAL: 'vertical',
  HORIZONTAL: 'horizontal'
} as const;
export type RadioGroupOrientation = typeof RadioGroupOrientation[keyof typeof RadioGroupOrientation];

export interface YBRadioGroupProps extends RadioGroupProps {
  label?: ReactNode;
  options: OptionProps[];
  orientation?: RadioGroupOrientation;
}

export const YBRadioGroup: FC<YBRadioGroupProps> = ({
  label,
  options = [],
  orientation = RadioGroupOrientation.VERTICAL,
  className,
  value,
  ...muiRadioGroupProps
}) => {
  const classes = useStyles();

  return (
    <>
      {label && <FormLabel className={classes.mainLabel}>{label}</FormLabel>}
      <RadioGroup
        className={clsx(
          orientation === RadioGroupOrientation.HORIZONTAL && classes.formGroupRow,
          className
        )}
        aria-label="radio-group"
        {...muiRadioGroupProps}
      >
        {options.map((option) => (
          <FormControlLabel
            key={`form-radio-option-${option.value}`}
            value={option.value}
            control={
              <YBRadio
                inputProps={{ 'data-testid': option['data-testid'] ?? `YBRadio-${option.value}` }}
                checked={option.value === value}
              />
            }
            label={option.label}
            disabled={option.disabled}
            classes={{ label: classes.label }}
          />
        ))}
      </RadioGroup>
    </>
  );
};
