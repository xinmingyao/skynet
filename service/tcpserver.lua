local buffer = require "socketbuffer"
local skynet = require "skynet"
local binlib = require "binlib.c"

local table = table
local next = next
local assert = assert
local coroutine = coroutine
local type = type

local sockets = {}	
local state ={}
local msg ={}
local state ={}
local selfaddr = skynet.self()
local tcpserveraddr
local function response(session)
	skynet.redirect(selfaddr , 0, "response", session, "")
end


local function bulk_fsm(narg)
	local obj={}
	obj.parser=function(self,buf)
		local count= self.count
		while count<self.narg do
			local line=buffer.readline(buf,"\r\n")
			count=count+1
			self.count=count
			if line then
				self.req[count-1]=line
			else
				return nil
			end	
		end	
        ---do business
	if self.req[1]=="GET" or self.req[1]=="get" then
		local key= self.req[3]
		local v=key.."test"
		local r="$"..tostring(string.len(v)).."\r\n"..v.."\r\n"
		return r
	elseif self.req[1]=="SET"then
		return "+NOT_FOUND"
	end
	end
	obj.narg=narg*2
	obj.count=0
	obj.req={}
	return obj
end


skynet.register_protocol {
	name = "client",
	id = 3,	-- PTYPE_CLIENT
	pack = buffer.pack,
	unpack = buffer.unpack,
	dispatch = function (_, _, fd, msg, sz)
		local buf=sockets[fd].buf
		local fsm=sockets[fd].fsm
		buf=buffer.push(buf,msg,sz)
		if fsm==nil then
			local line =buffer.readline(buf,"\r\n")
			if line then
				local prefix = string.sub(line, 1, 1)
				if prefix == "*" then
					local size = tonumber(string.sub(line, 2))
					fsm=bulk_fsm(size)
					sockets[fd].fsm=fsm
					local str=fsm.parser(fsm,buf)
					if str==nil then
						return
					else
						sockets[fd].fsm=nil
						skynet.send(tcpserveraddr,"client",fd,str,string.len(str))
					end
				else
					print("not support cmd now:",line)
				end
			else
				return
			end
		else
			local str=fsm.parser(fsm,buf)
			if str==nil then
				return
			else
				sockets.fsm=nil
				skynet.send(tcpserveraddr,"client",fd,str,string.len(str))
			end
		end
	end
}

skynet.register_protocol {
	name = "system",
	id = 4, -- PTYPE_SYSTEM
	pack = function (...) return ... end,
	unpack = function (...) return ... end,
	dispatch = function (session, addr, msg, sz)
		local _,cmd,fd = binlib.unpack("<II",msg,sz)
		if cmd == 1 then
				local socket={}
				socket.state=nil
				sockets[fd]=socket
				print("new client open",fd)
			elseif cmd==2 then
	            sockets[fd]=nil
			else
		end	
	end
}

local tcpserver = {}

skynet.start(function()
	skynet.dispatch("text", function(session, address, message)
		trace_cache[skynet.trace()] = message
		local cmd, key , value = string.match(message, "(%w+) (%w+) ?(.*)")
		local f = command[cmd]
		if f then
			f(key,value)
		else
			skynet.ret("Invalid command : "..message)
		end
	end)
	skynet.timeout(1, function()
				tcpserveraddr = skynet.launch("tcpserver","192.168.208.182:9999",selfaddr,20000,500)
				skynet.send(tcpserveraddr , 0, "start", 1, "")
				 end)
end)


