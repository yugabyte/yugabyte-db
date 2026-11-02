import { useContext } from 'react';
import { mui } from '@yugabyte-ui-library/core';
import { useFormContext } from 'react-hook-form';
import { NodeAvailabilityProps } from './dtos';
import { getNodeCount } from '../../CreateUniverseUtils';
import { useTranslation } from 'react-i18next';
import { CreateUniverseContext, CreateUniverseContextMethods } from '../../CreateUniverseContext';
import { CloudType } from '@app/redesign/helpers/dtos';
import {
  ENABLE_MULTI_PROVIDER_CREATE,
  getPrimaryProvider,
  getProviderConfigurations,
  isMultiProvider
} from '../../helpers/providerConfigurations';
import { AVAILABILITY_ZONES_BY_PROVIDER } from '../../fields/FieldNames';
import {
  getNodeCountFromByProvider,
  getNodeCountsByProvider,
  type AvailabilityZonesByProvider
} from '../../helpers/nodesByProvider';
import { CloudIconBox } from '../../helpers/CloudIconBox';

const { Box, styled } = mui;

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
  const [{ generalSettings }] = (useContext(
    CreateUniverseContext
  ) as unknown) as CreateUniverseContextMethods;
  const isK8s =
    generalSettings?.cloud === CloudType.kubernetes ||
    getPrimaryProvider(generalSettings)?.code === CloudType.kubernetes;
  const multiPlacement =
    ENABLE_MULTI_PROVIDER_CREATE && isMultiProvider(generalSettings);
  const providers = getProviderConfigurations(generalSettings);

  const { watch } = useFormContext<NodeAvailabilityProps>();
  const az = watch('availabilityZones');
  const byProvider = watch(AVAILABILITY_ZONES_BY_PROVIDER) as
    | AvailabilityZonesByProvider
    | undefined;
  const useDedicatedNodes = watch('useDedicatedNodes');
  const totalNodeCount = multiPlacement
    ? getNodeCountFromByProvider(byProvider)
    : getNodeCount(az);
  const totalNodesLabel = useDedicatedNodes
    ? t(isK8s ? 'totalPodsTserver' : 'totalNodesTserver')
    : t(isK8s ? 'totalPods' : 'totalNodes');

  const countsByProvider = multiPlacement ? getNodeCountsByProvider(byProvider) : null;
  const providerBreakdown =
    countsByProvider &&
    providers
      .map((provider) => ({
        uuid: provider.uuid,
        code: provider.code,
        count: countsByProvider[provider.uuid] ?? 0
      }))
      .filter((entry) => entry.count > 0);

  return (
    <NodesCount data-testid="total-node-count">
      <span>{totalNodesLabel}</span>
      {totalNodeCount}
      {providerBreakdown && providerBreakdown.length > 0 ? (
        <>
          <Box
            component="span"
            aria-hidden
            sx={{
              width: '1px',
              alignSelf: 'stretch',
              backgroundColor: 'grey.300',
              mx: '8px'
            }}
          />
          <Box
            component="span"
            sx={{ display: 'inline-flex', alignItems: 'center', gap: '12px' }}
            data-testid="total-node-count-by-provider"
          >
            {providerBreakdown.map((entry) => (
              <Box
                key={entry.uuid}
                component="span"
                sx={{ display: 'inline-flex', alignItems: 'center', gap: '4px' }}
              >
                <CloudIconBox code={entry.code} size={16} />
                {entry.count}
              </Box>
            ))}
          </Box>
        </>
      ) : null}
    </NodesCount>
  );
};
