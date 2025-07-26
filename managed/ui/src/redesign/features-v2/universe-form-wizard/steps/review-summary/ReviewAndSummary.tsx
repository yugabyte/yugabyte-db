import { forwardRef, useContext, useImperativeHandle } from 'react';
import { useTranslation } from 'react-i18next';
import {
  CreateUniverseContext,
  CreateUniverseContextMethods,
  StepsRef
} from '../../CreateUniverseContext';
import {
  MapLegend,
  MapLegendItem,
  MarkerType,
  useGetMapIcons,
  YBMapMarker,
  YBMaps
} from '@yugabyte-ui-library/core';
import { Region } from '../../../../features/universe/universe-form/utils/dto';
import { styled } from '@material-ui/core';
import { useCreateUniverse } from '../../../../../v2/api/universe/universe';
import { mapCreateUniversePayload } from '../../CreateUniverseUtils';

import { ReactComponent as UniverseIcon } from '../../../../assets/clusters.svg';
import { ReactComponent as Money } from '../../../../assets/money.svg';
import { toast } from 'react-toastify';

const StyledPanel = styled('div')(({ theme }) => ({
  borderRadius: '8px',
  border: `1px solid ${theme.palette.grey[200]}`,
  height: '300px'
}));

const StyledHeader = styled('span')(({ theme }) => ({
  fontSize: '13px',
  fontWeight: 400,
  lineHeight: '16px',
  color: theme.palette.grey[600],
  width: '120px',
  textAlign: 'right'
}));

const StyledCost = styled(StyledHeader)(({ theme }) => ({
  color: theme.palette.grey[900]
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
const StyledFooter = styled('div')(({ theme }) => ({
  display: 'flex',
  alignItems: 'flex-end',
  padding: '24px',
  borderTop: `1px solid ${theme.palette.grey[200]}`,
  justifyContent: 'space-between'
}));

const StyledBoldValue = styled('div')(({ theme }) => ({
  fontSize: '13px',
  fontWeight: 700,
  lineHeight: '20px',
  color: theme.palette.grey[900]
}));

export const ReviewAndSummary = forwardRef<StepsRef>((_, forwardRef) => {
  const [context, { moveToPreviousPage }] = (useContext(
    CreateUniverseContext
  ) as unknown) as CreateUniverseContextMethods;

  const { resilienceAndRegionsSettings } = context;

  const { t } = useTranslation('translation', { keyPrefix: 'createUniverseV2.reviewAndSummary' });

  const createUniverse = useCreateUniverse();

  useImperativeHandle(
    forwardRef,
    () => ({
      onNext: () => {
        const payload = mapCreateUniversePayload({ ...context });
        createUniverse
          .mutateAsync({
            data: payload
          })
          .then((resp) => {
            if (resp.resource_uuid) {
              // Navigate to the universe details page after creation
              window.location.href = `/universes/${resp.resource_uuid}`;
            }
          })
          .catch((error) => {
            console.error('Error creating universe:', error);
            toast.error(error);
            // Handle error appropriately, e.g., show a notification
          });
      },
      onPrev: () => {
        moveToPreviousPage();
      }
    }),
    []
  );

  const icon = useGetMapIcons({ type: MarkerType.REGION_SELECTED });

  return (
    <div style={{ display: 'flex', gap: '24px' }}>
      <StyledPanel style={{ width: '640px' }}>
        <div
          style={{
            padding: '24px 24px 0px 24px',
            display: 'flex',
            justifyContent: 'flex-end'
          }}
        >
          <StyledHeader>{t('daily')}</StyledHeader>
          <StyledHeader>{t('monthly')}</StyledHeader>
        </div>
        <div
          style={{
            padding: '24px',
            display: 'flex',
            justifyContent: 'flex-end'
          }}
        >
          <div style={{ display: 'flex', gap: '8px', flexDirection: 'row', marginRight: 'auto' }}>
            <UniverseIcon />
            <div style={{ display: 'flex', gap: '8px', flexDirection: 'column' }}>
              <StyledUniverseName>{t('universe')}</StyledUniverseName>
              <div
                style={{
                  display: 'flex',
                  gap: '8px',
                  flexDirection: 'row',
                  justifyContent: 'space-between'
                }}
              >
                <StyledAttrib>{t('nodes')}</StyledAttrib>
                <StyledValue>2</StyledValue>
              </div>
              <div
                style={{
                  display: 'flex',
                  gap: '8px',
                  flexDirection: 'row',
                  justifyContent: 'space-between'
                }}
              >
                <StyledAttrib>{t('totalCores')}</StyledAttrib>
                <StyledValue>144</StyledValue>
              </div>
              <div
                style={{
                  display: 'flex',
                  gap: '8px',
                  flexDirection: 'row',
                  justifyContent: 'space-between'
                }}
              >
                <StyledAttrib>{t('totalMemory')}</StyledAttrib>
                <StyledValue>
                  288 <span style={{ fontWeight: '200' }}>GB</span>
                </StyledValue>
              </div>
              <div
                style={{
                  display: 'flex',
                  gap: '8px',
                  flexDirection: 'row',
                  justifyContent: 'space-between'
                }}
              >
                <StyledAttrib>{t('totalStorage')}</StyledAttrib>
                <StyledValue>
                  300 <span style={{ fontWeight: '200' }}>GB</span>
                </StyledValue>
              </div>
            </div>
          </div>
          <StyledCost>$1234</StyledCost>
          <StyledCost>$1234</StyledCost>
        </div>
        <StyledFooter>
          <div style={{ display: 'flex', gap: '8px', alignItems: 'center', marginRight: 'auto' }}>
            <Money />
            <StyledBoldValue>{t('universeTotal')}</StyledBoldValue>
          </div>
          <StyledBoldValue style={{ width: '120px', textAlign: 'right' }}>$248.00</StyledBoldValue>
          <StyledBoldValue style={{ width: '120px', textAlign: 'right' }}>$248.00</StyledBoldValue>
        </StyledFooter>
      </StyledPanel>
      <YBMaps
        dataTestId='yb-maps-review-and-summary'
        mapHeight={360}
        coordinates={[
          [37.3688, -122.0363],
          [34.052235, -118.243683]
        ]}
        initialBounds={undefined}
        mapWidth={360}
        mapContainerProps={{
          scrollWheelZoom: false
        }}
      >
        {
          resilienceAndRegionsSettings?.regions?.map((region: Region) => {
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
    </div>
  );
});

ReviewAndSummary.displayName = 'ReviewAndSummary';
