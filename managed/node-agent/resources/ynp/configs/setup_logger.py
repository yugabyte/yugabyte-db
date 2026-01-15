import logging
import logging.config
import os
import pwd
import time


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

    log_path = os.path.join(log_dir, log_file)
    logging_config = {
        'version': 1,
        'disable_existing_loggers': False,
        'formatters': {
            'standard': {
                'format': '%(asctime)s - %(name)s - %(levelname)s - %(message)s',
                'datefmt': '%Y-%m-%dT%H:%M:%SZ',  # ISO-like UTC format
                '()': 'logging.Formatter',        # Explicit formatter class
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
                'filename': log_path,
                'mode': 'a',
            },
        },
        'loggers': {
            '': {  # root logger
                'handlers': ['console', 'file'],
                'level': 'DEBUG',
                'propagate': True,
            }
        }
    }
    logging.config.dictConfig(logging_config)
    # Patch all formatters to use UTC time
    for handler in logging.getLogger().handlers:
        if isinstance(handler.formatter, logging.Formatter):
            handler.formatter.converter = time.gmtime

    # Set file permissions (644)
    os.chmod(log_path, 0o644)
    current_user_id = os.getuid()
    # Set ownership to original user if script was run with sudo
    if 'SUDO_USER' in os.environ:
        original_user = os.environ['SUDO_USER']
    else:
        original_user = os.getlogin()
    user_info = pwd.getpwnam(original_user)
    uid = user_info.pw_uid
    gid = user_info.pw_gid
    if current_user_id == 0 and current_user_id != uid:
        # Change ownership only if running as root and yb_user is different from current user.
        os.chown(log_dir, uid, gid)
        os.chown(log_path, uid, gid)
    logger = logging.getLogger()
    logger.info("Logging setup complete in UTC timezone")
