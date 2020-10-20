import _ from 'lodash';
import React, { FC } from 'react';
import './Button.scss';

interface CustomButtonProps {
  isCTA?: boolean;
  chevronLeft?: boolean;
  chevronRight?: boolean;
}

type ButtonProps = JSX.IntrinsicElements['button'] & CustomButtonProps;

export const Button: FC<ButtonProps> = ({
  isCTA,
  chevronLeft,
  chevronRight,
  children,
  ...props
}) => {
  const classes = ['yb-uikit-button', props.className];
  if (isCTA) classes.push('yb-uikit-button--cta');
  if (chevronLeft) classes.push('yb-uikit-button--chevron-left');
  if (chevronRight) classes.push('yb-uikit-button--chevron-right');

  const extendedProps = {
    ...props,
    className: _.compact(classes).join(' ')
  };

  return <button {...extendedProps}>{children}</button>;
};
