import React, { FC, useMemo } from 'react';
import { useTranslation } from 'react-i18next';
import {
  MapLegend,
  MapLegendItem,
  MarkerType,
  mui,
  useGetMapIcons,
  YBMapMarker,
  YBMaps
} from '@yugabyte-ui-library/core';
import { Region } from '@app/redesign/features/universe/universe-form/utils/dto';

//icons
import Money from '../../../../../assets/money.svg';

const { styled } = mui;

export type ReviewMapMarker = {
  key: string;
  lat: number;
  lng: number;
  name: string;
  type: MarkerType;
};

export interface ReviewItem {
  name: string | React.ReactChild;
  /** `link` (default) matches create-universe/geo styling; `plain` is non-clickable body text. */
  nameVariant?: 'link' | 'plain';
  onNameClick?: () => void;
  badge?: React.ReactChild;
  attributes: {
    name: string | React.ReactChild;
    value: string | React.ReactChild;
  }[];
  dailyCost: string;
  monthlyCost: string;
  icon: React.ReactChild;
  totalMemory?: string | React.ReactChild;
  totalStorage?: string | React.ReactChild;
}

interface ReviewAndSummaryComponentProps {
  regions?: Region[];
  reviewItems: ReviewItem[];
  totalDailyCost: string;
  totalMonthlyCost: string;
  /** i18n key prefix for table headers and footer (e.g. createUniverseV2.reviewAndSummary). */
  summaryTranslationKeyPrefix?: string;
  /** Map legend label; defaults to "Region" for create-universe / geo flows. */
  mapLegendLabel?: string;
  /** Optional test id for the map container. */
  mapsDataTestId?: string;
  /** When set, replaces `regions` markers (e.g. primary + read-replica pins). */
  mapMarkers?: ReviewMapMarker[];
  /** When set, replaces the default single-item map legend. */
  mapLegendItems?: React.ReactNode[];
}

const StyledPanel = styled('div')(({ theme }) => ({
  borderRadius: '8px',
  border: `1px solid ${theme.palette.grey[200]}`
}));

const StyledUniverseName = styled('span')(({ theme }) => ({
  fontSize: '13px',
  fontWeight: 600,
  lineHeight: '20px',
  color: theme.palette.primary[600],
  textDecoration: 'underline',
  background: 'none',
  border: 'none',
  padding: 0,
  fontFamily: 'inherit'
}));

const StyledPlainName = styled('span')(({ theme }) => ({
  fontSize: '13px',
  fontWeight: 600,
  lineHeight: '20px',
  color: theme.palette.grey[900]
}));

const StyledAttrib = styled('div')(({ theme }) => ({
  fontSize: '13px',
  fontWeight: 400,
  lineHeight: '20px',
  color: theme.palette.grey[600],
  width: '120px'
}));
const StyledValue = styled('div')(({ theme }) => ({
  fontSize: '13px',
  fontWeight: 600,
  lineHeight: '20px',
  color: theme.palette.grey[900],
  textAlign: 'right',
  width: '80px'
}));

const StyledBoldValue = styled('div')(({ theme }) => ({
  fontSize: '13px',
  fontWeight: 700,
  lineHeight: '20px',
  color: theme.palette.grey[900]
}));

const StyledHead = styled('th')(() => ({
  textAlign: 'right',
  padding: '24px 24px 26px 24px',
  fontWeight: 400
}));

const StyledValueCell = styled('td')(() => ({
  gap: '8px',
  padding: '0px 24px 44px 24px',
  width: 'fit-content',
  verticalAlign: 'top',
  textAlign: 'right'
}));

const StyledTable = styled('table')(() => ({
  width: '720px',
  tableLayout: 'fixed'
}));

const StyledRoot = styled('div')(() => ({
  display: 'flex',
  gap: '16px',
  '& .yb-yb-MuiBox-root': {
    height: 'fit-content'
  }
}));

function toMapCoordinates(pins: { lat: number; lng: number }[]): [number, number][] {
  if (!pins.length) {
    return [
      [0, 0],
      [0, 0]
    ];
  }
  if (pins.length === 1) {
    return [
      [pins[0].lat, pins[0].lng],
      [pins[0].lat, pins[0].lng]
    ];
  }
  return pins.map((p) => [p.lat, p.lng]);
}

