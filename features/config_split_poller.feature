@config @daemons @merlin @queryhandler @livestatus
Feature: Merlin splits the configuration so that pollers only gets the configuration they need

    Background: Set up naemon configuration
        Given I have naemon hostgroup objects
            | hostgroup_name | alias |
            | pollergroup    | PG    |
            | emptygroup     | EG    |
        And I have naemon host objects
            | use          | host_name | address   | contacts  | hostgroups  |
            | default-host | hostA     | 127.0.0.1 | myContact | pollergroup |
            | default-host | hostB     | 127.0.0.2 | myContact | pollergroup |
        And I have naemon service objects
            | use             | host_name | description |
            | default-service | hostA     | PING        |
            | default-service | hostA     | PONG        |
            | default-service | hostB     | PING        |
            | default-service | hostB     | PONG        |
        And I have naemon contact objects
            | use             | contact_name |
            | default-contact | myContact    |

    Scenario Outline: Splitting a servicegroup should always result in an
        ordered member list in the poller configuration. The order should
        always be the same if the members are the same, regardsless of the
        order in the masters configuration so that masters cannot generate
        servicegroup members in arbitrary order. Having multiple servicegroups
        should also not affect the output of the configuration split.
        Given I have naemon servicegroup objects
            | servicegroup_name | alias | members   |
            | sg1               | sg1   | <members> |
            | sg2               | sg2   | <members> |
        And I start naemon with merlin nodes connected
            | type   | name        | port | hostgroup   |
            | poller | poller_one  | 4001 | pollergroup |
            | poller | poller_two  | 4002 | pollergroup |
        Then file poller_one.cfg has 1 occurence of define servicegroup \{\s*servicegroup_name\ssg1\s*alias\ssg1\s*members\s<output>\s*\}
        And file poller_one.cfg has 1 occurence of define servicegroup \{\s*servicegroup_name\ssg2\s*alias\ssg2\s*members\s<output>\s*\}
        And files poller_one.cfg and poller_two.cfg are identical
        
        Examples:
            | members                                     | output                                      |
            | hostA,PING,hostA,PONG,hostB,PING,hostB,PONG | hostA,PING,hostA,PONG,hostB,PING,hostB,PONG |
            | hostB,PONG,hostB,PING,hostA,PONG,hostA,PING | hostA,PING,hostA,PONG,hostB,PING,hostB,PONG |
            | hostB,PING,hostA,PING,hostB,PONG,hostA,PONG | hostA,PING,hostA,PONG,hostB,PING,hostB,PONG |
