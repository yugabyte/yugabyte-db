.. _installation:

Installation
============

Requirements
------------

- PostgreSQL 9.2+

Packages
--------

Hypopg is available as a package on some GNU/Linux distributions:

- RHEL/Centos

  HypoPG is available as a package using `the PGDG packages
  <https://yum.postgresql.org>`_.

  Once the PGDG repository is setup, you just need to install the package.  As
  root:

  .. code-block:: bash

    yum install hypopg

- Archlinux

  Hypopg is available on the `AUR repository
  <https://aur.archlinux.org/packages/hypopg-git/>`_.

  If you have **yaourt** setup, you can simply install the `hypopg-git` package
  with the following command:

  .. code-block:: bash

    yaourt -S hypopg-git

  Otherwise, look at the `official documentation
  <https://wiki.archlinux.org/index.php/Arch_User_Repository#Installing_packages>`_
  to manually install the package.

  .. note::

    Installing this package will use the current development version.  If you
    want to install a specific version, please see the
    :ref:`install_from_source` section.

.. _install_from_source:

Installation from sources
-------------------------

To install HypoPG from sources, you need the following extra requirements:

- PostgreSQL development packages

.. note::

  On Debian/Ubuntu systems, the development packages are named
  `postgresql-server-dev-X`, X being the major version.

  On RHEL/Centos systems, the development packages are named
  `postgresqlX-devel`, X being the major version.

- A C compiler and `make`
- `unzip`
- optionally the `wget` tool
- a user with `sudo` privilege, or a root access

.. note::

  If you don't have `sudo` or if you user isn't authorized to issue command as
  root, you should do all the following commands as **root**.

First, you need to download HypoPG source code.  If you want the development
version, you can download it `from here
<https://github.com/HypoPG/hypopg/archive/master.zip>`_, or via command line:

.. code-block:: bash

  wget https://github.com/HypoPG/hypopg/archive/master.zip

If you want a specific version, you can chose `the version you want here
<https://github.com/HypoPG/hypopg/releases>`_ and follow the related download
link.  For instance, if you want to install the version 1.0.0, you can download
it from the command line with the following command:

.. code-block:: bash

  wget https://github.com/HypoPG/hypopg/archive/1.0.0.zip

Then, you need to extract the downloaded archive with `unzip` and go to the
extracted directory.  For instance, if you downloaded the latest development
version:

.. code-block:: bash

  unzip master.zip
  cd hypopg-master

You can now compile and install HypoPG.  Simply run:

.. code-block:: bash

  make
  sudo make install

.. note::

  If you were doing these commands as **root**, you don't need to use sudo.
  The last command should therefore be:

  .. code-block:: bash

    make install

If no errors occured, HypoPG is now available!  If you need help on how to use
it, please refer to the :ref:`usage` section.
