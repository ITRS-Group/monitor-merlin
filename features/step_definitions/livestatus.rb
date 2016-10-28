Then /^I have ([0-9]+) ([^ ]+) objects? matching (.+)$/ do |count, type, filter|
  steps %Q{
    Given I submit the following livestatus query
      | GET #{type}       |
      | Filter: #{filter} |
      | StatsAnd: 0       |
    Then I should see the following livestatus response
      | stats_1  |
      | #{count} |
  }
end