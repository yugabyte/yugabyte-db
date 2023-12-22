import { isEqual } from 'lodash';
import { ButtonGroup, makeStyles } from '@material-ui/core';
import { YBButton } from '../YBButton/YBButton';
import { themeVariables } from '../../theme/variables';
import clsx from 'clsx';

export interface YBButtonGroupProps<T> {
  variant?: 'outlined' | 'text' | 'contained';
  color?: 'default' | 'secondary' | 'primary';
  values: T[];
  selectedNum: T;
  disabled?: boolean;
  dataTestId?: string;
  btnClassName?: any;
  btnGroupClassName?: any;
  handleSelect: (selectedNum: T) => void;
  displayLabelFn?: (elem: T) => JSX.Element;
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

export const YBButtonGroup = <T,>(props: YBButtonGroupProps<T>) => {
  const {
    variant,
    color,
    values,
    selectedNum,
    disabled,
    dataTestId,
    btnClassName,
    btnGroupClassName,
    handleSelect,
    displayLabelFn
  } = props;
  const classes = useStyles();

  return (
    <ButtonGroup
      data-testid={dataTestId ?? 'YBButtonGroup'}
      variant={variant ?? 'outlined'}
      color={color ?? 'default'}
      className={clsx(btnGroupClassName, classes.btnGroup)}
    >
      {values.map((value, i) => {
        return (
          <YBButton
            key={i}
            className={btnClassName ?? classes.button}
            data-testid={`${dataTestId}-option${value}`}
            disabled={!isEqual(value, selectedNum) && !!disabled}
            variant={isEqual(value, selectedNum) ? 'primary' : 'secondary'}
            onClick={(e: any) => {
              if (!!disabled) e.preventDefault();
              else handleSelect(value);
            }}
          >
            {displayLabelFn ? displayLabelFn(value) : value}
          </YBButton>
        );
      })}
    </ButtonGroup>
  );
};
