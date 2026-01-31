import { useContext } from 'react';
import { useTranslation } from 'react-i18next';
import { useFormContext } from 'react-hook-form';
import { mui, YBInput } from '@yugabyte-ui-library/core';
import { StyledContent, StyledHeader, StyledPanel } from '../../components/DefaultComponents';
import { getNodeCount } from '../../CreateUniverseUtils';
import { CreateUniverseContext, CreateUniverseContextMethods } from '../../CreateUniverseContext';
import { NodeAvailabilityProps } from './dtos';
import { FaultToleranceType, ResilienceType } from '../resilence-regions/dtos';

//icons
import Return from '../../../../../assets/tree.svg';

const { styled } = mui;

const StyledNodesCount = styled('div')(({ theme }) => ({
  padding: theme.spacing(3),
  display: 'flex',
  flexDirection: 'column',
  gap: theme.spacing(1),
  background: '#FBFCFD',
  border: `1px solid ${theme.palette.grey[300]}`,
  borderRadius: '8px',
  width: '672px'
}));

const NodesCount = styled(
  'span',
  {}
)(({ theme }) => ({
  padding: '12px 16px',
  background: '#F7F9FB',
  border: `1px solid ${theme.palette.grey[300]}`,
  borderRadius: '8px',
  display: 'flex',
  gap: '10px',
  width: 'fit-content',
  fontWeight: 600,
  fontSize: '13px',
  color: theme.palette.grey[700]
}));

export const GuidedRequirementDetails = () => {
  const { watch, setValue } = useFormContext<NodeAvailabilityProps>();

  const [{ resilienceAndRegionsSettings }] = (useContext(
    CreateUniverseContext
  ) as unknown) as CreateUniverseContextMethods;

  const { t } = useTranslation('translation', {
    keyPrefix: 'createUniverseV2.nodesAndAvailability.guidedMode'
  });

  const az = watch('availabilityZones');

  const nodePerAz = watch('nodeCountPerAz');
  const isDedicatedNodes = watch('useDedicatedNodes');
  const totalNodeCount = getNodeCount(az);

  const handleNodeCountChange = (event: React.ChangeEvent<HTMLInputElement>) => {
    const value = parseInt(event.target.value, 10);
    if (value < 1) return;
    const updatedAz: NodeAvailabilityProps['availabilityZones'] = {};

    for (const regionCode in az) {
      updatedAz[regionCode] = az[regionCode].map((zone) => ({
        ...zone,
        nodeCount: value
      }));
    }
    setValue('availabilityZones', updatedAz);
    setValue('nodeCountPerAz', value);
  };

  if (resilienceAndRegionsSettings?.faultToleranceType === FaultToleranceType.NONE) {
    return null;
  }

  return (
    <StyledPanel>
      <StyledHeader>{t('nodes')}</StyledHeader>
      <StyledContent>
        <StyledNodesCount>
          <div style={{ display: 'flex', alignItems: 'center', gap: '16px' }}>
            <YBInput
              type="number"
              name="nodeCount"
              sx={{ width: '100px' }}
              value={nodePerAz ?? 1}
              onChange={handleNodeCountChange}
              disabled={resilienceAndRegionsSettings?.resilienceType === ResilienceType.SINGLE_NODE}
              dataTestId="node-count-per-az-field"
            />
            {isDedicatedNodes ? t('nodesTserverPerAz') : t('nodesPerAz')}
          </div>
          <div style={{ display: 'flex', alignItems: 'center', gap: '8px' }}>
            <Return />
            <NodesCount>
              {totalNodeCount}
              <span>{t('totalNodes')}</span>
            </NodesCount>
          </div>
        </StyledNodesCount>
      </StyledContent>
    </StyledPanel>
  );
};
