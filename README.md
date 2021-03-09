# Welcome to Merlin #



     ... . ...   . .   .  .   ..        .=M7        .   ..   .
    ...  . .      . . .     .           ?MMM. .     .    .
    . .       . .. . .   .  ... . .   =MMMM=...     ... ...  .
        . . . ..       .          . ,MMMMM? .   .    .   .   .
      . .. .       .  .            $MMMMMD      ..   .   .   ..
     . . . . .      .    ..      .MMMMMMM      .   ..      ...
              .   .   .   .     .MMMMMMMI   .     .  ..  .. ...
      .     .                   MMMMMMMM     ..   . .     .
      ..                   .. .MMMMMMMMM. . . . .         .   .
                     .        DNNNNNNMMM
           . . .       .    .DNNNNNNNNNN .           .   .   .
                     .   . .MNNNNNNNNNNN.            .
          . . .         MMMMMMMMMI ?NNNN
        .                .MNNNNNNMDN.7NN
         .  ..      .   .MNNONNNNNNNN.N,    ..           ..
    .  .     .         ,M+   . .NNNNN          .. .         .. .
        ..... ..    MNDDD . .   .ZDDNDNM.   ..  .         .
     . .       .,MNDDDDDD     .  ZDDDDDDDDD.. .   . . . ... . .
     .        DNDDDDDDDDO        NDD. $DDDDDM,       .    .   .
       .    NDDDDDDDDO  I~       DDD~OMIDDDDDDDI     .  .. . .
     DM     888888DD.  .MD   .   ODDDDD. +DDDDDDD,       .   . .
    . OM8MMMD888888     D8  .   .7DDD?     DDDDDDDDMD
         $MM888888~.    88$8     =88       .DDDDDDDDDD
     7$$MMMMMMN8888  . .8888O.   :88..   ..  8888DDDDDDD      .
     .$I . M.MM 888..   88888$   =888        M888888MMMM
         ..MMM.    .    888888: .8888Z  .   .888,NMOMMMM     .
          DM:+    .   . 8888888 Z88888$      I    =8ZMZ   ..  .
    ..  .,M....    .    ?OOOOOO,8888888D    . ..  .MMM   . ..
             .           OOOOOOOOOOOOOOO88:  .    .MMMM,  .
        .       .   .    ?OOOOOOOOOOOOOOOOO~   .   . 8..   . . .
     .   . . .    .  ..  .OOOOOOOOOOOOOOOOOZ.   . .  .O~ .   .
    .    ..           .. .ZOOOOOOOOOOOOOOZ ,8.      ...,8 . ..
    . .    .   .  .  .   .=OOOOOO: .$OOO,.  ,OD.. .   . .O, .. .
        .      .  . ZMMMMMN8$ZOOZ . . . . . .$OO7.        +D  .
      .       ..  $OOOOOOOOOOOOOI       .      ,OON$,...,.  Z?
         .       .   . ..:~+=.   .   .   .   .   ZOOOOOOOOON
                   ..       .   .   .   .   .   .  ~7ZZZZZZO+
       .              .   .    . . .   .    .            .


The Merlin project, or Module for Effortless Redundancy and Loadbalancing In
Nagios, was initially started to create an easy way to set up distributed
Nagios installations, allowing Nagios processes to exchange information
directly as an alternative to the standard nagios way using NSCA. Now, the
nagios support is deprecated in favour of Naemon ( www.naemon.org ) which is a
fork of the Nagios project. Merlin can also save state changes to a database,
which can be used for reporting purposes.

## Brief description of the Merlin project ##

Merlin consists of:

merlin-mod: Responsible for jacking into the NEBCALLBACK_* calls and send them
to a socket. If the socket is not available the events are written to a backlog
and sent when the socket is available again.

merlind: The Merlin deamon listens to the socket that merlin-mod writes to and
sends all events received either to a database of your choice (using libdbi) or
to another merlin daemon. If the daemon is unsuccessful in this it writes to a
backlog and sends the data later.

