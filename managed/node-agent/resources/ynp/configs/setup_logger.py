import logging
import logging.config
import os
import pwd


def setup_logger(config):
    key = next(iter(config), None)
    log_file = "app.log"
    log_dir = "./logs"
    if key is not None:
        context = config[key]
        log_file = context.get('logfile')
        log_dir = context.get("logdir")

    # Create log directory if it doesn't exist
    # Ensure the log directory has the correct permissions (755)
    # We need execute set on directory for traversal, don't need it on all files.
    os.makedirs(log_dir, mode=0o755, exist_ok=True)

    logging_config = {
        'version': 1,
        'disable_existing_loggers': False,
        'formatters': {
            'standard': {
                'format': '%(asctime)s - %(name)s - %(levelname)s - %(message)s'
            },
        },
        'handlers': {
            'console': {
                'class': 'logging.StreamHandler',
                'formatter': 'standard',
                'level': 'DEBUG',
            },
            'file': {
                'class': 'logging.FileHandler',
                'formatter': 'standard',
                'level': 'DEBUG',
                'filename': os.path.join(log_dir, log_file),
                'mode': 'a',
            },
        },
        'loggers': {
            '': {  # root logger
                'handlers': ['console', 'file'],
                'level': 'DEBUG',
                'propagate': True
            }
        }
    }
    logging.config.dictConfig(logging_config)
    # Ensure the log file has the correct permissions (644)
    # No execute set, root can read/write, group can read, others can read
    os.chmod(os.path.join(log_dir, log_file), 0o644)
    logger = logging.getLogger()
    logger.info("Logging Setup Done")

    if 'SUDO_USER' in os.environ:
        original_user = os.environ['SUDO_USER']
    else:
        original_user = os.getlogin()
    user_info = pwd.getpwnam(original_user)
    uid = user_info.pw_uid
    gid = user_info.pw_gid
    os.chown(log_dir, uid, gid)
    os.chown(os.path.join(log_dir, log_file), uid, gid)
