import React, { FC } from 'react';
import './Tooltip.scss';

// TODO: put tooltip element outside of parent component (use portal?) + use popper.js to properly position it
export const Tooltip: FC = ({ children }) => {
  return (
    <>
      <div className="yb-uikit-tooltip">
        <div className="yb-uikit-tooltip__handler" />
        <div className="yb-uikit-tooltip__content">{children}</div>
      </div>
    </>
  );
};
