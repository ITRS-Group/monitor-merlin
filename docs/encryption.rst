Encrypted communication
========================

Merlin supports encrypting the packets transmitted over the Merlin TCP protocol.
Merlin uses libsodiums crypo_box. For more information on the encryption please
refer to their `documentation
<https://doc.libsodium.org/public-key_cryptography/authenticated_encryption>`_.

Note that the Merlin packets body is encrypted, the header remain unencrypted.


Configuring encryption
-----------------------

In this example we'll set up encryption between two peers, *peer01*, and *peer02*.

Generating encryption keys
^^^^^^^^^^^^^^^^^^^^^^^^^^^

To setup encryption, we first need to generate a encryption key pair consisting
of a private key and a public key.

To do so we can make use of the ``mon merlinkey`` command.

.. code-block:: none

   [root@peer01 ~]# mon merlinkey generate --write
   [root@peer02 ~]# mon merlinkey generate --write

The above command does the following:

1. Generates an encryption key pair and saves it at ``/etc/merlin/`` by default
2. Adds the ``ipc_privatekey`` setting to ``merlin.conf``

You can omit ``--write`` flag if you do not write the change to ``merlin.conf``.

We now need to copy the public keys, to the other node. Note the private key
should never be shared! We can use ``scp`` to copy the relevant public key.

.. code-block:: none

   [root@peer01 ~]# scp /etc/merlin/key.pub peer02:/etc/merlin/peer01.pub
   [root@peer02 ~]# scp -a /etc/merlin/key.pub peer01:/etc/merlin/peer02.pub


Adjusting configuration
^^^^^^^^^^^^^^^^^^^^^^^^

To enable encryption, we must adjust the node configuration in ``merlin.conf``,
on both *peer01* and *peer02*. Below is an example of how the node config
should look on the two nodes.

``merlin.conf`` on *peer01*

.. code-block:: none

   peer peer02 {
        address = IP_ADDR
        port = 15551
        encrypted = 1
        publickey = /etc/merlin/peer02.pub
   }

``merlin.conf`` on *peer02*

.. code-block:: none

   peer peer01 {
        address = IP_ADDR
        port = 15551
        encrypted = 1
        publickey = /etc/merlin/peer01.pub
   }

.. toctree::
   :maxdepth: 1 
