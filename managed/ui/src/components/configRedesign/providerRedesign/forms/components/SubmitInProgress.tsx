import { FC } from 'react';
import { Box, Typography, CircularProgress } from '@material-ui/core';

interface SubmitInProgressProps {
  isValidationEnabled: boolean;
}

export const SubmitInProgress: FC<SubmitInProgressProps> = ({ isValidationEnabled = false }) => {
  return (
    <Box display="flex" gridGap="5px" marginLeft="auto">
      <CircularProgress size={16} color="primary" thickness={5} />
      {isValidationEnabled && (
        <Typography variant="body2" color="primary">
          Validating provider configuration fields... usually take 5-30s to complete.
        </Typography>
      )}
    </Box>
  );
};
