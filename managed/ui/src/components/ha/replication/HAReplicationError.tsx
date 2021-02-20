import React, { FC, useEffect } from 'react';
import './HAReplicationError.scss';

interface HAReplicationErrorProps {
  error: unknown;
}

export const HAReplicationError: FC<HAReplicationErrorProps> = ({ error }) => {
  useEffect(() => {
    console.error(error);
  }, [error]);

  return (
    <div className="ha-replication-error">
      <div>
        <i className="fa fa-exclamation-circle ha-replication-error__icon" />
      </div>
      <div>
        Something went wrong. Check the browser console for more details.
      </div>
    </div>
  );
};
