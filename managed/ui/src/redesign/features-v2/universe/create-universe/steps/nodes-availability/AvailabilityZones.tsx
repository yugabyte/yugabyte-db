import { useContext } from 'react';
import { isEmpty } from 'lodash';
import { Trans, useTranslation } from 'react-i18next';
import { useFormContext } from 'react-hook-form';
import { mui } from '@yugabyte-ui-library/core';
import { StyledContent, StyledHeader, StyledPanel } from '../../components/DefaultComponents';
import { RegionCard } from './index';
import { CreateUniverseContext, CreateUniverseContextMethods } from '../../CreateUniverseContext';
import { NodeAvailabilityProps } from './dtos';
import { getFaultToleranceNeededForAZ } from '../../CreateUniverseUtils';

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

export const AvailabilityZones = () => {
  const { t } = useTranslation('translation', {
    keyPrefix: 'createUniverseV2.nodesAndAvailability.availabilityZones'
  });
  const [{ resilienceAndRegionsSettings }] = (useContext(
    CreateUniverseContext
  ) as unknown) as CreateUniverseContextMethods;

  const {
    watch,
    formState: { errors }
  } = useFormContext<NodeAvailabilityProps>();

  const az = watch('availabilityZones');
  const azCount = Object.keys(az).reduce((acc, region) => acc + az[region].length, 0);
  const faultToleranceNeeded = getFaultToleranceNeededForAZ(
    resilienceAndRegionsSettings?.replicationFactor ?? 1
  );

  return (
    <StyledPanel>
      <StyledHeader>{t('title')}</StyledHeader>
      {(errors as any)?.availabilityZones?.message && (
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
        {!isEmpty(az) &&
          Object.keys(az).map((regionCode, index) => {
            const region = resilienceAndRegionsSettings?.regions.find((r) => r.code === regionCode);
            return region && <RegionCard key={region.uuid} region={region} index={index} />;
          })}
      </StyledContent>
    </StyledPanel>
  );
};
