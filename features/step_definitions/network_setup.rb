# Many test cases doesn't care about handshaking, but just that the correct
# nodes is configured and connected correctly. For those cases, this step just
# sets everything up according to the structure specified in the table.

Given(/^I start naemon with merlin nodes connected$/) do |nodes|
  sut_portnum = 7000

  configured_peers = 0
  configured_pollers = 0
  configured_masters = 0

  nodes.hashes.each do |obj|
    if obj["type"] == "peer" then
      configured_peers += 1
    end
    if obj["type"] == "poller" then
      configured_pollers += 1
    end
    if obj["type"] == "master" then
      configured_masters += 1
    end
  end

  step "I have merlin configured for port #{sut_portnum}", nodes
  step "ipc listens for merlin at socket test_ipc.sock"
  step "I start naemon"
  step "I wait for 1 second"
  step "ipc received event CTRL_ACTIVE"

  nodes.hashes.each do |obj|
    if obj["type"] == "peer" then
      step "node #{obj["name"]} have info hash my_hash at 3000"
      step "node #{obj["name"]} have expected hash my_hash at 4000"
      step "#{obj["name"]} connect to merlin at port #{sut_portnum} from port #{obj["port"].to_i+sut_portnum}"
      step "#{obj["name"]} sends event CTRL_ACTIVE", Cucumber::Ast::Table.new([
        ["configured_peers", configured_peers.to_s],
        ["configured_pollers", configured_pollers.to_s],
        ["configured_masters", configured_masters.to_s]
      ])
    end
    if obj["type"] == "poller" then
      step "node #{obj["name"]} have info hash my_hash at 3000"
      step "node #{obj["name"]} have expected hash my_hash at 4000"
      step "#{obj["name"]} connect to merlin at port #{sut_portnum} from port #{obj["port"].to_i+sut_portnum}"
      # TODO: multiple poller groups, this assumes a single poller group
      step "#{obj["name"]} sends event CTRL_ACTIVE", Cucumber::Ast::Table.new([
        ["configured_peers", (configured_pollers-1).to_s],
        ["configured_pollers", "0"],
        ["configured_masters", (configured_peers+1).to_s]
      ])
    end
    if obj["type"] == "master" then
      step "#{obj["name"]} connect to merlin at port #{sut_portnum} from port #{obj["port"].to_i+sut_portnum}"
      step "#{obj["name"]} received event CTRL_ACTIVE"
    end
    step "#{obj["name"]} is connected to merlin"
  end
end