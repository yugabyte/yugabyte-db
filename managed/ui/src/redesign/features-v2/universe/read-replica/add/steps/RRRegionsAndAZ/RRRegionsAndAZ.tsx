import {
  forwardRef,
  useContext,
  useEffect,
  useImperativeHandle,
  useMemo,
  useState
} from 'react';
import { useForm, FormProvider, useFieldArray, useWatch } from 'react-hook-form';
import { yupResolver } from '@hookform/resolvers/yup';
import { useTranslation, Trans } from 'react-i18next';
import { useQuery } from 'react-query';
import { IconButton } from '@material-ui/core';
import CloseRounded from '@app/redesign/assets/close-large.svg';
import {
  mui,
  YBMaps,
  YBMapMarker,
  MarkerType,
  MapLegend,
  MapLegendItem,
  useGetMapIcons,
  YBButton
} from '@yugabyte-ui-library/core';
import {
  ClusterSpecClusterType,
  UniverseRespResponse
} from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';
import { Region } from '@app/redesign/features/universe/universe-form/utils/dto';
import { api, QUERY_KEY } from '@app/redesign/features/universe/universe-form/utils/api';
import { getClusterByType } from '../../../../edit-universe/EditUniverseUtils';
import { RRBreadCrumbs } from '../../ReadReplicaBreadCrumbs';
import {
  StepsRef,
  AddRRContext,
  AddRRContextMethods,
  AddReadReplicaSteps
} from '../../AddReadReplicaContext';
import InfoIcon from '@app/redesign/assets/book_open_blue.svg';
import AddIcon from '@app/redesign/assets/add2.svg';
import {
  getDefaultRRRegionsAndAZ,
  getEmptyRRPlacementRegion,
  RRRegionsAndAZFormValues
} from './dtos';
import { RRRegionsAndAZValidationSchema } from './ValidationSchema';
import { RRRegionCard } from './RRRegionCard';
import { NodeInstanceDetails } from '@app/redesign/features-v2/universe/geo-partition/add/NodeInstanceDetails';
import { sumReadReplicaNodeCounts } from '../../addReadReplicaClusterPayload';

const { Box, styled, Typography } = mui;

const StyledOuterPanel = styled('div')(({ theme }) => ({
  width: '100%',
  maxWidth: '720px',
  borderRadius: '8px',
  border: `1px solid ${theme.palette.grey[200]}`,
  overflow: 'hidden',
  backgroundColor: theme.palette.common.white
}));

const StyledPanelHeader = styled('div')(({ theme }) => ({
  display: 'flex',
  alignItems: 'center',
  height: '64px',
  padding: `${theme.spacing(1.25)} ${theme.spacing(3)}`,
  borderBottom: `1px solid ${theme.palette.grey[200]}`,
  backgroundColor: theme.palette.common.white
}));

const StyledBanner = styled(Box)(({ theme }) => ({
  padding: `${theme.spacing(2)} ${theme.spacing(3)}`,
  display: 'flex',
  alignItems: 'center',
  gap: theme.spacing(2),
  borderRadius: '8px',
  border: `1px solid #CBCCFB`,
  backgroundColor: theme.palette.common.white,
  color: theme.palette.grey[700],
  position: 'relative',
  '& a': {
    color: theme.palette.primary[600],
    textDecoration: 'underline'
  }
}));

type MapPin = { key: string; lat: number; lng: number; name: string };

function getPrimaryMapPins(
  universeData: UniverseRespResponse | undefined,
  providerRegions: Region[]
): MapPin[] {
  if (!universeData) return [];
  const primary = getClusterByType(universeData, ClusterSpecClusterType.PRIMARY);
  const regionList = (primary?.placement_spec?.cloud_list?.[0] as { region_list?: { uuid?: string; name?: string }[] })
    ?.region_list;
  if (!regionList?.length) return [];
  const byUuid = new Map(providerRegions.map((r) => [r.uuid, r]));
  const out: MapPin[] = [];
  regionList.forEach((pr) => {
    if (!pr.uuid) return;
    const meta = byUuid.get(pr.uuid);
    if (!meta || meta.latitude === null || meta.longitude === null) return;
    out.push({
      key: `primary-${pr.uuid}`,
      lat: meta.latitude,
      lng: meta.longitude,
      name: meta.name ?? pr.name ?? ''
    });
  });
  return out;
}

