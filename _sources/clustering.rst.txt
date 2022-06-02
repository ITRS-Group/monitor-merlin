Clustering
===========

Merlin allows clustering of multiple Naemon instances, to add redundancy and
loadbalancing to the Naemon monitoring infrastructure.

Merlin works in two main ways:

- Sends Naemon events, such as check results, between nodes over a TCP connection
- Keeps track, and syncs Naemon configuration across nodes over SSH

A single Merlin node, can have up to 65534 neighbours, allowing one to build
a large monitoring infrastructure.


Node types
-----------

Merlin has three different node types:

Peer
^^^^^

A peer is a redundant and loadbalanced node. A collection of peers, called a
peergroup, divides the check load equally. If you have three nodes in a peergroup,
each peer will execute 33% of the configured Naemon checks each. If one of the
nodes in a peergroup goes down, Merlin will redistribute the check load to the
two online peers, so they execute 50% of the checks each.

Peers will always run the same Naemon configuration.

Poller
^^^^^^^

A poller is responsible for checking host and services belonging to one or more
hostgroups. This makes it possible to put a Merlin node in a remote network. Each
poller can also be peered, ensuring multiple nodes are responsible for the set
of hostgroups.

A poller only has a subset of the full Naemon configuration (based on the
hostgroup assignment), and will never know of any objects not belonging to the
specified hostgroups.

It is possible to setup multiple pollergroups which are responsible for different
sets of hostgroups.

Master
^^^^^^^

A poller must connect to each node in the master peergroup. The master
peergroup, keeps track of the full Naemon configuration set. Masters will split
the Naemon configuration for pollers to use.

Cluster setup walkthrough
--------------------------

In this walk-through we'll go through creating a cluster with three nodes in
total. A master peergroup with two masters, and a single remote poller.

By the end we should have a node structure that looks like the following:

.. code-block:: none

    +----------+    +----------+
    | master01 |----| master02 |
    +----------+    +----------+
       |
       |
       | HOSTGROUP: pollergroup  +----------+
       = ------------------------| poller01 |
                                 +----------+

Preparation
^^^^^^^^^^^^

To begin with, prepare three machines (master01, master02, poller01), with
Naemon and Merlin installed as per the :doc:`installation instructions
</install>`. Make sure that there are no firewalls blocking port 22 (for SSH) &
15551 (Merlins default TCP port). Ensure that you had the root password for all
machines at hand and that SSH login on the root account is enabled.

Adding a peer
^^^^^^^^^^^^^^

We'll start by peering master01 and master02. To begin we need to ensure that
passwordless SSH connection is possible between the nodes. Merlin includes
a convenience script to setup and install SSH keys across the nodes, that we'll
use below.

.. code-block:: none

   [root@master01 ~]# mon sshkey push IP-OF-MASTER01

   [root@master02 ~]# mon sshkey push IP-OF-MASTER02

With the above, we should be able to SSH between both nodes, both as the
``root`` user and the ``naemon`` user.

Now we setup the peers in each nodes Merlin configuration. We'll use another
``mon`` tool adjust Merlins configuration file.

We'll start by adding master01 to master02's configuration.

Start by adding add master01 to the Merlin config on master02.

.. code-block:: none

   [root@master02 ~]# non node add master01 type=peer address=IP-OF-MASTER01

On master01 then, add master02 to the Merlin config, and push the Naemon
configuration to master02. We always push configuration as the ``naemon`` user.
Finally, restart Naemon & Merlin.

.. code-block:: none

   [root@master01 ~]# mon node add master02 type=peer address=IP-OF-MASTER02
   [root@master01 ~]# su naemon -c "mon oconf push master02"
   [root@master01 ~]# mon restart


At this point we should have a healthy cluster, and we can use a few more
``mon`` tools to look at our cluster state. These two nodes are now
loadbalanced sharing the check executions equally.

