import React, { useEffect, useState } from 'react';
import PropTypes from 'prop-types';
import '../app/stylesheets/Toaster.scss';


const Toaster = (props) => {
  const { toast, onDismissClick} = props;

  const icon = toast.type === 'success' ? 'fa-check-circle' : 'fa-warning';
  const position = toast.position || 'bottom-right';

  useEffect(() => {
    const timeout = toast.dismissTime || 5000;
    setTimeout(() => onDismissClick(), timeout)
  }, []);

  return (
    <div>
      <div className={`toaster-container ${position}`}>
        <div className={`toaster toast ${position} toast-${toast.type}`}>
          <button onClick={onDismissClick}>X</button>
          <div className="toaster-icon">
            <i className={`fa fa-3x ${icon}`} />
          </div>
          <div>
            <p className="toaster-title">{toast.type}</p>
            <p title={toast.description} className="toaster-message">
              {toast.description}
            </p>
          </div>
        </div>
      </div>
    </div>
  )
}

Toaster.propTypes = {
  toast: PropTypes.object.isRequired,
  dismissTime: PropTypes.number
};

export default Toaster;
