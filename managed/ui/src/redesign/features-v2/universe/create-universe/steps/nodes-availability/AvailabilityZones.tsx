import { useContext } from 'react';
import { isEmpty } from 'lodash';
import { useTranslation } from 'react-i18next';
import { useFormContext } from 'react-hook-form';
import { StyledContent, StyledHeader, StyledPanel } from '../../components/DefaultComponents';
import { RegionCard } from './index';
import { CreateUniverseContext, CreateUniverseContextMethods } from '../../CreateUniverseContext';
import { NodeAvailabilityProps } from './dtos';

export const AvailabilityZones = () => {
  const { t } = useTranslation('translation', {
    keyPrefix: 'createUniverseV2.nodesAndAvailability.availabilityZones'
  });
  const [{ resilienceAndRegionsSettings }] = (useContext(
    CreateUniverseContext
  ) as unknown) as CreateUniverseContextMethods;

  const { watch } = useFormContext<NodeAvailabilityProps>();

  const az = watch('availabilityZones');

  return (
    <StyledPanel>
      <StyledHeader>{t('title')}</StyledHeader>
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
