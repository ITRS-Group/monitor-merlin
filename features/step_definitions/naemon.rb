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