.. code-block:: none

   [root@master01 ~]# mon node status
   Total checks (host / service): 4 / 21

   #00 0/1:1 local ipc: ACTIVE - 0.000s latency
   Uptime: 15m 47s. Connected: 15m 48s. Last alive: 8s ago
   Host checks (handled, expired, total)   : 2, 0, 4 (50.00% : 50.00%)
   Service checks (handled, expired, total): 11, 0, 21 (52.38% : 52.38%)

   #01 1/1:1 peer master02: ACTIVE - 0.000s latency - (UNENCRYPTED)
   Uptime: 15m 48s. Connected: 15m 48s. Last alive: 8s ago
   Host checks (handled, expired, total)   : 2, 0, 4 (50.00% : 50.00%)
   Service checks (handled, expired, total): 10, 0, 21 (47.62% : 47.62%)

   [root@master01 ~]# mon node tree
    +-----+    +----------+
    | ipc |----| master02 |
    +-----+    +----------+

Adding a poller
^^^^^^^^^^^^^^^^

Now that we have two peers in the master peergroup, we can add a poller. We must
first decide which hostgroup(s) the poller should be responsible for. Before
getting started with the setup, ensure that the hostgroup has already been added
to the masters Naemon configuration. In our example we'll use a hostgroup called
``pollergroup``.

SSH connection must be established to both masters, so we start by adding SSH
keys.

.. code-block:: none

   [root@poller01 ~]# mon sshkey push IP-OF-MASTER01
   [root@poller01 ~]# mon sshkey push IP-OF-MASTER02
   [root@master01 ~]# mon sshkey push IP-OF-POLLER01

   [root@master02 ~]# mon sshkey push IP-OF-POLLER02


With the above we have ensured that the poller can SSH to both masters, and
that both masters can SSH to the poller. We now add the poller to both masters.
Afterwards we restart both masters. This ensures the Merlin will prepare the
a subset of the Naemon configuration for poller01.

.. code-block:: none

   [root@master01 ~]# mon node add poller01 type=poller hostgroup=pollergroup address=IP-OF-POLLER01
   [root@master01 ~]# mon restart
   
   [root@master02 ~]# mon node add poller01 type=poller hostgroup=pollergroup address=IP-OF-POLLER01
   [root@master02 ~]# mon restart

On the poller, we now need to add both masters to the Merlin configuration.

.. code-block:: none

   [root@poller01 ~]# mon node add master01 type=master address=IP-OF-MASTER01
   [root@poller01 ~]# mon node add master02 type=master address=IP-OF-MASTER02

Finally, on one of the masters, do the initial configuration push to the poller
manually.

.. code-block:: none

   [root@master01 ~]# su naemon -c "mon oconf push poller01"

We have now added a poller, and we use the ``mon`` tools again to view the state
of our cluster.

.. code-block:: none

   [root@master01 ~]# mon node tree
    +-----+    +----------+
    | ipc |----| master02 |
    +-----+    +----------+
       |
       |
       | HOSTGROUP: pollergroup  +----------+
       = ------------------------| poller01 |
                                 +----------+


   [root@master01 ~]# mon node status
   Total checks (host / service): 5 / 29

   #00 0/1:1 local ipc: ACTIVE - 0.000s latency
   Uptime: 12m 24s. Connected: 12m 25s. Last alive: 2s ago
   Host checks (handled, expired, total)   : 3, 0, 4 (75.00% : 60.00%)
   Service checks (handled, expired, total): 11, 0, 21 (52.38% : 37.93%)

   #01 1/1:1 peer master02: ACTIVE - 0.000s latency - (UNENCRYPTED)
   Uptime: 12m 22s. Connected: 12m 17s. Last alive: 2s ago
   Host checks (handled, expired, total)   : 1, 0, 4 (25.00% : 20.00%)
   Service checks (handled, expired, total): 10, 0, 21 (47.62% : 34.48%)

   #02 0/0:0 poller poller01: ACTIVE - 0.000s latency - (UNENCRYPTED)
   Uptime: 3m 39s. Connected: 3m 39s. Last alive: 4s ago
   Host checks (handled, expired, total)   : 1, 0, 1 (100.00% : 20.00%)
   Service checks (handled, expired, total): 8, 0, 8 (100.00% : 27.59%)

.. toctree::
   :maxdepth: 1