function RRPlacementMap({
  universeData,
  providerRegions
}: {
  universeData?: UniverseRespResponse;
  providerRegions: Region[];
}) {
  const { t } = useTranslation('translation', { keyPrefix: 'readReplica.addRR' });
  const watchRegions = useWatch({ name: 'regions' }) as RRRegionsAndAZFormValues['regions'] | undefined;
  const primaryIcon = useGetMapIcons({ type: MarkerType.REGION_SELECTED });
  const rrIcon = useGetMapIcons({ type: MarkerType.READ_REPLICA });
  const readReplicaCluster = universeData ? getClusterByType(universeData, ClusterSpecClusterType.ASYNC) : undefined;
  const primaryPins = useMemo(
    () => getPrimaryMapPins(universeData, providerRegions),
    [universeData, providerRegions]
  );

  const draftRrPins = useMemo(() => {
    const list: MapPin[] = [];
    (watchRegions ?? []).forEach((row, idx) => {
      if (!row?.regionUuid) return;
      const r = providerRegions.find((x) => x.uuid === row.regionUuid);
      if (!r || r.latitude === null || r.longitude === null) return;
      list.push({
        key: `rr-draft-${idx}-${row.regionUuid}`,
        lat: r.latitude,
        lng: r.longitude,
        name: r.name
      });
    });
    return list;
  }, [watchRegions, providerRegions]);

  const coordinates = useMemo(() => {
    const all = [...primaryPins, ...draftRrPins];
    if (!all.length) {
      return [
        [0, 0],
        [0, 0]
      ] as [number, number][];
    }
    if (all.length === 1) {
      return [
        [all[0].lat, all[0].lng],
        [all[0].lat, all[0].lng]
      ] as [number, number][];
    }
    return all.map((p) => [p.lat, p.lng] as [number, number]);
  }, [primaryPins, draftRrPins]);

  return (
    <Box sx={{ width: '360px', flexShrink: 0, display: 'flex', flexDirection: 'column', gap: '16px' }}>
      <YBMaps
        mapHeight={362}
        dataTestId="yb-maps-rr-regions-az"
        coordinates={coordinates}
        initialBounds={undefined}
        mapContainerProps={{
          scrollWheelZoom: false,
          zoom: 2,
          center: [0, 0]
        }}
      >
        {(primaryPins.map((p) => (
          <YBMapMarker
            key={p.key}
            position={[p.lat, p.lng]}
            type={MarkerType.REGION_SELECTED}
            tooltip={<>{p.name}</>}
          />
        )) as unknown) as any}
        {(draftRrPins.map((p) => (
          <YBMapMarker
            key={p.key}
            position={[p.lat, p.lng]}
            type={MarkerType.READ_REPLICA}
            tooltip={<>{p.name}</>}
          />
        )) as unknown) as any}
        <MapLegend
          mapLegendItems={
            [
              <MapLegendItem key="primary-legend" icon={<>{primaryIcon.normal}</>} label={t('legendPrimaryCluster')} />,
              <MapLegendItem key="rr-legend" icon={<>{rrIcon.normal}</>} label={t('legendReadReplica')} />
            ] as any
          }
        />
      </YBMaps>
      {readReplicaCluster && <NodeInstanceDetails cluster={readReplicaCluster} />}
    </Box>
  );
}

