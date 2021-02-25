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
		Then file merlin.log matches WARNING: Maximum binlog size reached for node my-peer

	Scenario: Binlog is saved on disk for persistence if nodes are
		disconnected and we restart this nodes.
		Given I start naemon with merlin nodes connected
			| type   | name    | port | data_timeout |
			| peer   | my-peer | 4001 | 1            |
		And I wait for 2 seconds

		When my-peer becomes disconnected
		And I send naemon command PROCESS_HOST_CHECK_RESULT;hostA;1;Fromtest
		And I send naemon command SHUTDOWN_PROGRAM
		And I wait for 2 seconds
		Then the file module.my-peer.binlog.save should exist
		And the file module.my-peer.binlog.meta should exist

	Scenario: Binlog is not saved on disk when nodes are connected
		Given I start naemon with merlin nodes connected
			| type   | name    | port |
			| peer   | my-peer | 4001 |
		And my-peer is connected to merlin

		When I send naemon command PROCESS_HOST_CHECK_RESULT;hostA;1;Fromtest
		And I wait for 2 seconds
		And I send naemon command SHUTDOWN_PROGRAM
		Then the file module.my-peer.binlog.save should not exist
		And the file module.my-peer.binlog.meta should not exist

	Scenario: Saved binlog is loaded and deleted when naemon starts
		Given I start naemon with merlin nodes connected
			| type   | name    | port | data_timeout |
			| peer   | my-peer | 4001 | 1            |
		And I wait for 2 seconds

		When my-peer becomes disconnected
		And I send naemon command PROCESS_HOST_CHECK_RESULT;hostA;1;Fromtest
		And I send naemon command RESTART_PROGRAM
		And I wait for 2 seconds
		Then the file module.my-peer.binlog.save should not exist
		And the file module.my-peer.binlog.meta should not exist

	Scenario: Saved binlog is sent to peer after restart.
		Given I have a saved binlog
		And I have merlin config binlog_persist set to 1
		And I start naemon with merlin nodes connected
			| type   | name    | port |
			| peer   | my-peer | 4001 |

		And my-peer is connected to merlin
		# Binlog is sent right before we send normal events.
		# To ensure the binlog is sent, we send a small event first
		And I send naemon command START_EXECUTING_HOST_CHECKS

		Then the file module.my-peer.binlog.save should not exist
		And the file module.my-peer.binlog.meta should not exist
		And my-peer received event HOST_CHECK
			| state.plugin_output | Fromtest |
			| state.current_state | 1        |
		And my-peer received event EXTERNAL_COMMAND
			| command_type   | 87                        |
			| command_string | PROCESS_HOST_CHECK_RESULT |
			| command_args   | hostA;1;Fromtest          |

	Scenario: Binlog is NOT saved on disk if binlog persist is disabled
		Given I have merlin config binlog_persist set to 0
		And I start naemon with merlin nodes connected
			| type   | name    | port | data_timeout |
			| peer   | my-peer | 4001 | 1            |
		And I wait for 2 seconds

		When my-peer becomes disconnected
		And I send naemon command PROCESS_HOST_CHECK_RESULT;hostA;1;Fromtest
		And I send naemon command SHUTDOWN_PROGRAM
		And I wait for 2 seconds
		Then the file module.my-peer.binlog.save should not exist
		And the file module.my-peer.binlog.meta should not exist

	Scenario: Saved binlog is not touched or sent to peer after restart if
		binlog persist is disabled.
		Given I have a saved binlog
		And I have merlin config binlog_persist set to 0
		And I start naemon with merlin nodes connected
			| type   | name    | port |
			| peer   | my-peer | 4001 |

		#And I wait for 2 seconds
		And my-peer is connected to merlin
		# Binlog is sent right before we send normal events. We
		# to ensure the binlog is sent, we send a small event first
		And I send naemon command START_EXECUTING_HOST_CHECKS

		Then the file module.my-peer.binlog.save should exist
		And the file module.my-peer.binlog.meta should exist
		And my-peer should not receive HOST_CHECK
