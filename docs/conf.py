# Configuration file for the Sphinx documentation builder.
#
# For the full list of built-in configuration values, see the documentation:
# https://www.sphinx-doc.org/en/master/usage/configuration.html

# -- Project information -----------------------------------------------------
# https://www.sphinx-doc.org/en/master/usage/configuration.html#project-information

project = 'pg_hint_plan'
copyright = '2012-2023, NIPPON TELEGRAPH AND TELEPHONE CORPORATION'
author = 'NIPPON TELEGRAPH AND TELEPHONE CORPORATION'

# -- General configuration ---------------------------------------------------
# https://www.sphinx-doc.org/en/master/usage/configuration.html#general-configuration

extensions = ['sphinx_rtd_theme', 'myst_parser']

templates_path = ['_templates']
exclude_patterns = ['_build', 'Thumbs.db', '.DS_Store']



# -- Options for HTML output -------------------------------------------------
# https://www.sphinx-doc.org/en/master/usage/configuration.html#options-for-html-output

html_theme = 'sphinx_rtd_theme'
#html_static_path = ['_static']

# Internationalization
# https://www.sphinx-doc.org/en/master/usage/advanced/intl.html
# https://docs.readthedocs.io/en/stable/guides/manage-translations-sphinx.html

locale_dirs = ['locale/']   # path is example but recommended.
gettext_compact = False     # optional.
gettext_uuid = True         # optional.
