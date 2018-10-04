@config @daemons @merlin @queryhandler @livestatus

Feature: Testing "mon log purge" command for proper functioning
	Background: Set up merlin 
		Given I have merlin configured for port 7000
			| type | name | port | connect |

		And I start merlin
		And I wait for 1 second

	Scenario: check mon log purge command if only one host present
		Given I have the following report_data in the database
		|timestamp |event_type |host_name       |service_description |state |hard |
		|100       |111        |host1			|service1      		 |0     |0    |
		|101       |111        |host1			|service1      		 |0     |0    |
		|102       |111        |host1			|service1     		 |0     |0    |
		|103       |111        |host1			|service1     		 |0     |0    |
		
		When I start command mon log purge --remove-older-than=1d
		Then table report_data contain 1 matching row
            | timestamp      | 103                |
            | event_type     | 111                |
            | host_name      | host1              |
            | service_description | service1      |
		And table report_data has 1 entries

	Scenario: check mon log purge command if multiple hosts and services present
		Given I have the following report_data in the database
		|timestamp |event_type |host_name       |service_description |state |hard |
		|100       |111        |host1			|service1      		 |0     |0    |
		|101       |111        |host1			|service1      		 |0     |0    |
		|102       |111        |host1			|service1     		 |0     |0    |
		|103       |111        |host2			|service1     		 |0     |0    |
		|104       |111        |host2			|service1      		 |0     |0    |
		|105       |111        |host2			|service2     		 |0     |0    |
		|106       |111        |host2			|service2     		 |0     |0    |
		
		When I start command mon log purge --remove-older-than=1d
		Then table report_data contain 1 matching row
            | timestamp      | 102                |
            | event_type     | 111                |
            | host_name      | host1              |
            | service_description | service1      |

        And table report_data contain 1 matching row
            | timestamp      | 104                |
            | event_type     | 111                |
            | host_name      | host2              |
            | service_description | service1      |

        And table report_data contain 1 matching row
            | timestamp      | 106                |
            | event_type     | 111                |
            | host_name      | host2              |
            | service_description | service2      |

		And table report_data has 3 entries

	Scenario: check mon log purge command if different event_type present
		Given I have the following report_data in the database
		|timestamp |event_type |host_name       |service_description |state |hard |
		|100       |111        |host1			|service1      		 |0     |0    |
		|101       |111        |host1			|service1      		 |0     |0    |
		|102       |111        |host1			|service1     		 |0     |0    |
		|103       |111        |host2			|service1     		 |0     |0    |
		|104       |111        |host2			|service1      		 |0     |0    |
		|105       |111        |host2			|service2     		 |0     |0    |
		|106       |111        |host2			|service2     		 |0     |0    |
		|107       |111        |host3			|service1     		 |0     |0    |
		|108       |222        |host3			|service1     		 |0     |0    |
		|109       |222        |host3			|service2     		 |0     |0    |
		
		When I start command mon log purge --remove-older-than=1d
		Then table report_data contain 1 matching row
            | timestamp      | 107                |
            | event_type     | 111                |
            | host_name      | host3              |
            | service_description | service1      |

        And table report_data contain 1 matching row
            | timestamp      | 108                |
            | event_type     | 222                |
            | host_name      | host3              |
            | service_description | service1      |

        And table report_data contain 1 matching row
            | timestamp      | 109                |
            | event_type     | 222                |
            | host_name      | host3              |
            | service_description | service2      |

		And table report_data has 6 entries


	Scenario: check mon log purge command if different state and hard present
		Given I have the following report_data in the database
		|timestamp |event_type |host_name       |service_description |state   |hard   |
		|100       |111        |host1			|service1      		 |0       |1      |
		|101       |111        |host1			|service1      		 |1       |0      |
		|102       |111        |host1			|service1      		 |1       |0      |
		|103       |111        |host1			|service1      		 |0       |0      |

		When I start command mon log purge --remove-older-than=1d
		Then table report_data contain 1 matching row
            | timestamp      | 100                |
            | event_type     | 111                |
            | host_name      | host1              |
            | service_description | service1      |
            | state          |0                   |
            | hard           |1                   |

        And table report_data contain 1 matching row
            | timestamp      | 102                |
            | event_type     | 111                |
            | host_name      | host1              |
            | service_description | service1      |
            | state          |1                   |
            | hard           |0                   |

        And table report_data contain 1 matching row
            | timestamp      | 103                |
            | event_type     | 111                |
            | host_name      | host1              |
            | service_description | service1      |
            | state          |0                   |
            | hard           |0                   |

		And table report_data has 3 entries