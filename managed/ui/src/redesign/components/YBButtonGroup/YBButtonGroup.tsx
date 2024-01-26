import { FC } from 'react';
import { ButtonGroup, makeStyles } from '@material-ui/core';
import { YBButton } from '../YBButton/YBButton';
import { themeVariables } from '../../theme/variables';
import clsx from 'clsx';

export interface YBButtonGroupProps {
  variant?: 'outlined' | 'text' | 'contained';
  color?: 'default' | 'secondary' | 'primary';
  values: number[];
  selectedNum: number;
  disabled?: boolean;
  dataTestId?: string;
  btnClassName?: any;
  btnGroupClassName?: any;
  handleSelect: (selectedNum: number) => void;
}

const useStyles = makeStyles(() => ({
  button: {
    height: themeVariables.inputHeight,
    borderWidth: '0.5px !important',
    border: '1px solid #DEDEE0'
  },
  btnGroup: {
    boxShadow: 'none'
  }
}));

export const YBButtonGroup: FC<YBButtonGroupProps> = (props) => {
  const {
    variant,
    color,
    values,
    selectedNum,
    disabled,
    dataTestId,
    btnClassName,
    btnGroupClassName,
    handleSelect
  } = props;
  const classes = useStyles();

  return (
    <ButtonGroup
      data-testid={dataTestId ?? 'YBButtonGroup'}
      variant={variant ?? 'outlined'}
      color={color ?? 'default'}
      className={clsx(btnGroupClassName, classes.btnGroup)}
    >
      {values.map((value) => {
        return (
          <YBButton
            key={value}
            className={btnClassName ?? classes.button}
            data-testid={`${dataTestId}-option${value}`}
            disabled={value !== selectedNum && !!disabled}
            variant={value === selectedNum ? 'primary' : 'secondary'}
            onClick={(e: any) => {
              if (!!disabled) e.preventDefault();
              else handleSelect(value);
            }}
          >
            {value}
          </YBButton>
        );
      })}
    </ButtonGroup>
  );
};
