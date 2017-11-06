@config @daemons @merlin @queryhandler @livestatus
Feature: Binlog
	The binlog should start filling up when nodes disconnect

	Background: Set up naemon configuration
		Given I have naemon host objects
			| use          | host_name | address   |
			| default-host | hostA     | 127.0.0.1 |
			| default-host | hostB     | 127.0.0.1 |
		And I have naemon service objects
			| use             | host_name | description |
			| default-service | hostA     | PONG        |

	Scenario Outline: Binlog does not grow larger than small binlog_max_file_size
		Given I have merlin config binlog_max_file_size set to <file_size>
		And I have merlin config binlog_max_memory_size set to <memory_size>
		And I start naemon with merlin nodes connected
			| type   | name    | port | data_timeout |
			| peer   | my-peer | 4001 | 1            |
		And I wait for 3 seconds

		When my-peer becomes disconnected
		And I send a passive check result for service PONG on host hostA with <output_data_size> MiB output data
		Then the <binlog> should not be larger than <MiB> MiB
        Examples:
		| file_size | memory_size | output_data_size | binlog                | MiB |
		| 1         | 0           | 2                | module.my-peer.binlog | 1   |
		| 10        | 0           | 12               | module.my-peer.binlog | 10  |
		| 10        | 1           | 2                | module.my-peer.binlog | 2   |

	Scenario: Log error when binlog dir does not exist
		Given I have merlin config binlog_dir set to ./asdf-not-existing
		And I start naemon with merlin nodes connected
			| type   | name    | port | data_timeout |
			| peer   | my-peer | 4001 | 1            |

		When I wait for 2 seconds
		And my-peer becomes disconnected

		When I send naemon command PROCESS_HOST_CHECK_RESULT;hostA;1;Fromtest
		Then file merlin.log matches ERROR: Cannot write to binlog dir

	Scenario: Log warning when binlog is full
		Given I have merlin config binlog_max_file_size set to 1
		And I have merlin config binlog_max_memory_size set to 0
		And I start naemon with merlin nodes connected
			| type   | name    | port | data_timeout |
			| peer   | my-peer | 4001 | 1            |

		When I wait for 2 seconds
		And my-peer becomes disconnected
		And I send a passive check result for service PONG on host hostA with 2 MiB output data
		Then file merlin.log matches WARNING: Maximum binlog size reached for node hostA
