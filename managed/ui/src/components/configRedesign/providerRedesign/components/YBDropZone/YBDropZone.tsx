/*
 * Copyright 2023 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
import React from 'react';
import { useDropzone } from 'react-dropzone';
import clsx from 'clsx';

import { YBButton } from '../../../../../redesign/components';

import styles from './YBDropZone.module.scss';

export type YBDropZoneProps = {
  actionButtonText?: string;
  className?: string;
  descriptionText?: string;
  dragOverText?: string;
  showHelpText?: boolean;
  dataTestId?: string;
  name?: string;
  noClick?: boolean;
} & (
  | {
      multipleFiles: false;
      value?: File;
      onChange?: (file: File) => void;
    }
  | {
      multipleFiles: true;
      value?: File[];
      onChange?: (file: File[]) => void;
    }
);

const DROP_ZONE_ICON = <i className={`fa fa-upload ${styles.dropZoneIcon}`} />;

export const YBDropZone = ({
  value,
  actionButtonText,
  className,
  descriptionText,
  dragOverText,
  multipleFiles,
  onChange,
  name,
  showHelpText = true,
  noClick = true,
  dataTestId
}: YBDropZoneProps) => {
  const onDropAccepted = (acceptedFiles: File[]) => {
    onChange && (multipleFiles === true ? onChange(acceptedFiles) : onChange(acceptedFiles[0]));
  };
  const { acceptedFiles, getRootProps, getInputProps, open, isDragActive } = useDropzone({
    onDropAccepted,
    multiple: multipleFiles,
    noClick: noClick,
    noKeyboard: true
  });

  const storedFiles = value ?? acceptedFiles;
  const dataTestIdPrefix = dataTestId && dataTestId !== '' ? dataTestId : 'YBDropZone';
  return storedFiles.length === 0 ? (
    <div
      className={clsx(styles.dropZoneContainer, isDragActive && styles.activeDrag, className)}
      {...getRootProps()}
    >
      <input name={name} {...getInputProps()} />
      {DROP_ZONE_ICON}
      <YBButton variant="secondary" onClick={open} data-testid={`${dataTestIdPrefix}-uploadButton`}>
        <i className="fa fa-upload" />
        {actionButtonText ?? 'Upload'}
      </YBButton>
      {showHelpText && (
        <div className={styles.subText}>
          {isDragActive
            ? dragOverText ?? 'Drop file here to upload.'
            : descriptionText ?? 'Drag a file into the box to upload.'}
        </div>
      )}
    </div>
  ) : (
    <div className={styles.updateFilesContainer}>
      <div className={styles.acceptedFilesMetadataContainer}>
        {Array.isArray(storedFiles)
          ? storedFiles.map((acceptedFile) => <li>{acceptedFile.name}</li>)
          : storedFiles.name}
      </div>
      <YBButton variant="secondary" onClick={open} data-testid={`${dataTestIdPrefix}-uploadButton`}>
        <i className="fa fa-upload" />
        {'Upload'}
      </YBButton>
    </div>
  );
};
