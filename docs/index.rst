.. Merlin documentation master file, created by
   sphinx-quickstart on Sun Oct 31 10:51:03 2021.
   You can adapt this file completely to your liking, but it should at least
   contain the root `toctree` directive.

Introduction
=============

The Merlin project, or Module for Effortless Redundancy and Loadbalancing In
Naemon, is a Naemon module built to create an easy way to set up distributed
Naemon installations, allowing Naemon processes to exchange information. This
allows Naemon to scale past a single installation to handle a bigger monitoring
workload.

Merlin can also save state changes to a database, which can be
used for reporting purposes.

Brief description of the Merlin project
========================================

Merlin consists of:

**merlin-mod:** Responsible for jacking into the NEBCALLBACK_* calls and send them
to a socket (remote Merlin and merlind). If the socket is not available the
events are written to a backlog and sent when the socket is available again.

**merlind:** The Merlin deamon listens to the socket that merlin-mod writes to and
sends all state changes to a database of your choice (using libdbi).

**merlin database:** This is a database that contains state changes.

**mon scripts:** A bunch of utility scripts utilized to add new nodes to the system,
and other useful things.

Table of Contents
------------------

.. toctree::
   :maxdepth: 1
   :caption: About

   self
   changelog.md

.. toctree::
   :caption: Using merlin
   :maxdepth: 1

   install
   clustering
   configuration
   encryption
   container-poller
