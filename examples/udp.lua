local skynet = require "skynet"
local snax = require "snax"
local socket = require "socket"
local peer_port = ...
local handle = function(...)
   print(...)
   
end
skynet.start(function()		
		socket.receive_handle(handle)
		local id1 = socket.open_udp("127.0.0.1",5002)
		local id2 = socket.open_udp("127.0.0.1",5001)
		socket.write(id2,"ping","127.0.0.1",5002)
		socket.write(id2,"ping2","127.0.0.1",5002)
end)
