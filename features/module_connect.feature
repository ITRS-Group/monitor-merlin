@config @daemons @merlin
Feature: Naemon module connects to merlin daemon
	When a module starts, the module should connect to the merlind daemon using
	ipc socket. The module should take initiative to the connection.

Background: Set up naemon configuration
	Given I have naemon host objects
		| use          | host_name | address   |
		| default-host | something | 127.0.0.1 |
		| default-host | gurka     | 127.0.0.2 |
	And I have naemon service objects
		| use             | host_name | description |
		| default-service | something | PING        |
		| default-service | something | PONG        |
		| default-service | gurka     | PING        |
		| default-service | gurka     | PONG        |
	And I have naemon objects stored in oconf.cfg
	And I have config dir checkresults
	And I have config file naemon.cfg
		"""
		cfg_file=oconf.cfg
		query_socket=naemon.qh
		check_result_path=checkresults
		"""
	And I start daemon naemon -v naemon.cfg

Scenario: The module initiates the connetion
	Given I wait for 1 second
	# TODO: when merlin module location can be updated, add connection tests