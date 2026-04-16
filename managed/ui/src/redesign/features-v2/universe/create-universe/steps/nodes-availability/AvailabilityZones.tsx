import { ReactNode } from 'react';
import { Trans, useTranslation } from 'react-i18next';
import { mui } from '@yugabyte-ui-library/core';
import { StyledContent, StyledHeader, StyledPanel } from '../../components/DefaultComponents';

import ErrorCircle from '@app/redesign/assets/error-circle.svg?img';

const { Box, styled, Divider } = mui;

const StyledError = styled(Box)(({ theme }) => ({
  display: 'flex',
  justifyContent: 'flex-start',
  alignItems: 'center',
  marginLeft: '24px',
  marginBottom: '16px',
  color: theme.palette.error[500],
  gap: '4px',
  fontSize: '11.5px',
  fontWeight: 400,
  lineHeight: '16px',
  '& img': {
    width: '12px',
    height: '12px',
    color: theme.palette.error[500]
  }
}));

interface AvailabilityZonesProps {
  showErrorsAfterSubmit?: boolean;
  showAvailabilityZonesError: boolean;
  azCount: number;
  faultToleranceNeeded: number;
  topContent?: ReactNode;
  bottomContent?: ReactNode;
  children?: ReactNode;
}

export const AvailabilityZones = ({
  showErrorsAfterSubmit = true,
  showAvailabilityZonesError,
  azCount,
  faultToleranceNeeded,
  topContent,
  bottomContent,
  children
}: AvailabilityZonesProps) => {
  const { t } = useTranslation('translation', {
    keyPrefix: 'createUniverseV2.nodesAndAvailability.availabilityZones'
  });

  return (
    <StyledPanel>
      <StyledHeader>{t('title')}</StyledHeader>
      {showErrorsAfterSubmit && showAvailabilityZonesError && (
        <StyledError>
          <Box sx={{ display: 'flex', gap: '4px', alignItems: 'center' }}>
            <img src={ErrorCircle} alt="error" />
            <Trans
              t={t}
              i18nKey={'errMsg.selectedAz'}
              components={{ b: <b /> }}
              values={{ count: azCount }}
            />
          </Box>
          <Divider
            orientation="vertical"
            variant="middle"
            sx={{ borderColor: '#DA1515', height: '10px', marginLeft: '4px', marginRight: '4px' }}
            flexItem
          />
          <Trans
            t={t}
            i18nKey={'errMsg.requiredAz'}
            components={{ b: <b /> }}
            values={{ count: faultToleranceNeeded }}
          />
        </StyledError>
      )}
      <StyledContent sx={{ gap: '16px' }}>
        {topContent}
        {children}
        {bottomContent}
      </StyledContent>
    </StyledPanel>
  );
};
