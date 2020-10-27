@config @daemons @merlin @queryhandler
Feature: Module handles network and connections

	Module should connect to the daemon, so daemon can manage the database.
	
	Otherwise, network connections is handled by the module, and both nodes
	normally connects to each other, and the first open connection between two
	nodes is used, and no other conections should be made.
	
	In the case of lost connection, the nodes should reconnect, and a new
	successful connection replaces the old one.
	
	In a distributed system nodes should be aware of each others state.
	Meaning that if a peer/poller/master is disconnected, it should be known to
	all other nodes that the specific node is down.

	Background: A hostgroup for pollers
		Given I have naemon hostgroup objects
			| hostgroup_name | alias |
			| emptygroup     | EG    |
	
	Scenario: The module initiates the connetion to merlind
		Given I have merlin configured for port 7000
			| type | name   | port |
		And merlind listens for merlin at socket test_ipc.sock
		
		When I start naemon
		
		Then merlind is connected to merlin
		And merlind received event CTRL_ACTIVE
		And I ask query handler merlin nodeinfo
			| filter_var | filter_val | match_var   | match_val       |
			| name       | ipc        | state       | STATE_CONNECTED |

	Scenario: The module connects to peers
		Given I have merlin configured for port 7000
			| type | name    | port |
			| peer | my_peer | 4123 |
		And my_peer listens for merlin at port 4123
		
		When I start naemon
		And I wait for 1 second
	    And node my_peer have info hash my_hash at 5000
	    And node my_peer have expected hash my_hash at 5000
	    
		Then my_peer is connected to merlin
		
		When my_peer sends event CTRL_ACTIVE
			| configured_peers   |           1 |
			| configured_pollers |           0 |
			| configured_masters |           0 |
		And I wait for 1 second
			
		Then my_peer received event CTRL_ACTIVE
		And I ask query handler merlin nodeinfo
			| filter_var | filter_val | match_var   | match_val       |
			| name       | my_peer    | state       | STATE_CONNECTED |

	Scenario: The module connects to pollers
		Given I have merlin configured for port 7000
			| type   | name      | port | hostgroups |
			| poller | my_poller | 4123 | emptygroup |
		And my_poller listens for merlin at port 4123
		
		When I start naemon
		And I wait for 1 second
	    And node my_poller have info hash my_hash at 5000
	    And node my_poller have expected hash my_hash at 5000
	    
		Then my_poller is connected to merlin
		
		# Should match the pollers environment
		When my_poller sends event CTRL_ACTIVE
			| configured_peers   |           0 |
			| configured_pollers |           0 |
			| configured_masters |           1 |
		And I wait for 1 second
		
		Then my_poller received event CTRL_ACTIVE
		And I ask query handler merlin nodeinfo
			| filter_var | filter_val | match_var   | match_val       |
			| name       | my_poller  | state       | STATE_CONNECTED |

	Scenario: The module connects to masters
		Given I have merlin configured for port 7000
			| type   | name      | port |
			| master | my_master | 4123 |
		And my_master listens for merlin at port 4123
		
		When I start naemon
		And I wait for 1 second
	    And node my_master have info hash my_hash at 5000
	    And node my_master have expected hash my_hash at 5000
	    
		Then my_master is connected to merlin
		
		# Should match the pollers environment
		When my_master sends event CTRL_ACTIVE
			| configured_peers   |           0 |
			| configured_pollers |           0 |
			| configured_masters |           1 |
		And I wait for 1 second
		
		Then my_master received event CTRL_ACTIVE
		And I ask query handler merlin nodeinfo
			| filter_var | filter_val | match_var   | match_val       |
			| name       | my_master  | state       | STATE_CONNECTED |

	Scenario: The module listens to peers
		Given I have merlin configured for port 7000
			| type | name    | port |
			| peer | my_peer | 4123 |
			
		When I start naemon
		And I wait for 1 second
	    And node my_peer have info hash my_hash at 5000
	    And node my_peer have expected hash my_hash at 5000
	    
		Then my_peer connect to merlin at port 7000 from port 11123
		And my_peer is connected to merlin
		
		When my_peer sends event CTRL_ACTIVE
			| configured_peers   |           1 |
			| configured_pollers |           0 |
			| configured_masters |           0 |
		And I wait for 1 second
			
		Then my_peer received event CTRL_ACTIVE
		And I ask query handler merlin nodeinfo
			| filter_var | filter_val | match_var   | match_val       |
			| name       | my_peer    | state       | STATE_CONNECTED |

	Scenario: The module listens to pollers
		Given I have merlin configured for port 7000
			| type   | name      | port | hostgroups |
			| poller | my_poller | 4123 | emptygroup |
	
		When I start naemon
		And I wait for 1 second
	    And node my_poller have info hash my_hash at 5000
	    And node my_poller have expected hash my_hash at 5000
	    
		Then my_poller connect to merlin at port 7000 from port 11123
		And my_poller is connected to merlin
		
		# Should match the pollers environment
		When my_poller sends event CTRL_ACTIVE
			| configured_peers   |           0 |
			| configured_pollers |           0 |
			| configured_masters |           1 |
		And I wait for 1 second
			
		Then my_poller received event CTRL_ACTIVE
		And I ask query handler merlin nodeinfo
			| filter_var | filter_val | match_var   | match_val       |
			| name       | my_poller  | state       | STATE_CONNECTED |

	Scenario: The module listens to masters
		Given I have merlin configured for port 7000
			| type   | name      | port |
			| master | my_master | 4123 |

		When I start naemon
		And I wait for 1 second
	    And node my_master have info hash my_hash at 5000
	    And node my_master have expected hash my_hash at 5000
	    
		Then my_master connect to merlin at port 7000 from port 11123
		And my_master is connected to merlin
		
		# Should match the pollers environment
		When my_master sends event CTRL_ACTIVE
			| configured_peers   |           0 |
			| configured_pollers |           0 |
			| configured_masters |           1 |
		And I wait for 1 second

		Then my_master received event CTRL_ACTIVE
		And I ask query handler merlin nodeinfo
			| filter_var | filter_val | match_var   | match_val       |
			| name       | my_master  | state       | STATE_CONNECTED |

	Scenario: As a peer, if the number of seconds passed is greater than the
		limit set by the parameter data_timeout we should consider the other
		peer to be disconnected.

		Given I start naemon with merlin nodes connected
			| type   | name        | port | hostgroup  | data_timeout |
			| peer   | the_peer    | 4001 | ignore     | 2            |
		And the_peer should appear connected

		When I wait for 3 seconds

		Then the_peer should appear disconnected

	Scenario: As a peer, if the number of seconds passed is less than the
		limit set by the parameter data_timeout we should consider the other
		peer to be connected.

		Given I start naemon with merlin nodes connected
			| type   | name        | port | hostgroup  | data_timeout |
			| peer   | the_peer    | 4001 | ignore     | 5            |
		And the_peer should appear connected

		When I wait for 2 seconds

		Then the_peer should appear connected

	Scenario: As a master, if the number of seconds passed is greater than the
		limit set by the parameter data_timeout we should consider the poller
		to be disconnected.

		Given I start naemon with merlin nodes connected
			| type     | name          | port | hostgroup  | data_timeout |
			| poller   | the_poller    | 4001 | emptygroup | 2            |
		And the_poller should appear connected

		When I wait for 4 seconds

		Then the_poller should appear disconnected

	Scenario: As a master, if the number of seconds passed is less than the
		limit set by the parameter data_timeout we should consider the poller
		to be connected.

		Given I start naemon with merlin nodes connected
			| type   | name        | port | hostgroup  | data_timeout |
			| poller | the_poller  | 4001 | emptygroup | 5            |
		And the_poller should appear connected

		When I wait for 2 seconds

		Then the_poller should appear connected

	Scenario: As a poller, if the number of seconds passed is greater than the
		limit set by the parameter data_timeout we should consider the master
		to be disconnected.

		Given I start naemon with merlin nodes connected
			| type     | name          | port | hostgroup  | data_timeout |
			| master   | the_master    | 4001 | ignore     | 2            |
		And the_master should appear connected

		When I wait for 4 seconds

		Then the_master should appear disconnected

	Scenario: As a poller, if the number of seconds passed is less than the
		limit set by the parameter data_timeout we should consider the master
		to be connected.

		Given I start naemon with merlin nodes connected
			| type   | name        | port | hostgroup  | data_timeout |
			| master | the_master  | 4001 | ignore     | 5            |
		And the_master should appear connected

		When I wait for 2 seconds

		Then the_master should appear connected
