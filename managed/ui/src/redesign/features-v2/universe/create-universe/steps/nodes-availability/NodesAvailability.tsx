import { forwardRef, useContext, useImperativeHandle, useState } from 'react';
import { FormProvider, useForm } from 'react-hook-form';
import { useMount } from 'react-use';
import { AlertVariant, mui, YBAlert } from '@yugabyte-ui-library/core';

import {
  YBMaps,
  YBMapMarker,
  MarkerType,
  MapLegend,
  MapLegendItem,
  useGetMapIcons
} from '@yugabyte-ui-library/core';
import { ClusterType, Region } from '../../../../../helpers/dtos';
import { FreeFormRFRequirement } from './FreeFormRFRequirement';
import {
  CreateUniverseContext,
  CreateUniverseContextMethods,
  StepsRef
} from '../../CreateUniverseContext';
import { GuidedRequirementDetails } from './GuidedRequirementDetails';
import { AvailabilityZones } from './AvailabilityZones';
import { ReplicationStatusCard } from './ReplicationStatusCard';
import { DedicatedNode } from './DedicatedNodes';
import { ResilienceFormMode, ResilienceType } from '../resilence-regions/dtos';
import { assignRegionsAZNodeByReplicationFactor } from '../../CreateUniverseUtils';
import { NodeAvailabilityProps } from './dtos';
import { NodesAvailabilitySchema } from './ValidationSchema';
import { yupResolver } from '@hookform/resolvers/yup';
import { Trans, useTranslation } from 'react-i18next';
import { isEmpty } from 'lodash';
import { ArrowDropDown } from '@material-ui/icons';
import { Fade, Grow } from '@material-ui/core';
import { Universe } from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';

import NodesIcon from '@app/redesign/assets/nodes.svg';

const { Box, styled } = mui;

const NodesAccordion = styled('div')(({ theme, expanded }) => ({
  border: `1px solid ${theme.palette.grey[200]}`,
  borderRadius: '8px',
  background: '#FBFCFD',
  height: expanded ? 'auto' : '56px',
  width: '100%',
  cursor: 'pointer',
  padding: '16px',
  '& .header': {
    display: 'flex',
    alignItems: 'center',
    justifyContent: 'space-between',
    fontSize: '13px',
    fontWeight: 600,
    color: theme.palette.grey[900],
    '& svg': {
      width: '24px',
      height: '24px'
    }
  },
  '& .details': {
    marginTop: '24px',
    columnGap: '32px',
    '& tr > * + *': {
      paddingLeft: '32px',
      paddingBottom: '6px'
    },
    '& .attrib': {
      fontSize: '11.5px',
      fontWeight: 500,
      color: theme.palette.grey[500],
      textTransform: 'uppercase'
    },
    '& .value': {
      fontSize: '13px',
      fontWeight: 400,
      color: theme.palette.grey[700]
    }
  }
}));

export const NodesAvailability = forwardRef<
  StepsRef,
  { isGeoPartition?: boolean; universeData?: Universe }
