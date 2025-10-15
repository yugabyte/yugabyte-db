import { useCallback, useContext } from 'react';
import { useQuery } from 'react-query';
import { isEmpty, uniqBy } from 'lodash';
import { useFormContext } from 'react-hook-form';
import { useTranslation } from 'react-i18next';
import {
  MarkerType,
  YBAutoComplete,
  YBMapMarker,
  YBMaps,
  MapLegend,
  MapLegendItem,
  useGetMapIcons,
  YBInputField
} from '@yugabyte-ui-library/core';
import { StyledHeader } from '../../components/DefaultComponents';
import { CreateUniverseContext, CreateUniverseContextMethods } from '../../CreateUniverseContext';
import { api, QUERY_KEY } from '../../../../../features/universe/universe-form/utils/api';
import { Region } from '../../../../../features/universe/universe-form/utils/dto';
import { FaultToleranceType, ResilienceAndRegionsProps, ResilienceType } from './dtos';
import {
  NODE_COUNT,
  REGIONS_FIELD,
  RESILIENCE_TYPE,
  SINGLE_AVAILABILITY_ZONE
} from '../../fields/FieldNames';
import { AvailabilityZoneField } from '../../fields/availability-zone/AvailabilityZoneField';
import { canSelectMultipleRegions } from '../../CreateUniverseUtils';

export const RegionSelection = () => {
  const [{ generalSettings }] = (useContext(
    CreateUniverseContext
  ) as unknown) as CreateUniverseContextMethods;

  const providerUUID = generalSettings?.providerConfiguration?.uuid;
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
  const { isFetching, data: regionsList } = useQuery(
    [QUERY_KEY.getRegionsList, providerUUID],
    () => api.getRegionsList(providerUUID),
    {
      enabled: !!providerUUID,
      onSuccess: (regions) => {
        if (isEmpty(getValues(REGIONS_FIELD)) && regions.length === 1) {
          setValue(REGIONS_FIELD, [regions[0]], { shouldValidate: true });
        }
      }
    }
  );

  const regions = watch(REGIONS_FIELD);
  const resilienceType = watch(RESILIENCE_TYPE);
  const faultToleranceType = watch('faultToleranceType');

  const icon = useGetMapIcons({ type: MarkerType.REGION_SELECTED });
  const allowmultipleRegionsSelection =
    canSelectMultipleRegions(resilienceType) && faultToleranceType !== FaultToleranceType.NONE;


  const mapCoordinates = useCallback(() => {
    const coordinates = regions?.map((region) => ([
      region.latitude ?? [0],
      region.longitude ?? [0]
    ]));
    if (coordinates?.length === 0) {
      return [[0, 0], [0, 0]];
    }
    if (coordinates?.length === 1) {
      return [[coordinates[0][0], coordinates[0][1]], [0, 0]];
    }
    return coordinates;
  }, [regions]);

  return (
    <div
      style={{
        display: 'flex',
        flexDirection: 'column',
        gap: '24px',
        border: '1px solid #D7DEE4',
        borderRadius: '8px'
      }}
    >
      <StyledHeader>{t('regions')}</StyledHeader>
      <YBAutoComplete
        ybInputProps={{
          placeholder: t('selectRegion'),
          error: !!errors[REGIONS_FIELD],
          helperText: errors[REGIONS_FIELD]?.message,
          dataTestId: 'region-selection-autocomplete',
          slotProps: {
            input: {
              style: {
                paddingLeft: '8px'
              }
            }
          }
        }}
        dataTestId="region-selection-autocomplete-parent"
        options={((regionsList as unknown) as Record<string, string>[]) ?? []}
        getOptionLabel={(r) => (typeof r === 'string' ? r : r.name ?? '')}
        sx={{ marginBottom: '24px', marginLeft: '24px', marginRight: '24px' }}
        loading={isFetching}
        multiple={allowmultipleRegionsSelection}
        onChange={(_, option) => {
          const value = uniqBy(
            Array.isArray(option) ? option : option === null ? [] : [option],
            'name'
          );
          setValue(REGIONS_FIELD, (value as unknown) as Region[], { shouldValidate: true });
        }}
        value={
          allowmultipleRegionsSelection
            ? ((regions as unknown) as Record<string, string>[])
            : isEmpty(regions)
              ? null
              : ((regions[0] as unknown) as Record<string, string>)
        }
      />
      {resilienceType === ResilienceType.SINGLE_NODE && (
        <div
          style={{
            display: 'flex',
            flexDirection: 'row',
            gap: '8px',
            marginBottom: '24px',
            marginLeft: '24px',
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
            sx={{ width: '100px', marginTop: '5px' }}
            disabled
          />
        </div>
      )}
      <YBMaps
        mapHeight={345}
        dataTestId='yb-maps-region-selection'
        coordinates={mapCoordinates()}
        initialBounds={[[37.3688, -122.0363]]}
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
        <>
          {(regionsList ?? [])
            .filter((region) => !regions?.some((r) => r.code === region.code))
            .map((region) => (
              <YBMapMarker
                key={region.code}
                position={[region.latitude, region.longitude]}
                type={regions.includes(region) ? MarkerType.REGION_SELECTED : MarkerType.REGION_NOT_SELECTED}
                tooltip={<>{region.name}</>}
              />
            ))}
        </>
        {regions?.length > 0 ? (
          <MapLegend
            mapLegendItems={[<MapLegendItem icon={<>{icon.normal}</>} label={'Region'} />]}
          />
        ) : <span />}
      </YBMaps>
    </div>
  );
};
