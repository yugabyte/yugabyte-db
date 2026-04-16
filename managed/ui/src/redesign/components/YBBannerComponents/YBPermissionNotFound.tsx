import { Box } from '@material-ui/core';
import { ReactComponent as LockIcon } from '../../assets/lock.svg';

export const YBPermissionNotFound = () => {
  return (
    <Box
      display="flex"
      flexDirection="column"
      alignItems="center"
      justifyContent="center"
      height="100%"
      width="100%"
      data-testid="YBPermissionNotFound-Container"
    >
      <LockIcon />
      You don&apos;t have permission to view this page
    </Box>
  );
};
