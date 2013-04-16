local skynet = require "skynet"
local socket = require "socket"

local function readline(sep)
	while true do
		local line = socket.readline(sep)
		if line then
			return line
		end
		coroutine.yield()
	end
end

local function split_package()
	while true do
		local cmd = readline "\n"
		if cmd ~= "" then
			skynet.send(skynet.self(), "text", cmd)
		end
	end
end

local split_co = coroutine.create(split_package)

skynet.register_protocol {
	name = "client",
	id = 3,
	pack = function(...) return ... end,
	unpack = function(msg,sz)
		assert(msg , "Stdin closed")
		socket.push(msg,sz)
		assert(coroutine.resume(split_co))
	end,
	dispatch = function () end
}


skynet.start(function()
	skynet.dispatch("text", function (session, address, cmd)
		local handle = skynet.newservice(cmd)
		if handle == nil then
			print("Launch error:",cmd)
		end
	end)
	if getos() ~= "win32" then
		socket.stdin()
	else
		socket.start_console()
	end
end)
function getos()
        -- Unix, Linux varients
        fh,err = io.popen("uname -o 2>/dev/null","r")
        if fh then
                osname = fh:read()
                end
        if osname then return osname end

        -- Add code for other operating systems here
        return "win32"
end
