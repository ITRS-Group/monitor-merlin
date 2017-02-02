@config @daemons @merlin @queryhandler @livestatus
Feature: Binlog
	The binlog should start filling up when nodes disconnect

	Background: Set up naemon configuration
		Given I have naemon host objects
			| use          | host_name | address   |
			| default-host | hostA     | 127.0.0.1 |
			| default-host | hostB     | 127.0.0.1 |

	Scenario: Log error when binlog dir does not exist
		Given I have merlin config binlog_dir set to ./asdf-not-existing
		And I start naemon with merlin nodes connected
			| type   | name    | port | data_timeout |
			| peer   | my-peer | 4001 | 1            |

		When I wait for 2 seconds
		Then my-peer should appear disconnected

		When I send naemon command PROCESS_HOST_CHECK_RESULT;hostA;1;Fromtest
		Then file merlin.log matches ERROR: Cannot write to binlog dir
