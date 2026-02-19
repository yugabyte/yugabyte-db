import React, { FC } from 'react';
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

export interface ReviewItem {
  name: string | React.ReactChild;
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
  textDecoration: 'underline'
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

export const ReviewAndSummaryComponent: FC<ReviewAndSummaryComponentProps> = ({
  regions,
  reviewItems,
  totalDailyCost,
  totalMonthlyCost
}) => {
  const { t } = useTranslation('translation', { keyPrefix: 'createUniverseV2.reviewAndSummary' });
  const icon = useGetMapIcons({ type: MarkerType.REGION_SELECTED });

  return (
    <StyledRoot>
      <StyledPanel>
        <StyledTable>
          <thead>
            <tr>
              <StyledHead>&nbsp;</StyledHead>
              <StyledHead>Daily</StyledHead>
              <StyledHead>Monthly</StyledHead>
            </tr>
          </thead>
          <tbody>
            {reviewItems.map((item, index) => (
              <tr key={index}>
                <StyledValueCell
                  style={{ display: 'flex', gap: '8px', flexDirection: 'row', textAlign: 'left' }}
                >
                  {item.icon}
                  <div>
                    <StyledUniverseName>{item.name}</StyledUniverseName>
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
            ))}
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
        dataTestId="yb-maps-review-and-summary"
        mapHeight={360}
        coordinates={[
          [0, 0],
          [0, 0]
        ]}
        initialBounds={undefined}
        mapWidth={360}
        mapContainerProps={{
          scrollWheelZoom: false,
          zoom: 1,
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
        <MapLegend
          mapLegendItems={[<MapLegendItem icon={<>{icon.normal}</>} label={'Region'} />]}
        />
      </YBMaps>
    </StyledRoot>
  );
};
