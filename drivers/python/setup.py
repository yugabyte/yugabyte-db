
from setuptools import setup, find_packages
from age import VERSION 

with open("README.md", "r", encoding='utf8') as fh:
    long_description = fh.read()

setup(
    name             = 'incubator-age',
    version          = VERSION.VERSION,
    description      = 'Python driver support for Apache AGE',
    long_description=long_description,
    long_description_content_type="text/markdown",
    author           = 'rhizome',
    author_email     = 'rhizome.ai@gmail.com',
    url              = 'https://github.com/apache/incubator-age',
    license          = 'Apache2.0',
    install_requires = [ 'psycopg2', 'antlr4-python3-runtime' ],
    packages         = ['age', 'age.gen'],
    keywords         = ['Graph Database', 'Apache AGE', 'PostgreSQL'],
    python_requires  = '>=3.9',
    # package_data     =  {},
    # zip_safe=False,
    classifiers      = [
        'Programming Language :: Python :: 3.9'
    ]
)