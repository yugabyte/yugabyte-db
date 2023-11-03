import { ReactNode, useState } from 'react';
import { Collapse } from 'react-bootstrap';

import styles from './CollapsibleNote.module.scss';

interface CollapsibleNoteProps {
  noteContent: ReactNode;
  expandContent: ReactNode;
}

export const CollapsibleNote = ({ noteContent, expandContent }: CollapsibleNoteProps) => {
  const [isNoteDetailsExpanded, setIsNoteDetailsExpanded] = useState(false);
  return (
    <div className={styles.note}>
      <i className="fa fa-exclamation-circle" aria-hidden="true" />
      <div className={styles.noteText}>
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
      </div>
    </div>
  );
};