>(({ isGeoPartition = false, universeData = undefined }, forwardRef) => {
  const [
    { resilienceAndRegionsSettings, nodesAvailabilitySettings },
    { moveToPreviousPage, moveToNextPage, saveNodesAvailabilitySettings }
  ] = (useContext(CreateUniverseContext) as unknown) as CreateUniverseContextMethods;

  const { t } = useTranslation('translation', {
    keyPrefix: 'createUniverseV2.nodesAndAvailability'
  });
  const { t: tGeoPartition } = useTranslation('translation', {
    keyPrefix: 'geoPartition.geoPartitionNodesAndAvailability'
  });
  const regions = resilienceAndRegionsSettings?.regions ?? [];
  const icon = useGetMapIcons({ type: MarkerType.REGION_SELECTED });

  const methods = useForm<NodeAvailabilityProps>({
    defaultValues: nodesAvailabilitySettings,
    resolver: yupResolver(NodesAvailabilitySchema(t, resilienceAndRegionsSettings))
  });
  const availabilityZones = methods.watch('availabilityZones');
  const [isNodesAccordionOpen, setIsNodesAccordionOpen] = useState(false);

  useMount(() => {
    if (!resilienceAndRegionsSettings) return;
    if (isEmpty(availabilityZones)) {
      const availabilityZones = assignRegionsAZNodeByReplicationFactor(
        resilienceAndRegionsSettings
      );
      methods.setValue('availabilityZones', availabilityZones);
    }
  });

  useImperativeHandle(
    forwardRef,
    () => ({
      onNext: () => {
        return methods.handleSubmit((data) => {
          saveNodesAvailabilitySettings(data);
          moveToNextPage();
        })();
      },
      onPrev: () => {
        moveToPreviousPage();
      }
    }),
    []
  );
  const { errors } = methods.formState;
  if (!isGeoPartition) {
    return (
      <FormProvider {...methods}>
        <Box sx={{ display: 'flex', flexDirection: 'column', gap: '24px' }}>
          <YBMaps
            mapHeight={345}
            coordinates={[
              [37.3688, -122.0363],
              [34.052235, -118.243683]
            ]}
            initialBounds={undefined}
            defaultZoom={5}
            dataTestId="yb-maps-nodes-availability"
            mapContainerProps={{
              scrollWheelZoom: false,
              zoom: 2,
              center: [0, 0]
            }}
          >
            {
              regions?.map((region: Region) => {
                return (
                  <YBMapMarker
                    key={region.code}
                    position={[region.latitude, region.longitude]}
                    type={MarkerType.REGION_SELECTED}
                    tooltip={<>{region.name}</>}
                  />
                );
              }) as any
            }
            {regions?.length > 0 ? (
              <MapLegend
                mapLegendItems={[<MapLegendItem icon={<>{icon.normal}</>} label={'Region'} />]}
              />
            ) : (
              <span />
            )}
          </YBMaps>
          {resilienceAndRegionsSettings?.resilienceType === ResilienceType.REGULAR &&
            resilienceAndRegionsSettings?.resilienceFormMode === ResilienceFormMode.GUIDED && (
              <ReplicationStatusCard />
            )}
          {resilienceAndRegionsSettings?.resilienceFormMode === ResilienceFormMode.FREE_FORM ? (
            <FreeFormRFRequirement />
          ) : (
            <GuidedRequirementDetails />
          )}
          <AvailabilityZones />
          {(errors as any)?.lesserNodes?.message && (
            <YBAlert
              open
              variant={AlertVariant.Error}
              text={
                <Trans
                  t={t}
                  i18nKey={(errors as any)?.lesserNodes?.message}
                  components={{ b: <b /> }}
                >
                  {(errors as any).lesserNodes.message}
                </Trans>
              }
            />
          )}
          <DedicatedNode />
        </Box>
      </FormProvider>
    );
  }

  const primaryCluster = universeData?.spec?.clusters?.find(
    (cluster) => cluster.cluster_type === ClusterType.PRIMARY
  );
  const nodeSpec = primaryCluster?.node_spec;
  const instanceType = nodeSpec?.instance_type ?? '-';
  const volumeSize = nodeSpec?.storage_spec?.volume_size ?? '-';
  const volumeType = nodeSpec?.storage_spec?.storage_type ?? '-';
  const iops = nodeSpec?.storage_spec?.disk_iops ?? '-';
  const throughput = nodeSpec?.storage_spec?.throughput ?? '-';

  // geo Parition
  return (
    <FormProvider {...methods}>
      <Box sx={{ display: 'flex', flexDirection: 'row', gap: '16px' }}>
        <Box sx={{ display: 'flex', flexDirection: 'column', gap: '24px', width: '720px' }}>
          {resilienceAndRegionsSettings?.resilienceType === ResilienceType.REGULAR &&
            resilienceAndRegionsSettings?.resilienceFormMode === ResilienceFormMode.GUIDED && (
              <ReplicationStatusCard />
            )}
          {resilienceAndRegionsSettings?.resilienceFormMode === ResilienceFormMode.FREE_FORM ? (
            <FreeFormRFRequirement />
          ) : (
            <GuidedRequirementDetails />
          )}
          <AvailabilityZones />
          {(errors as any)?.lesserNodes?.message && (
            <YBAlert
              open
              variant={AlertVariant.Error}
              text={
                <Trans
                  t={t}
                  i18nKey={(errors as any)?.lesserNodes?.message}
                  components={{ b: <b /> }}
                >
                  {(errors as any).lesserNodes.message}
                </Trans>
              }
            />
          )}
        </Box>
        <Box sx={{ display: 'flex', flexDirection: 'column', gap: '16px' }}>
          <YBMaps
            mapHeight={360}
            mapWidth={360}
            coordinates={[
              [37.3688, -122.0363],
              [34.052235, -118.243683]
            ]}
            initialBounds={undefined}
            defaultZoom={5}
            dataTestId="yb-maps-nodes-availability"
            mapContainerProps={{
              scrollWheelZoom: false,
              zoom: 2,
              center: [0, 0]
            }}
          >
            {
              regions?.map((region: Region) => {
                return (
                  <YBMapMarker
                    key={region.code}
                    position={[region.latitude, region.longitude]}
                    type={MarkerType.REGION_SELECTED}
                    tooltip={<>{region.name}</>}
                  />
                );
              }) as any
            }
            {regions?.length > 0 ? (
              <MapLegend
                mapLegendItems={[<MapLegendItem icon={<>{icon.normal}</>} label={'Region'} />]}
              />
            ) : (
              <span />
            )}
          </YBMaps>
          <NodesAccordion
            onClick={() => setIsNodesAccordionOpen(!isNodesAccordionOpen)}
            expanded={isNodesAccordionOpen}
          >
            <div className="header">
              <Box sx={{ display: 'flex', alignItems: 'center', gap: '8px' }}>
                <NodesIcon />
                {tGeoPartition('nodes')}
              </Box>
              <ArrowDropDown
                style={{
                  transform: isNodesAccordionOpen ? 'rotate(180deg)' : 'rotate(0deg)',
                  transition: 'transform 0.3s ease'
                }}
              />
            </div>
            <Fade in={isNodesAccordionOpen} timeout={300}>
              <Box className="details">
                <table>
                  <tbody>
                    <tr>
                      <td className="attrib">{tGeoPartition('instanceType')}</td>
                      <td>{instanceType}</td>
                    </tr>
                    <tr>
                      <td className="attrib">{tGeoPartition('volume')}</td>
                      <td>{volumeSize}</td>
                    </tr>
                    <tr>
                      <td className="attrib">{tGeoPartition('ebsType')}</td>
                      <td>{volumeType}</td>
                    </tr>
                    <tr>
                      <td className="attrib">{tGeoPartition('iops')}</td>
                      <td>{iops}</td>
                    </tr>
                    <tr>
                      <td className="attrib">{tGeoPartition('throughput')}</td>
                      <td>{throughput}</td>
                    </tr>
                  </tbody>
                </table>
                <Box></Box>
              </Box>
            </Fade>
          </NodesAccordion>
        </Box>
      </Box>
    </FormProvider>
  );
});

NodesAvailability.displayName = 'NodesAvailability';
