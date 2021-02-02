import React, { useEffect, useState } from 'react';
import PropTypes from 'prop-types';
import '../app/stylesheets/Toaster.scss';


const Toaster = (props) => {
  const { toast, dismissTime, onDismissClick} = props;
  const [show, setShow] = useState(true);

  useEffect(() => {
    const timeout = dismissTime || 5000;
    setTimeout(() => setShow(false), timeout)
  }, [show])

  const deleteToast = () => {
    setShow(!show)
  }
  return (
    <div>
      {show &&
        <div className={`toaster-container ${toast.position}`}>
          <div className={`toaster toast ${toast.position} toast-${toast.type}`}>
            <button onClick={onDismissClick}>X</button>
          <div className="toaster-icon">
            <i className={`${toast.icon}`}/>
          </div>
            <div>
              <p className="toaster-title">{toast.type}</p>
              <p className="toaster-message">
                {toast.description}
              </p>
            </div>
          </div>
        </div>
      }
      {/* <button onClick={deleteToast}>Show Toast</button> */}
    </div>
  )
}

Toaster.propTypes = {
  toast: PropTypes.object.isRequired,
  dismissTime: PropTypes.number
};

export default Toaster;
