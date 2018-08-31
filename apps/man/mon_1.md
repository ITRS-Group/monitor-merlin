% MON(1)
% Johan Thorén, Linus Agren, Jacob Hansen
% September 2018

# NAME

mon

# SYNOPSIS

**mon**␣[**-h**]

# DESCRIPTION

The **mon** command is a very powerful command. It is used to manually stop and start the *monitor* system processes, and to set up a distributed or a load balanced environment.

Handle this command with care! It has the power to both create and destroy your whole *OP5 Monitor* installation.

You should not use **mon** unless specifically instructed by OP5 Support or the Documentation itself.

# OPTIONS

**-h**, **--help**
:  Display a friendly help message.

# COMMANDS

The following commands are understood:

**start**
:  Starts the *monitor* and *merlind* system processes.

**restart**
:  Restarts the *monitor* and *merlind* system processes.

# FILES

Foo.

# BUGS

Lots of bugs.

# EXAMPLE

**mon**␣**node**␣**status**
:  Print the status of all connected nodes, including both *peers* and *masters/pollers*.
