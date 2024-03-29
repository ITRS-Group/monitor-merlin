Installing
===========

Dependencies & Requirements
----------------------------

Merlin is currently tested on CentOS 7 & CentOS 8. Merlin requires Naemon, and
the Naemon binary must be installed at ``/usr/bin/naemon``.

From package repositories
--------------------------

Installing via repositories hosted at openSUSE Build Service is the recommended
approach. This makes updating easy using the OS provided package manager.

Prior to installing we need to ensure that the EPEL release is included, as
dependencies from EPEL are required. Hereafter we can install the relevant
`.repo` files, both for Naemon and Merlin. Finally we can install merlin using
the package manager. 

CentOS 7
^^^^^^^^^

.. code-block:: none

   yum install -y epel-release
   curl -s https://download.opensuse.org/repositories/home:/naemon/CentOS_7/home:naemon.repo >> /etc/yum.repos.d/naemon-stable.repo
   curl -s https://download.opensuse.org/repositories/home:/itrs-op5/CentOS_7/home:itrs-op5.repo >> /etc/yum.repos.d/itrs-op5.repo
   yum install merlin
   systemctl enable naemon && systemctl start naemon

CentOS 8
^^^^^^^^^

.. code-block:: none

   dnf install -y epel-release
   curl -s https://download.opensuse.org/repositories/home:/naemon/CentOS_8_Stream/home:naemon.repo >> /etc/yum.repos.d/naemon-stable.repo
   curl -s https://download.opensuse.org/repositories/home:/itrs-op5/CentOS_8_Stream/home:itrs-op5.repo >> /etc/yum.repos.d/itrs-op5.repo
   dnf install merlin
   systemctl enable naemon && systemctl start naemon

Post install steps
^^^^^^^^^^^^^^^^^^^
Clustering relies on having a running SSH server. Enable the SSH server by:

.. code-block:: none

   systemctl enable sshd
   systemctl start sshd

With Thruk
^^^^^^^^^^^

If you wish to install the Thruk UI, add ``naemon`` to the dnf/yum install
commands above.

Development snapshot
^^^^^^^^^^^^^^^^^^^^^

Development snapshots are built anytime there is a merge to master. If you need
a feature not yet included for release, you can install the development snapshot
by replacing the repo definitions above with either corresponding one below.

**CentOS 7:** 

.. code-block:: none

   https://download.opensuse.org/repositories/home:/itrs-op5:/master/CentOS_7/home:itrs-op5:master.repo

**CentOS 8:**

.. code-block:: none

   https://download.opensuse.org/repositories/home:/itrs-op5:/master/CentOS_8_Stream/home:itrs-op5:master.repo

From .rpm files
---------------------------------

Releases are also published on Github_. Each release contain a set of tar.xz
files containing the built rpm sources.

Extract the achieve and install the complete all RPMs in a single command using
your package manager (yum/dnf). Prior to installing ensure that Naemon is either
installed or the Naemon repository has been added.

Slim packages
---------------------------------

Currently two sets of packages are being built. *Normal* packages and *slim*
packages which are denoted ``-slim``. The slim packages are intended for use
in containers, for example with the Merlin container poller.

Some of the differences with the slim packages are noted below:

- No systemd service files
- No database is installed
- Includes container specific healthchecks
- Includes the ``cluster_tools`` script which helps registering with master nodes

Compiling from source
----------------------

**Needs review, this section is potentially out of date and needs updating** 

Requirements
^^^^^^^^^^^^^

Merlin requires Naemon, including its development headers for building.
Currently, it requires version >= 1.2.4.

Other general build requirements: gcc, autoconf, automake, glib-2-devel,
check-devel, libdbi-devel, libtool, naemon-devel, gperf

For running Merlin with its default configuration, you'll need to have
libdbi-dbd-mysql and all of its dependencies (generally libdbi-drivers, libdbi
and mysql-libs) installed.

You will of course also need an sql database supported by libdbi (refer to the
libdbi documentation for further information about supported databases), as
well as a Naemon installation that the Merlin module can plug in to. The import
script is currently limited to MySQL only, so that's currently the only
database supported.

The install script requires sql administration privileges in order to create
the database that merlin will populate for you.

GNU sed 4.0.9 or better is required for the install script to be able to modify
your naemon configuration files.

For redundant/distributed installation you also need sudo rights for naemon
user to be able to reload the configuration on all your nodes in the cluster.
The command that needs to be executed 'sudo mon restart'.


Building and installation
^^^^^^^^^^^^^^^^^^^^^^^^^^

Building is a standard autotools flair. Checkout with git, or download the 
release tarball and run

.. code-block:: none

   ./autogen.sh
   make
   sudo make install

The install will by default try to install merlin's database. The configure
script provides a way to configure database name and users, as well as a way to
prevent merlin from doing this automatically, in which case you need to run the
install-merlin.sh script manually. Run ./configure --help for more information.

Merlin will drop a naemon configuration file into the directory naemon's
configuration lies. You can include it in your main naemon config file with
``include_file=merlin.cfg`` or by putting the config file in an already included
directory with the ``--with-naemon-config-dir`` argument to the configure script.

.. _Github: https://github.com/ITRS-Group/monitor-merlin/releases

.. toctree::
   :maxdepth: 1 
