import os
import configparser
from jinja2 import Environment, FileSystemLoader
from collections import defaultdict
import copy


def convert_dotted_keys_to_nested(config_dict):
    ret = defaultdict(dict)
    for key in list(config_dict):
        if "." in key:
            value_to_update = dict(config_dict[key])
            for defkey in config_dict["DEFAULT"]:
                del value_to_update[defkey]
            keylist = key.split(".")
            d = config_dict
            for k in keylist[:-1]:
                d = d.setdefault(k, {})
            d[keylist[-1]] = value_to_update

    return config_dict


def parse_config(ynp_config):
    # Determine the directory containing the current script
    module_dir = os.path.dirname(__file__)

    # Setup Jinja2 environment and load template
    env = Environment(loader=FileSystemLoader(module_dir))
    template = env.get_template("config.j2")

    # Render the template with the configuration data
    output = template.render(ynp=ynp_config['ynp'], yba=ynp_config['yba'])

    # Determine the absolute path of the config.ini file
    config_file = os.path.join(module_dir, 'config.ini')

    # Write the rendered template to config.ini file
    try:
        with open(config_file, 'w') as ini_file:
            ini_file.write(output)
        print("INI file has been created successfully at:", config_file)
    except Exception as e:
        print("Error occurred while writing config.ini:", str(e))

    # Read the generated config.ini file and parse it into a dictionary
    config = configparser.ConfigParser()
    config_dict = {}
    try:
        config.read(config_file)
        config_dict = {section: dict(config.items(section)) for section in config.sections()}
        if config.defaults():
            config_dict['DEFAULT'] = dict(config.defaults())
    except Exception as e:
        print("Error occurred while parsing config.ini:", str(e))

    # Post-process config_dict to handle nested keys
    return convert_dotted_keys_to_nested(config_dict)
