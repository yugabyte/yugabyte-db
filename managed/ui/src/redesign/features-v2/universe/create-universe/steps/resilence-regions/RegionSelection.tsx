import {
  useCallback,
  useContext,
  useMemo,
  type HTMLAttributes
} from 'react';
import { CloudIconBox } from '../../helpers/CloudIconBox';
import { isEmpty, uniqBy } from 'lodash';
import { useQueries, useQuery } from 'react-query';
import { useTranslation } from 'react-i18next';
import { useFormContext } from 'react-hook-form';
import {
  MarkerType,
  YBAutoComplete,
  YBMapMarker,
  YBMaps,
  MapLegend,
  MapLegendItem,
  useGetMapIcons,
  YBInputField,
  YBLabel,
  mui
} from '@yugabyte-ui-library/core';
import { AvailabilityZoneField } from '../../fields';
import { StyledContent, StyledHeader } from '../../components/DefaultComponents';
import { api, QUERY_KEY } from '../../../../../features/universe/universe-form/utils/api';
import { canSelectMultipleRegions } from '../../CreateUniverseUtils';
import { CreateUniverseContext, CreateUniverseContextMethods } from '../../CreateUniverseContext';
import { CloudType, Region } from '../../../../../features/universe/universe-form/utils/dto';
import {
  FaultToleranceType,
  ResilienceAndRegionsProps,
  ResilienceFormMode,
  ResilienceType
} from './dtos';
import {
  NODE_COUNT,
  REGIONS_BY_PROVIDER,
  REGIONS_FIELD,
  RESILIENCE_FORM_MODE,
  RESILIENCE_TYPE,
  SINGLE_AVAILABILITY_ZONE
} from '../../fields/FieldNames';
import { getFlagFromRegion } from '../../helpers/RegionToFlagUtils';
import pluralize from 'pluralize';
import {
  getPrimaryProvider,
  getProviderConfigurations
} from '../../helpers/providerConfigurations';
import {
  flattenRegionsByProvider,
  isMultiProviderPlacement,
  stampRegionProviderUuid
} from '../../helpers/placementsByProvider';
import { BASE_CLOUD_OPTIONS } from '../../helpers/generalSettingsDefaults';

const { Box, MenuItem, styled, Typography } = mui;

/** Figma Multi-Provider map legend pin colors (node 3830:146432). */
const CLOUD_LEGEND_COLOR: Partial<Record<CloudType, string>> = {
  [CloudType.aws]: '#F5A623',
  [CloudType.azu]: '#0891B2',
  [CloudType.gcp]: '#34A853',
  [CloudType.oci]: '#F80000'
};

function cloudOptionForCode(code?: string) {
  return BASE_CLOUD_OPTIONS.find((option) => option.value === code);
}

function CloudRegionLabel({ code, text }: { code?: string; text: string }) {
  return (
    <Box sx={{ display: 'flex', alignItems: 'center', gap: '6px' }}>
      <CloudIconBox code={code} size={16} />
      <span>{text}</span>
    </Box>
  );
}

function ProviderLegendIcon({ code }: { code?: string }) {
  const color = (code && CLOUD_LEGEND_COLOR[code as CloudType]) ?? '#6D7C88';
  return (
    <Box sx={{ display: 'inline-flex', alignItems: 'center', gap: '4px' }}>
      <Box
        aria-hidden
        sx={{
          width: 10,
          height: 10,
          borderRadius: '50%',
          backgroundColor: color,
          flexShrink: 0
        }}
      />
      <CloudIconBox code={code} size={14} />
    </Box>
  );
}

const getRegionOptionLabel = (region: string | Record<string, string>): string => {
  if (typeof region === 'string') return region;
  const flag = getFlagFromRegion(region.code);
  const name = region.name ?? '';
  return flag ? `${flag} ${name}` : name;
};

const StyledMenu = styled(MenuItem)({
  display: 'flex',
  flexDirection: 'row',
  justifyContent: 'flex-start',
  padding: '4px 16px',
  height: 'auto !important',
  minHeight: '52px !important'
});

interface RegionSelectionProps {
  showErrorsAfterSubmit?: boolean;
}

