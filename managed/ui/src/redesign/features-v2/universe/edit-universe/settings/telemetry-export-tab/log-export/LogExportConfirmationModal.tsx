import { FC } from 'react';

import { YBModalProps } from '@app/redesign/components';
import {
  TelemetryConfigConfirmationModal,
  TelemetryConfigConfirmationOperation,
  TelemetryConfigType
} from '../../shared/TelemetryConfigConfirmationModal';
import { LogExportOperation, LogExportType } from './logExportHelpers';

interface LogExportConfirmationModalProps {
  logExportType: LogExportType;
  operation: LogExportOperation;
  universeName: string;
  replicationFactor: number;
  isSubmitting: boolean;
  onSubmit: () => void;
  modalProps: YBModalProps;
}

const mapOperation = (operation: LogExportOperation): TelemetryConfigConfirmationOperation =>
  operation === 'create' ? 'enable' : 'edit';

const mapConfigType = (logExportType: LogExportType): TelemetryConfigType =>
  logExportType === 'query' ? 'queryLogExport' : 'auditLogExport';

export const LogExportConfirmationModal: FC<LogExportConfirmationModalProps> = ({
  logExportType,
  operation,
  universeName,
  replicationFactor,
  isSubmitting,
  onSubmit,
  modalProps
}) => (
  <TelemetryConfigConfirmationModal
    configType={mapConfigType(logExportType)}
    operation={mapOperation(operation)}
    universeName={universeName}
    replicationFactor={replicationFactor}
    isSubmitting={isSubmitting}
    onSubmit={onSubmit}
    modalProps={modalProps}
  />
);
