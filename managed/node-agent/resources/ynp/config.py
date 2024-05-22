import os
import configparser

module_dir = os.path.dirname(__file__)


def parse_config(config_file='config.ini'):
    config = configparser.ConfigParser()
    config_file = os.path.join(module_dir, config_file)
    config.read(config_file)
    config_dict = {section: dict(config.items(section)) for section in config.sections()}
    if config.defaults():
        config_dict['DEFAULT'] = dict(config.defaults())
    return config_dict
