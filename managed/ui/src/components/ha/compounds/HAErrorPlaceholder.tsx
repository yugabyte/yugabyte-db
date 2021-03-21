import React, { FC, useEffect } from 'react';
import './HAErrorPlaceholder.scss';

interface HAErrorPlaceholderProps {
  error: unknown;
}

export const HAErrorPlaceholder: FC<HAErrorPlaceholderProps> = ({ error }) => {
  useEffect(() => {
    console.error(error);
  }, [error]);

  return (
    <div className="ha-error-placeholder" data-testid="ha-generic-error">
      <div>
        <i className="fa fa-exclamation-circle ha-error-placeholder__icon" />
      </div>
      <div>
        Something went wrong. Check the browser console for more details.
      </div>
    </div>
  );
};
