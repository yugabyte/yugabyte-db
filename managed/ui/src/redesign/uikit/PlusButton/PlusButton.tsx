import React, { FC, HTMLAttributes } from 'react';
import { I18n } from '../I18n/I18n';
import './PlusButton.scss';

interface PlusButtonProps extends HTMLAttributes<HTMLElement> {
  disabled?: boolean;
  text: string;
}

export const PlusButton: FC<PlusButtonProps> = ({ disabled, text, className, onClick }) => {
  return (
    <div
      onClick={onClick}
      className={`
        yb-uikit-plus-button
        ${disabled ? 'yb-uikit-plus-button--disabled' : ''}
        ${className}
      `}
    >
      <I18n>{text}</I18n>
    </div>
  );
};
