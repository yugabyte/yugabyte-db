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
  shouldDisableButtonFn?: (elem: T) => boolean;
}

const useStyles = makeStyles((theme) => ({
  button: {
    height: themeVariables.inputHeight,
    borderWidth: '0.5px !important',
    border: '1px solid #DEDEE0'
  },
  btnGroup: {
    boxShadow: 'none'
  },
  overrideMuiButtonGroup: {
    '& .MuiButton-containedSecondary': {
      backgroundColor: 'rgba(43, 89, 195, 0.1)',
      color: theme.palette.primary[600],
      border: `1px solid ${theme.palette.ybacolors.ybBorderGray}`
    },
    '& .MuiButton-outlinedSecondary': {
      color: theme.palette.ybacolors.labelBackground,
      border: `1px solid ${theme.palette.ybacolors.ybBorderGray}`
    }
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
    displayLabelFn,
    shouldDisableButtonFn
  } = props;
  const classes = useStyles();

  return (
    <ButtonGroup
      data-testid={dataTestId ?? 'YBButtonGroup'}
      variant={variant ?? 'outlined'}
      color={color ?? 'default'}
      className={clsx(
        btnGroupClassName,
        classes.btnGroup,
        color === 'secondary' && classes.overrideMuiButtonGroup
      )}
      disabled={disabled}
    >
      {values.map((value, i) => {
        return (
          <YBButton
            key={i}
            className={btnClassName ?? classes.button}
            data-testid={`${dataTestId}-option${value}`}
            disabled={
              (shouldDisableButtonFn && shouldDisableButtonFn(value))
            }
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
