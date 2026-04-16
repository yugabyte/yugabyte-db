import { FC } from 'react';

interface BadgeInstanceTypeProps {
  isActive: boolean;
}

export const BadgeInstanceType: FC<BadgeInstanceTypeProps> = ({ isActive }) =>
  isActive ? (
    <span className="badge badge-green">Active</span>
  ) : (
    <span className="badge badge-gray">Standby</span>
  );