function RegionOptionMenuItem({
  props,
  row,
  t
}: {
  props: HTMLAttributes<HTMLLIElement>;
  row: Record<string, any>;
  t: (key: string, options?: Record<string, unknown>) => string;
}) {
  return (
    <StyledMenu {...props}>
      <Box sx={{ display: 'flex', flexDirection: 'column', gap: '4px' }}>
        <Box>
          {getFlagFromRegion(row.code)}
          <span style={{ marginLeft: '4px' }}>{row.name}</span>
        </Box>
        <Typography variant="subtitle1" color="textSecondary">
          {pluralize(
            t('availabilityZonesCount', { count: row.zones?.length ?? 0 }),
            row.zones?.length
          )}
        </Typography>
      </Box>
    </StyledMenu>
  );
}

export const RegionSelection = ({ showErrorsAfterSubmit = true }: RegionSelectionProps) => {
  const [{ generalSettings }] = useContext(
    CreateUniverseContext
  ) as unknown as CreateUniverseContextMethods;

  const {
    getValues,
    setValue,
    watch,
    control,
    formState: { errors }
  } = useFormContext<ResilienceAndRegionsProps>();
  const { t } = useTranslation('translation', {
    keyPrefix: 'createUniverseV2.resilienceAndRegions'
  });

  const formMode = watch(RESILIENCE_FORM_MODE);
  const providers = getProviderConfigurations(generalSettings);
  const multiPlacement = isMultiProviderPlacement(generalSettings, formMode);
  const providerUUID = getPrimaryProvider(generalSettings)?.uuid;

  const regionsByProvider = watch(REGIONS_BY_PROVIDER) ?? {};
  const singularRegions = watch(REGIONS_FIELD);
  const regions = multiPlacement
    ? flattenRegionsByProvider(regionsByProvider)
    : singularRegions;
  const resilienceType = watch(RESILIENCE_TYPE);
  const faultToleranceType = watch('faultToleranceType');
  const isCreateUniverseFlow = window.location.pathname.includes('create-universe');
  const showExpertRegionIntro = isCreateUniverseFlow && formMode === ResilienceFormMode.EXPERT_MODE;

  const icon = useGetMapIcons({ type: MarkerType.REGION_SELECTED });
  const allowmultipleRegionsSelection =
    canSelectMultipleRegions(resilienceType) &&
    (faultToleranceType !== FaultToleranceType.NONE || formMode === ResilienceFormMode.EXPERT_MODE);

  const { isFetching, data: regionsList } = useQuery(
    [QUERY_KEY.getRegionsList, providerUUID],
    () => api.getRegionsList(providerUUID),
    {
      enabled: !!providerUUID && !multiPlacement,
      onSuccess: (list) => {
        if (isEmpty(getValues(REGIONS_FIELD)) && list.length === 1) {
          setValue(REGIONS_FIELD, [list[0]], { shouldValidate: true });
        }
      }
    }
  );

  const multiRegionQueries = useQueries(
    providers.map((provider) => ({
      queryKey: [QUERY_KEY.getRegionsList, provider.uuid],
      queryFn: () => api.getRegionsList(provider.uuid),
      enabled: multiPlacement && !!provider.uuid
    }))
  );

  const multiRegionsByProviderUuid = useMemo(() => {
    const map: Record<string, Region[]> = {};
    providers.forEach((provider, index) => {
      map[provider.uuid] = (multiRegionQueries[index]?.data as Region[] | undefined) ?? [];
    });
    return map;
  }, [providers, multiRegionQueries]);

  const allCatalogRegions = useMemo(() => {
    if (!multiPlacement) return regionsList ?? [];
    return providers.flatMap((provider) => multiRegionsByProviderUuid[provider.uuid] ?? []);
  }, [multiPlacement, regionsList, providers, multiRegionsByProviderUuid]);

  const mapCoordinates = useCallback(() => {
    const coordinates = regions?.map((region) => [region.latitude ?? [0], region.longitude ?? [0]]);
    if (coordinates?.length === 0) {
      return [
        [0, 0],
        [0, 0]
      ];
    }
    if (coordinates?.length === 1) {
      return [
        [coordinates[0][0], coordinates[0][1]],
        [0, 0]
      ];
    }
    return coordinates;
  }, [regions]);

  const setProviderRegions = (providerUuid: string, next: Region[]) => {
    const stamped = next.map((region) => stampRegionProviderUuid(region, providerUuid));
    const updated = {
      ...getValues(REGIONS_BY_PROVIDER),
      [providerUuid]: stamped
    };
    // Ensure every selected provider has a key so validation can require ≥1 each.
    for (const provider of providers) {
      if (!(provider.uuid in updated)) {
        updated[provider.uuid] = [];
      }
    }
    setValue(REGIONS_BY_PROVIDER, updated, { shouldValidate: true });
    setValue(REGIONS_FIELD, [], { shouldValidate: false });
  };

  const regionsFieldError =
    showErrorsAfterSubmit &&
    (multiPlacement
      ? errors[REGIONS_BY_PROVIDER]?.message ??
        (errors[REGIONS_BY_PROVIDER] as { message?: string } | undefined)?.message
      : errors[REGIONS_FIELD]?.message);

  return (
    <div
      style={{
        display: 'flex',
        flexDirection: 'column',
        border: '1px solid #D7DEE4',
        borderRadius: '8px'
      }}
    >
      <StyledHeader>{t('regions')}</StyledHeader>
      <StyledContent sx={{ gap: '24px' }}>
        {showExpertRegionIntro && (
          <Typography
            variant="body2"
            sx={{
              lineHeight: '16px',
              color: '#0B1117'
            }}
            data-testid="expert-region-intro"
          >
            {t('expertRegionIntro')}
          </Typography>
        )}
        {multiPlacement ? (
          <Box sx={{ display: 'flex', flexDirection: 'column', gap: '24px' }}>
            {providers.map((provider, index) => {
              const providerRegions = regionsByProvider[provider.uuid] ?? [];
              const catalog = multiRegionsByProviderUuid[provider.uuid] ?? [];
              const fetching = multiRegionQueries[index]?.isFetching;
              const cloudLabel =
                cloudOptionForCode(provider.code)?.label ??
                provider.code?.toUpperCase?.() ??
                provider.uuid;
              const emptyProvider =
                showErrorsAfterSubmit && providerRegions.length === 0;
              return (
                <Box
                  key={provider.uuid}
                  style={{ display: 'flex', flexDirection: 'column' }}
                  data-testid={`region-selection-provider-${provider.uuid}`}
                >
                  <YBLabel>
                    <CloudRegionLabel
                      code={provider.code}
                      text={t('providerRegions', { provider: cloudLabel })}
                    />
                  </YBLabel>
                  <YBAutoComplete
                    ybInputProps={{
                      placeholder: t('select'),
                      error: emptyProvider || !!regionsFieldError,
                      helperText: emptyProvider
                        ? t('errMsg.providerRegionRequired')
                        : index === providers.length - 1
                          ? regionsFieldError
                          : undefined,
                      dataTestId: `region-selection-autocomplete-${provider.uuid}`
                    }}
                    dataTestId={`region-selection-autocomplete-parent-${provider.uuid}`}
                    options={(catalog as unknown as Record<string, string>[]) ?? []}
                    getOptionLabel={getRegionOptionLabel}
                    filterSelectedOptions
                    isOptionEqualToValue={(option, value) => option.code === value.code}
                    renderOption={(props, row) => (
                      <RegionOptionMenuItem props={props} row={row} t={t} />
                    )}
                    sx={{ marginRight: '24px' }}
                    size="large"
                    loading={fetching}
                    multiple={allowmultipleRegionsSelection}
                    onChange={(_, option) => {
                      const value = uniqBy(
                        Array.isArray(option) ? option : option === null ? [] : [option],
                        'name'
                      ) as unknown as Region[];
                      setProviderRegions(provider.uuid, value);
                    }}
                    value={
                      allowmultipleRegionsSelection
                        ? (providerRegions as unknown as Record<string, string>[])
                        : isEmpty(providerRegions)
                          ? null
                          : (providerRegions[0] as unknown as Record<string, string>)
                    }
                  />
                </Box>
              );
            })}
          </Box>
        ) : (
          <Box style={{ display: 'flex', flexDirection: 'column' }}>
            <YBLabel>{t('regions')}</YBLabel>
            <YBAutoComplete
              ybInputProps={{
                placeholder: t('selectRegion'),
                error: showErrorsAfterSubmit && !!errors[REGIONS_FIELD],
                helperText: showErrorsAfterSubmit ? errors[REGIONS_FIELD]?.message : undefined,
                dataTestId: 'region-selection-autocomplete'
              }}
              dataTestId="region-selection-autocomplete-parent"
              options={(regionsList as unknown as Record<string, string>[]) ?? []}
              getOptionLabel={getRegionOptionLabel}
              filterSelectedOptions
              isOptionEqualToValue={(option, value) => option.code === value.code}
              renderOption={(props, row) => (
                <RegionOptionMenuItem props={props} row={row} t={t} />
              )}
              sx={{ marginRight: '24px' }}
              size="large"
              loading={isFetching}
              multiple={allowmultipleRegionsSelection}
              onChange={(_, option) => {
                const value = uniqBy(
                  Array.isArray(option) ? option : option === null ? [] : [option],
                  'name'
                );
                setValue(REGIONS_FIELD, value as unknown as Region[], {
                  shouldValidate: true
                });
              }}
              value={
                allowmultipleRegionsSelection
                  ? (singularRegions as unknown as Record<string, string>[])
                  : isEmpty(singularRegions)
                    ? null
                    : (singularRegions[0] as unknown as Record<string, string>)
              }
            />
          </Box>
        )}
        {resilienceType === ResilienceType.SINGLE_NODE && !multiPlacement && (
          <div
            style={{
              display: 'flex',
              flexDirection: 'row',
              gap: '8px',
              marginRight: '24px'
            }}
          >
            <AvailabilityZoneField
              dataTestId="single-node-availability-zone"
              name={SINGLE_AVAILABILITY_ZONE}
              label={t('singleNode.availabilityZone')}
              placeholder={t('singleNode.chooseAvailabilityZone')}
              sx={{ width: '500px' }}
            />
            <YBInputField
              dataTestId="single-node-count"
              name={NODE_COUNT}
              control={control}
              label={t('singleNode.node')}
              type="number"
              sx={{ width: '100px' }}
              disabled
            />
          </div>
        )}
        <Box sx={{ margin: '0px -25px -25px -25px' }}>
          <YBMaps
            mapHeight={345}
            dataTestId="yb-maps-region-selection"
            coordinates={mapCoordinates()}
            initialBounds={[[37.3688, -122.0363]]}
            mapContainerProps={{
              scrollWheelZoom: false,
              zoom: 2,
              center: [0, 0]
            }}
            showBoundaries={false}
          >
            {
              regions?.map((region: Region) => {
                return (
                  <YBMapMarker
                    key={`${region.providerUuid ?? ''}-${region.code}`}
                    position={[region.latitude, region.longitude]}
                    type={MarkerType.REGION_SELECTED}
                    tooltip={<>{region.name}</>}
                  />
                );
              }) as any
            }
            <>
              {(allCatalogRegions ?? [])
                .filter(
                  (region) =>
                    !regions?.some(
                      (selected) =>
                        selected.uuid === region.uuid || selected.code === region.code
                    )
                )
                .map((region) => (
                  <YBMapMarker
                    key={`catalog-${region.uuid ?? region.code}`}
                    position={[region.latitude, region.longitude]}
                    type={MarkerType.REGION_NOT_SELECTED}
                    tooltip={<>{region.name}</>}
                  />
                ))}
            </>
            {multiPlacement ? (
              <MapLegend
                mapLegendItems={providers.map((provider) => {
                  const cloudLabel =
                    cloudOptionForCode(provider.code)?.label ??
                    provider.code?.toUpperCase?.() ??
                    provider.uuid;
                  return (
                    <MapLegendItem
                      key={provider.uuid}
                      icon={<ProviderLegendIcon code={provider.code} />}
                      label={t('providerRegionLegend', { provider: cloudLabel })}
                    />
                  );
                })}
              />
            ) : regions?.length > 0 ? (
              <MapLegend
                mapLegendItems={[<MapLegendItem icon={<>{icon.normal}</>} label={'Region'} />]}
              />
            ) : (
              <span />
            )}
          </YBMaps>
        </Box>
      </StyledContent>
    </div>
  );
};
