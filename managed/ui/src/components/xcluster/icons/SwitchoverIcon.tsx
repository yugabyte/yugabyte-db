import { useTheme } from '@material-ui/core';
import { ReactComponent as SwitchoverIconSvg } from '../../../redesign/assets/switchover.svg';

interface SwitchoverIconProps {
  isDisabled?: boolean;
}

export const SwitchoverIcon = ({ isDisabled = false }: SwitchoverIconProps) => {
  const theme = useTheme();
  return (
    <SwitchoverIconSvg
      fill={isDisabled ? theme.palette.ybacolors.disabledIcon : theme.palette.ybacolors.ybIcon}
    />
  );
};
