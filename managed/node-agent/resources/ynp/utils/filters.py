'''
Utility for defining the custom filter functions.
'''


def split_string(value, separator=','):
    if value.startswith('"') and value.endswith('"'):
        value = value[1:-1]
    return [item.strip() for item in value.split(separator)]
