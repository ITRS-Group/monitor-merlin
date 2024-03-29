Configuration
==============

Configuration file format
--------------------------

The syntax is made up of a key without spaces and a value containing arbitrary
characters (although no semi-colons). A configuration statement is terminated
either by a newline or a semi-colon. A configuration statement starting with a
hash-character (#) is considered a comment. Thus,

.. code-block:: none

    key = value; # comment

makes "key" the key, "value" the value, terminated by the semi-colon, and "#
comment" all of the comment. Leading and trailing whitespace is ignored.

Merlins configuration consists of several sections. A section scope are started
by ``{`` and ended by ``}``. 

Sample config
--------------

The sample configuration below contains a description of all notable
configuration settings.

.. literalinclude:: merlin.conf.sample
    :language: none

.. toctree::
   :maxdepth: 1 
