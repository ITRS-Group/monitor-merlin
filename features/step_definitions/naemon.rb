Given(/^I send naemon command (.*)$/) do |command|
  full_command = "[0] "+command
  step "I ask query handler command run #{full_command}", Cucumber::Ast::Table.new([
    ["filter_var", "filter_val", "match_var", "match_val"]
  ])
end

Given(/^naemon status ([^ ]+) should be set to (.*)$/) do |name, value|
  steps %Q{
    When I submit the following livestatus query
      | GET status       |
      | Columns: #{name} |
    Then I should see the following livestatus response
      | #{name}  |
      | #{value} |
  }
end

Then(/^(.*) of service (.*) on host (.*) should be (.*)$/) do
    | param, service, host, value |
  steps %Q{
     When I submit the following livestatus query
       | GET services                            |
       | Columns: #{param} description host_name |
       | Filter: description = #{service}        |
       | Filter: host_name = #{host}             |
     Then I should see the following livestatus response
       | #{param} | description | host_name |
       | #{value} | #{service}  | #{host}   |
    }
end

Then(/^(.*) of host (.*) should be (.*)$/) do
    | param, host, value |
  steps %Q{
     When I submit the following livestatus query
       | GET hosts              |
       | Columns: #{param} name |
       | Filter: name = #{host} |
     Then I should see the following livestatus response
       | #{param} | name    |
       | #{value} | #{host} |
    }
end