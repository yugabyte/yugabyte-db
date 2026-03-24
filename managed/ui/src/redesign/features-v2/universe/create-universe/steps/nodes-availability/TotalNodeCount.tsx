import { mui } from '@yugabyte-ui-library/core';
import { useFormContext } from 'react-hook-form';
import { NodeAvailabilityProps } from './dtos';
import { getNodeCount } from '../../CreateUniverseUtils';
import { useTranslation } from 'react-i18next';
import { useContext } from 'react';
import { CreateUniverseContext, CreateUniverseContextMethods } from '../../CreateUniverseContext';
import { ResilienceFormMode } from '../resilence-regions/dtos';

const { styled } = mui;

const NodesCount = styled('span', {})(({ theme }) => ({
  padding: '12px 16px',
  background: '#F7F9FB',
  border: `1px solid ${theme.palette.grey[300]}`,
  borderRadius: '8px',
  display: 'flex',
  gap: '5px',
  width: 'fit-content',
  fontWeight: 600,
  fontSize: '13px',
  color: theme.palette.grey[700],
  alignItems: 'center'
}));

export const TotalNodeCount = () => {
  const { t } = useTranslation('translation', {
    keyPrefix: 'createUniverseV2.nodesAndAvailability.guidedMode'
  });

  const [{ resilienceAndRegionsSettings }] = (useContext(
    CreateUniverseContext
  ) as unknown) as CreateUniverseContextMethods;

  const { watch } = useFormContext<NodeAvailabilityProps>();
  const az = watch('availabilityZones');
  const totalNodeCount = getNodeCount(az);
  if (resilienceAndRegionsSettings?.resilienceFormMode === ResilienceFormMode.EXPERT_MODE) {
    return null;
  }
  return (
    <NodesCount>
      <span>{t('totalNodes')}</span>
      {totalNodeCount}
    </NodesCount>
  );
};