export const RRRegionsAndAZ = forwardRef<StepsRef>((_, ref) => {
  const [
    { regionsAndAZ, regionsAndAZBaseline, readReplicaPlacementFromUniverse, universeData, activeStep },
    {
      moveToNextPage,
      moveToPreviousPage,
      saveRegionsAndAZSettings,
      spliceRegionsAndAZBaseline
    }
  ] = (useContext(AddRRContext) as unknown) as AddRRContextMethods;

  const { t } = useTranslation('translation', { keyPrefix: 'readReplica.addRR' });
  const { t: tc } = useTranslation('translation', { keyPrefix: 'common' });

  const [bannerVisible, setBannerVisible] = useState(true);

  const primaryCluster = universeData
    ? getClusterByType(universeData, ClusterSpecClusterType.PRIMARY)
    : undefined;
  const providerUUID = primaryCluster?.provider_spec?.provider ?? '';

  const { data: regionsList = [], isLoading: isRegionsLoading } = useQuery(
    [QUERY_KEY.getRegionsList, providerUUID],
    () => api.getRegionsList(providerUUID),
    { enabled: Boolean(providerUUID) }
  );

  const defaultValues = useMemo<RRRegionsAndAZFormValues>(
    () => regionsAndAZ ?? getDefaultRRRegionsAndAZ(),
    [regionsAndAZ]
  );

  const resolver = useMemo(
    () => yupResolver(RRRegionsAndAZValidationSchema(t)),
    [t]
  );

  const methods = useForm<RRRegionsAndAZFormValues>({
    defaultValues,
    resolver,
    mode: 'onChange'
  });

  const { control, handleSubmit, reset } = methods;

  const watchedRegions = useWatch({ control, name: 'regions' });
  const totalPlacementNodes = useMemo(
    () => sumReadReplicaNodeCounts({ regions: watchedRegions ?? [] }),
    [watchedRegions]
  );

  const { fields: regionFields, append: appendRegion, remove: removeRegion } = useFieldArray({
    control,
    name: 'regions'
  });

  useEffect(() => {
    if (regionsAndAZ && activeStep === AddReadReplicaSteps.REGIONS_AND_AZ) {
      reset(regionsAndAZ);
    }
  }, [regionsAndAZ, reset, activeStep]);

  useImperativeHandle(
    ref,
    () => ({
      onNext: () => {
        return handleSubmit((data) => {
          saveRegionsAndAZSettings(data);
          moveToNextPage();
        })();
      },
      onPrev: () => {
        moveToPreviousPage();
      }
    }),
    [handleSubmit, saveRegionsAndAZSettings, moveToNextPage, moveToPreviousPage]
  );

  return (
    <FormProvider {...methods}>
      <Box sx={{ display: 'flex', flexDirection: 'column', gap: '24px' }}>
        <RRBreadCrumbs groupTitle={t('placement')} subTitle={t('regionsAndAZ')} />
        {bannerVisible && (
          <StyledBanner>
            <InfoIcon style={{ flexShrink: 0 }} />
            <Box sx={{ flex: 1, pr: 4 }}>
              <Trans t={t} i18nKey="regionAZHelpText" components={{ a: <a href="#" /> }} />
            </Box>
            <IconButton
              size="small"
              aria-label={tc('close')}
              onClick={() => setBannerVisible(false)}
            >
              <CloseRounded />
            </IconButton>
          </StyledBanner>
        )}
        <Box sx={{ display: 'flex', flexDirection: 'row', gap: '16px', alignItems: 'flex-start' }}>
          <StyledOuterPanel>
            <StyledPanelHeader>
              <Typography
                sx={{
                  fontSize: '15px',
                  fontWeight: 600,
                  lineHeight: '20px',
                  color: 'grey.900'
                }}
              >
                {t('regionsSectionTitle')}
              </Typography>
            </StyledPanelHeader>
            <Box
              sx={{
                display: 'flex',
                flexDirection: 'column',
                gap: '24px',
                pt: 1,
                px: 3,
                pb: 3
              }}
            >
              {isRegionsLoading && (
                <Typography variant="body2" color="textSecondary">
                  {t('loadingRegions')}
                </Typography>
              )}
              {!isRegionsLoading &&
                regionFields.map((field, index) => (
                  <RRRegionCard
                    key={field.id}
                    regionIndex={index}
                    regionsList={regionsList as Region[]}
                    baselineRegion={regionsAndAZBaseline?.regions?.[index]}
                    allowAzUndo={Boolean(readReplicaPlacementFromUniverse)}
                    showRemoveRegion={regionFields.length > 1}
                    onRemoveRegion={
                      regionFields.length > 1
                        ? () => {
                          spliceRegionsAndAZBaseline(index);
                          removeRegion(index);
                        }
                        : undefined
                    }
                  />
                ))}
              {!isRegionsLoading && regionFields.length > 0 ? (
                <Box
                  sx={{
                    display: 'inline-flex',
                    alignItems: 'center',
                    gap: 1,
                    px: 2,
                    py: 1.25,
                    borderRadius: '8px',
                    border: (theme) => `1px solid ${theme.palette.grey[300]}`,
                    bgcolor: 'grey.50',
                    alignSelf: 'flex-start'
                  }}
                  data-testid="rr-total-nodes-placement"
                >
                  <Typography
                    sx={{
                      fontSize: '13px',
                      fontWeight: 600,
                      lineHeight: '20px',
                      color: 'grey.600',
                      letterSpacing: '0.026px'
                    }}
                  >
                    {t('totalNodesPlacement')}
                  </Typography>
                  <Typography
                    sx={{
                      fontSize: '13px',
                      fontWeight: 600,
                      lineHeight: '20px',
                      color: 'grey.600',
                      letterSpacing: '0.2px'
                    }}
                  >
                    {totalPlacementNodes}
                  </Typography>
                </Box>
              ) : null}
              <Box>
                <YBButton
                  variant="secondary"
                  size="large"
                  startIcon={<AddIcon />}
                  disabled={
                    !regionsList.length || regionFields.length >= (regionsList as Region[]).length
                  }
                  onClick={() => appendRegion(getEmptyRRPlacementRegion({ isNew: true }))}
                  dataTestId="rr-add-region"
                >
                  {t('addRegion')}
                </YBButton>
              </Box>
            </Box>
          </StyledOuterPanel>
          <RRPlacementMap universeData={universeData} providerRegions={regionsList as Region[]} />
        </Box>
      </Box>
    </FormProvider>
  );
});

RRRegionsAndAZ.displayName = 'RRRegionsAndAZ';
