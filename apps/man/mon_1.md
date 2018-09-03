% MON(1)
% Johan Thorén, Linus Agren, Jacob Hansen
% September 2018

# NAME

mon

# SYNOPSIS

**mon**␣**COMMAND**␣[**OPTIONS␣...**␣|␣**COMMAND␣...**]

# DESCRIPTION

The **mon** command is a very powerful command. It is used to manually stop and start the *monitor* system processes, and to set up a distributed or a load balanced environment.

Handle this command with care! It has the power to both create and destroy your whole *OP5 Monitor* installation.

# OPTIONS

**-h**, **--help**
:  Display a friendly help message.

# COMMANDS

The following commands are understood:

**start**
:  **mon**␣**start**
:  Starts the *monitor* and *merlind* system processes.

**restart**
:  **mon**␣**restart**
:  Restarts the *monitor* and *merlind* system processes.

## ECMD

**search**
:  **mon**␣**ecmd**␣**search**␣\<regex\>

# FILES

Foo.

# BUGS

Lots of bugs.

# EXAMPLE

**mon**␣**node**␣**status**
:  Print the status of all connected nodes, including both *peers* and *masters/pollers*.
