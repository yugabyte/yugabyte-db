import { FC } from 'react';
import { useTranslation } from 'react-i18next';
import { Box, Typography } from '@material-ui/core';
import { TroubleshootAdvisor } from './TroubleshootAdvisor';
import { YBM_MULTI_REGION_INFO } from './MockData';
import { BrowserRouter as Router, Link } from 'react-router-dom';

export const Troubleshoot: FC<any> = () => {
  const { t } = useTranslation();

  return (
    <Box sx={{ width: '100%' }}>
      <Box sx={{ borderBottom: 1, borderColor: 'divider' }}>
        <Router>
          <Link to={{ pathname: `/troubleshoot` }}>
            <Typography variant="h6" className="content-title">
              {'Troubleshoot'}
            </Typography>
          </Link>
        </Router>
        <Box m={2}>
          <TroubleshootAdvisor data={YBM_MULTI_REGION_INFO} />
        </Box>
      </Box>
    </Box>
  );
};
