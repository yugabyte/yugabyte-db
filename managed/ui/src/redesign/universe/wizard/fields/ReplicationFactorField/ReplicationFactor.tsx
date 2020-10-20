import React, { FC } from 'react';
import './ReplicationFactor.scss';

interface ReplicationFactorProps {
  options: number[];
  value: number;
  onChange(value: number): void;
  disabled?: boolean;
}

export const ReplicationFactor: FC<ReplicationFactorProps> = ({
  options,
  value,
  onChange,
  disabled
}) => {
  const onClick = (newValue: number) => {
    if (newValue !== value) onChange(newValue);
  };

  return (
    <div className={`replication-factor ${disabled ? 'replication-factor--disabled' : ''}`}>
      {options.map((option) => (
        <div
          key={option}
          className={`replication-factor__item ${
            value === option ? 'replication-factor__item--selected' : ''
          }`}
          onClick={() => onClick(option)}
        >
          {option}
        </div>
      ))}
    </div>
  );
};
