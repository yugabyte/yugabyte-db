import { useTheme } from '@material-ui/core';
import { ReactComponent as FailoverIconSvg } from '../../../redesign/assets/failover.svg';

interface FailoverIconProps {
  isDisabled?: boolean;
}

export const FailoverIcon = ({ isDisabled = false }: FailoverIconProps) => {
  const theme = useTheme();
  return (
    <FailoverIconSvg
      fill={isDisabled ? theme.palette.ybacolors.disabledIcon : theme.palette.ybacolors.ybIcon}
    />
  );
};
