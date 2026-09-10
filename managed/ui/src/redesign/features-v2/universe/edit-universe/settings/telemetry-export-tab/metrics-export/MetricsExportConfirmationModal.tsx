import { FC } from 'react';

import { YBModalProps } from '@app/redesign/components';
import {
  TelemetryConfigConfirmationModal,
  TelemetryConfigConfirmationOperation
} from '../../shared/TelemetryConfigConfirmationModal';
import { MetricsExportOperation } from './metricsExportHelpers';

interface MetricsExportConfirmationModalProps {
  operation: MetricsExportOperation;
  universeName: string;
  replicationFactor: number;
  isSubmitting: boolean;
  onSubmit: () => void;
  modalProps: YBModalProps;
}

const mapOperation = (operation: MetricsExportOperation): TelemetryConfigConfirmationOperation =>
  operation === 'create' ? 'enable' : 'edit';

export const MetricsExportConfirmationModal: FC<MetricsExportConfirmationModalProps> = ({
  operation,
  universeName,
  replicationFactor,
  isSubmitting,
  onSubmit,
  modalProps
}) => (
  <TelemetryConfigConfirmationModal
    configType="metricsExport"
    operation={mapOperation(operation)}
    universeName={universeName}
    replicationFactor={replicationFactor}
    isSubmitting={isSubmitting}
    onSubmit={onSubmit}
    modalProps={modalProps}
  />
);
