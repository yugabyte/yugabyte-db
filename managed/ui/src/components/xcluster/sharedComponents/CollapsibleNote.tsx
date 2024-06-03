import { Typography } from '@material-ui/core';
import { ReactNode, useState } from 'react';
import { Collapse } from 'react-bootstrap';

import { YBBanner, YBBannerVariant } from '../../common/descriptors';

import styles from './CollapsibleNote.module.scss';

interface CollapsibleNoteProps {
  noteContent: ReactNode;
  expandContent: ReactNode;
}

export const CollapsibleNote = ({ noteContent, expandContent }: CollapsibleNoteProps) => {
  const [isNoteDetailsExpanded, setIsNoteDetailsExpanded] = useState(false);
  return (
    <YBBanner variant={YBBannerVariant.INFO}>
      <Typography variant="body2">
        {noteContent}
        <button
          className={styles.toggleNoteDetailsBtn}
          onClick={() => {
            setIsNoteDetailsExpanded(!isNoteDetailsExpanded);
          }}
          type="button"
        >
          {isNoteDetailsExpanded ? (
            <span>
              Less details <i className="fa fa-caret-up" aria-hidden="true" />
            </span>
          ) : (
            <span>
              More details <i className="fa fa-caret-down" aria-hidden="true" />
            </span>
          )}
        </button>
        <Collapse in={isNoteDetailsExpanded}>
          <div className={styles.expandContent}>{expandContent}</div>
        </Collapse>
      </Typography>
    </YBBanner>
  );
};
