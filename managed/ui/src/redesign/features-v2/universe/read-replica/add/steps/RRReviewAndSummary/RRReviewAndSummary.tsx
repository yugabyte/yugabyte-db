import { forwardRef, useContext, useImperativeHandle } from 'react';
import { useTranslation } from 'react-i18next';
import { mui } from '@yugabyte-ui-library/core';
import { RRBreadCrumbs } from '../../ReadReplicaBreadCrumbs';
import { StepsRef, AddRRContext, AddRRContextMethods } from '../../AddReadReplicaContext';

const { Box } = mui;

export const RRReviewAndSummary = forwardRef<StepsRef>((_, forwardRef) => {
  const [{}, { moveToNextPage, moveToPreviousPage }] = (useContext(
    AddRRContext
  ) as unknown) as AddRRContextMethods;

  const { t } = useTranslation('translation', { keyPrefix: 'readReplica.addRR' });

  useImperativeHandle(
    forwardRef,
    () => ({
      onNext: () => {
        return Promise.resolve();
        //create read replica
      },
      onPrev: () => {
        moveToPreviousPage();
      }
    }),
    []
  );

  return (
    <Box sx={{ display: 'flex', flexDirection: 'column', gap: '24px' }}>
      <RRBreadCrumbs groupTitle={t('review')} subTitle={t('summaryAndCost')} />
    </Box>
  );
});
