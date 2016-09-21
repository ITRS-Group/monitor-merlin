Given(/^I send naemon command (.*)$/) do |command|
  full_command = "[0] "+command
  step "I ask query handler command run #{full_command}", Cucumber::Ast::Table.new([
    ["filter_var", "filter_val", "match_var", "match_val"]
  ])
end