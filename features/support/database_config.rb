class DatabaseConfig
  attr_accessor :host
  attr_accessor :port
  attr_accessor :user
  attr_accessor :pass
  attr_accessor :name

  def initialize
    @host = "127.0.0.1" # 127.0.0.1 makes merlind use TCP instead of Unix socket
    @port = "30000"
    @user = "merlin"
    @pass = "merlin"
    @name = "merlin"
  end
end

Before do
  @databaseconfig = DatabaseConfig.new
end
