@config @daemons @merlin @queryhandler
Feature: Startup of merlin and Naemon

	Scenario: Naemon and Merlin should be able to start correct even if a Merlin
		node has unresolvable DNS.

		Given I have merlin configured for port 7000
			| type | name    | address   | port |
			| peer | my_peer | my_peer   | 4123 |

		When I start naemon
		And I wait for 1 second

	Then my_peer should appear disconnected

	@systemd
	Scenario: merlind should run as the monitor user when started from systemd

		Given I start merlind with systemd
		Then merlind should not run as the root user
