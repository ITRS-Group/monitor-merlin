# Encrypted Merlin

It is possible to configure merlin to encrypt the packets sent over the merlin port (default 15551).
This can be useful if different OP5 Monitor nodes need to communicate over the internet, for example if you have a remote poller.
For nodes which are placed within the same secure local network, encryption is often unnecessary, and introduces an unnecessary performance penalty.

Merlin uses public key encryption, which requires setup of both a private and public key for each node.

The encryption used is:

Key agreement protocol: ECDH
Encryption: XSalsa20
Authentication: Poly1305 MAC

# Setup

## Generate and copy keys
To setup encryption, you must first generate a key pair on every node involved in the encrypted communication.

This is done using the command `mon merlinkey generate`. This will generate two files:
key.priv and key.pub at /opt/monitor/op5/merlin by default, or you can use --path=/preferred/path/here

When the keys have been created, the public key (key.pub) needs to be copied to the relevant servers.

This can be done with `scp`, for example, in a situation where we have one master and one poller:

On master:
scp /opt/monitor/op5/merlin/key.pub poller_ip:/opt/monitor/op5/merlin/master.pub

On poller:
scp /opt/monitor/op5/merlin/key.pub master_ip:/opt/monitor/op5/merlin/poller.pub

**Important:** The private key (key.priv) should never be shared.

## Change merlin configuration

After the key pairs have been generated and the public keys have been copied we need to configure merlin to use them.

To do so, we must change the merlin configuration file at /opt/monitor/op5/merlin/merlin.conf.

First we must add the "top-level" setting `ipc_privatekey`. It can be added directly after the `ipc_socket` line.
The setting must be set to the location of the private key, which is `/opt/monitor/op5/merlin/key.priv` by default.

Secondly we need to find the node configuration for the nodes we want to enable encryption for. Those are located at the end of the merlin.conf file. In every node definition we need to add two settings `encrypted` and `publickey`. `encrypted` must be set to 1 and the `publickey` must be set to the location of the copied public key file.

Example:

On master:

```
...
ipc_socket = /var/lib/merlin/ipc.sock;
ipc_privatekey = /opt/monitor/op5/merlin/key.priv

log_level = info;
...
poller poller {
	address = IP_ADDR
	port = 15551
	encrypted = 1
	publickey = /opt/monitor/op5/merlin/poller.pub
	hostgroup = pollergroup
}
```

On poller:

```
...
ipc_socket = /var/lib/merlin/ipc.sock;
ipc_privatekey = /opt/monitor/op5/merlin/key.priv

log_level = info;
...
master master {
	address = IP_ADDR
	port = 15551
	encrypted = 1
	publickey = /opt/monitor/op5/merlin/master.pub
}
```

## Restart monitor

After the configuration changes has been made, OP5 Monitor needs to be restarted on every node:

`mon restart`

You can then verify that encryption has been enabled by running:

`mon node status`

Look for the encryption status at the end of the first line for each node.