merlin database: This is a database that contains state changes.

mon scripts: A bunch of utility scripts utilized to add new nodes to the system,
and other useful things.

## Requirements ##

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

## Building and installation ##

Building is a standard autotools fair: released tarballs does what you need
with

    ./configure
     make
     sudo make install

For git checkouts, do

     ./autogen.sh
     make
     sudo make install

The install will by default try to install merlin's database. The configure
script provides a way to configure database name and users, as well as a way to
prevent merlin from doing this automatically, in which case you need to run the
install-merlin.sh script manually. Run ./configure --help for more information.

Merlin will drop a naemon configuration file into the directory naemon's
configuration lies. You can include it in your main naemon config file with
include_file=merlin.cfg or by putting the config file in an already included
directory with the --with-naemon-config-dir argument to the configure script.

## Configuration ##

Configuring merlin is pretty straight-forward. Merlin's default config - by
default installed into /usr/local/etc/merlin/merlin.conf - contains most of the
common examples available.

More information and examples can be found in the HOWTO document that resides
in the same repo.

The syntax is fairly standard, being made up of a key without spaces and a
value containing arbitrary characters (although no semi-colons). A
configuration statement is terminated either by a newline or a semi-colon. A
configuration statement starting with a hash-character (#) is considered a
comment. Thus,

    key = value; # comment

makes "key" the key, "value" the value, terminated by the semi-colon, and "#
comment" all of the comment. Leading and trailing whitespace is ignored.

The thing it doesn't really cover very well is how to configure masters, peers
and pollers, which is described more in-depth here.

In order to set up a load balanced system (ie, 2 or more peers), all you need
to do is add a section similar to the following to your merlin configuration
files on your merlin-empowered Naemon systems.  Let's pretend we have "master1"
and "master2" in the network and you wish for them to be set up in load
balanced/redundancy mode. master1 has 192.168.1.1 as IP. master2 has
192.168.1.2. Both use port 15551 (the default).

On master1, add the following section to your merlin.conf file:

     peer master2.example.com {
      address = 192.168.1.2;
      port = 15551; # optional, since 15551 is the default
     }

 
On master2, add the following section to your merlin.conf file:

    peer master1.example.com {
     address = 192.168.1.1;
     port = 15551; # optional, since 15551 is the default
    }


Assuming master2 is a poller-node instead, rename it to poller 1 that is
responsible for checking hosts in germany, you need to create a hostgroup in
Naemon containing all the hosts in germany that you want poller1 to check for
you. Let's assume you call that hostgroup "germany-hosts".  Then you need to
add following sections to your merlin.conf files:

On master1 (the "master" server), add the following section:

    poller poller1.example.com {
     address = 192.168.1.2
     port = 15551;
     hostgroup = germany-hosts; # name of the hostgroup containing all
                                # the hosts you want this poller to check
    }


On poller1 (the slave server), add the following section:

    master master1.example.com {
     address = 192.168.1.1;
     port = 15551;
    }


Note that these configuration sections need to be in the base section of the
configuration file. They must *not* be inside the daemon section. This is
because the master server will disable checks for all its pollers once those
pollers connect, and therefore it needs to read the list of available nodes at
configuration time.

A merlin node can have up to 65534 neighbours (assuming your system lets a
single program have that many file-descriptors open). A neighbour is, in merlin
terminology, a node that merlin connects to directly, so you can build
arbitrarily large networks by just specifying multiple tiers of pollers.

A single merlin node can have pollers, peers and master nodes in its own
neighbourhood. As such, a single merlin node can, at the same time be a peer
(to its peers), a master (to its pollers) and a poller (to its masters). One
section has to be added to the merlin.conf file for each of the hosts in its
neighbourhood. The section must contain the address of the neighbour, the port
the neighbour is listening to (unless it's the default port 15551) and, if the
neighbour is a poller, the section *must* contain a hostgroup statement
declaring which hostgroup the poller is responsible for checking.
