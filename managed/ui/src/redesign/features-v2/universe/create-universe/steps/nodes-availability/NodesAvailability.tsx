import { forwardRef, useContext, useEffect, useImperativeHandle, useState } from 'react';
import { isEmpty } from 'lodash';
import { useMount } from 'react-use';
import { Trans, useTranslation } from 'react-i18next';
import { FormProvider, useForm } from 'react-hook-form';
import { yupResolver } from '@hookform/resolvers/yup';
import {
  AlertVariant,
  mui,
  YBAlert,
  YBMaps,
  YBMapMarker,
  MarkerType,
  MapLegend,
  MapLegendItem,
  useGetMapIcons
} from '@yugabyte-ui-library/core';
import {
  AvailabilityZones,
  DedicatedNode
} from '.';
import { values } from 'lodash';
import {
  assignRegionsAZNodeByReplicationFactor,
  getFaultToleranceNeeded
} from '../../CreateUniverseUtils';
import { getGuidedNodesStepReplicationFactor } from '../resilence-regions/GuidedResilienceRequirementSummary';
import { NodesAvailabilitySchema } from './ValidationSchema';
import { Universe } from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';
import {
  CreateUniverseContext,
  CreateUniverseContextMethods,
  StepsRef
} from '../../CreateUniverseContext';
import { NodeAvailabilityProps } from './dtos';
import { ClusterType, Region } from '../../../../../helpers/dtos';
import { ResilienceFormMode, ResilienceType } from '../resilence-regions/dtos';

//icons
import { ArrowDropDown } from '@material-ui/icons';
import NodesIcon from '@app/redesign/assets/nodes.svg';
import { REPLICATION_FACTOR } from '../../fields/FieldNames';
import { ResilienceRequirementCard } from '../resilence-regions/ResilienceRequirementCard';

const { Box, styled, Fade } = mui;

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
    resolver: yupResolver(NodesAvailabilitySchema(resilienceAndRegionsSettings))
  });
  const { trigger } = methods;
  const availabilityZones = methods.watch('availabilityZones');
  const faultToleranceNeeded = getFaultToleranceNeeded(
    resilienceAndRegionsSettings?.resilienceFactor ?? 1
  );
  const totalAzCount = values(availabilityZones).reduce(
    (acc, zones) => acc + zones.length,
    0
  );
  const lesserNodesTransValues = {
    faultToleranceNeeded,
    required_zones: faultToleranceNeeded,
    max_az: faultToleranceNeeded - 1,
    nodeCount: totalAzCount,
    availability_zone: totalAzCount,
    selected_regions: regions.length
  };
  const [isNodesAccordionOpen, setIsNodesAccordionOpen] = useState(false);
  const [showErrorsAfterSubmit, setShowErrorsAfterSubmit] = useState(false);
  const { errors, isSubmitted } = methods.formState;

  // Clear errors as soon as validation passes (e.g. after user fixes AZ count or preferred ranks).
  // React to formState.errors so we don't depend on effect timing or trigger() resolving with stale state.
  const hasErrors = errors && Object.keys(errors).length > 0;
  useEffect(() => {
    if (isSubmitted && !hasErrors) {
      setShowErrorsAfterSubmit(false);
    }
  }, [isSubmitted, hasErrors]);

  // Re-validate when AZ data changes so formState.errors stays in sync. Use serialized AZ data so
  // add/remove AZ always triggers (watch() reference may not change for nested setValue).
  useEffect(() => {
    if (isSubmitted) trigger();
  }, [JSON.stringify(availabilityZones), isSubmitted, trigger]);

  useMount(() => {
    if (!resilienceAndRegionsSettings) return;

    if (resilienceAndRegionsSettings.resilienceFormMode === ResilienceFormMode.GUIDED) {
      methods.setValue(
        REPLICATION_FACTOR,
        getGuidedNodesStepReplicationFactor(
          resilienceAndRegionsSettings.faultToleranceType,
          resilienceAndRegionsSettings.resilienceFactor
        )
      );
    }

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
        setShowErrorsAfterSubmit(true);
        return methods.handleSubmit((data) => {
          saveNodesAvailabilitySettings(data);
          moveToNextPage();
        })();
      },
      onPrev: () => {
        moveToPreviousPage();
      },
      setValue: methods.setValue as (name: string, value: unknown) => void
    }),
    [methods, saveNodesAvailabilitySettings, moveToNextPage, moveToPreviousPage]
  );
  if (!isGeoPartition) {
    return (
      <FormProvider {...methods}>
        <Box sx={{ display: 'flex', flexDirection: 'column', gap: '24px' }}>
          <YBMaps
            mapHeight={216}
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
              <ResilienceRequirementCard
                resilienceAndRegionsProps={resilienceAndRegionsSettings}
                noShadow
                placementStep="nodes"
              />
            )}
          <AvailabilityZones showErrorsAfterSubmit={showErrorsAfterSubmit} />
          {showErrorsAfterSubmit && (errors as any)?.lesserNodes?.message && (
            <YBAlert
              open
              variant={AlertVariant.Error}
              text={
                <Trans
                  t={t}
                  i18nKey={(errors as any)?.lesserNodes?.message}
                  components={{ b: <b /> }}
                  values={lesserNodesTransValues}
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
          <AvailabilityZones showErrorsAfterSubmit={showErrorsAfterSubmit} />
          {showErrorsAfterSubmit && (errors as any)?.lesserNodes?.message && (
            <YBAlert
              open
              variant={AlertVariant.Error}
              text={
                <Trans
                  t={t}
                  i18nKey={(errors as any)?.lesserNodes?.message}
                  components={{ b: <b /> }}
                  values={lesserNodesTransValues}
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