export const ReviewAndSummaryComponent: FC<ReviewAndSummaryComponentProps> = ({
  regions,
  reviewItems,
  totalDailyCost,
  totalMonthlyCost,
  summaryTranslationKeyPrefix = 'createUniverseV2.reviewAndSummary',
  mapLegendLabel = 'Region',
  mapsDataTestId = 'yb-maps-review-and-summary',
  mapMarkers,
  mapLegendItems
}) => {
  const { t } = useTranslation('translation', { keyPrefix: summaryTranslationKeyPrefix });
  const icon = useGetMapIcons({ type: MarkerType.REGION_SELECTED });

  const resolvedMarkers: ReviewMapMarker[] = useMemo(() => {
    if (mapMarkers) return mapMarkers;
    return (regions ?? [])
      .filter((region) => region.latitude != null && region.longitude != null)
      .map((region) => ({
        key: region.uuid || region.code,
        lat: region.latitude as number,
        lng: region.longitude as number,
        name: region.name,
        type: MarkerType.REGION_SELECTED
      }));
  }, [mapMarkers, regions]);

  const coordinates = useMemo(() => toMapCoordinates(resolvedMarkers), [resolvedMarkers]);

  const legendItems =
    mapLegendItems ??
    ([
      <MapLegendItem key="legend" icon={<>{icon.normal}</>} label={mapLegendLabel} />
    ] as React.ReactNode[]);

  return (
    <StyledRoot>
      <StyledPanel>
        <StyledTable>
          <thead>
            <tr>
              <StyledHead>&nbsp;</StyledHead>
              <StyledHead>{t('daily')}</StyledHead>
              <StyledHead>{t('monthly')}</StyledHead>
            </tr>
          </thead>
          <tbody>
            {reviewItems.map((item, index) => {
              const isLink = item.nameVariant !== 'plain';
              const NameEl = isLink ? StyledUniverseName : StyledPlainName;
              return (
                <tr key={index}>
                  <StyledValueCell
                    style={{ display: 'flex', gap: '8px', flexDirection: 'row', textAlign: 'left' }}
                  >
                    {item.icon}
                    <div>
                      <div style={{ display: 'flex', alignItems: 'center', gap: '8px' }}>
                        <NameEl
                          onClick={item.onNameClick}
                          style={item.onNameClick ? { cursor: 'pointer' } : undefined}
                          role={item.onNameClick ? 'button' : undefined}
                          tabIndex={item.onNameClick ? 0 : undefined}
                          onKeyDown={
                            item.onNameClick
                              ? (e) => {
                                  if (e.key === 'Enter' || e.key === ' ') {
                                    e.preventDefault();
                                    item.onNameClick?.();
                                  }
                                }
                              : undefined
                          }
                        >
                          {item.name}
                        </NameEl>
                        {item.badge}
                      </div>
                      {item.attributes.map((attr, idx) => (
                        <div
                          key={idx}
                          style={{
                            display: 'flex',
                            gap: '8px',
                            flexDirection: 'row',
                            justifyContent: 'space-between',
                            marginTop: idx === 0 ? '8px' : '0px'
                          }}
                        >
                          <StyledAttrib>{attr.name}</StyledAttrib>
                          <StyledValue>{attr.value}</StyledValue>
                        </div>
                      ))}
                    </div>
                  </StyledValueCell>
                  <StyledValueCell>${item.dailyCost}</StyledValueCell>
                  <StyledValueCell>${item.monthlyCost}</StyledValueCell>
                </tr>
              );
            })}
          </tbody>
          <tfoot style={{ borderTop: '1px solid #E9EEF2' }}>
            <tr>
              <StyledHead
                style={{ display: 'flex', gap: '8px', alignItems: 'center', marginRight: 'auto' }}
              >
                <Money />
                <StyledBoldValue>{t('universeTotal')}</StyledBoldValue>
              </StyledHead>
              <StyledHead style={{ fontWeight: 'bold' }}>${totalDailyCost}</StyledHead>
              <StyledHead style={{ fontWeight: 'bold' }}>${totalMonthlyCost}</StyledHead>
            </tr>
          </tfoot>
        </StyledTable>
      </StyledPanel>
      <YBMaps
        dataTestId={mapsDataTestId}
        mapHeight={360}
        coordinates={coordinates}
        initialBounds={undefined}
        mapWidth={360}
        mapContainerProps={{
          scrollWheelZoom: false,
          zoom: 1,
          center: [0, 0]
        }}
      >
        {
          resolvedMarkers.map((marker) => (
            <YBMapMarker
              key={marker.key}
              position={[marker.lat, marker.lng]}
              type={marker.type}
              tooltip={<>{marker.name}</>}
            />
          )) as any
        }
        <MapLegend mapLegendItems={legendItems as any} />
      </YBMaps>
    </StyledRoot>
  );
};
