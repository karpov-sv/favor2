require "rfsm_timeevent"
require "rfsm_emem"
require "rfsm_ext"
require "utils"
require "rfsmpp"

-- Common routines for state machines

fsm = {}

-- Whether the FSM is running
is_running = false

-- Max ntries for message_send
message_send_max_ntries = 5

-- Time source for time events

-- function gettime()
--    return rtp.clock.gettime("CLOCK_MONOTONIC")
-- end

function gettime() return os.time(), 0 end

rfsm_timeevent.set_gettime_hook(gettime)

-- Guard condition generator
make_checker = function(cond)
   return loadstring("return (" .. cond .. ")")
end

-- Deferred events
deferred_queue = {}
deferred_list = {}

print_queue = function()
   print("Events:")
   for v,c in pairs(deferred_list) do
      print(" ", v, c)
   end
   print("Queue:")
   for i,v in ipairs(deferred_queue) do
      print(" ", i, v)
   end
   print()
end

defer_events = function(evt)
   if type(evt) ~= 'table' then evt={evt} end

   for _,e in pairs(evt) do
      if deferred_list[e] then deferred_list[e] = deferred_list[e]+1 else deferred_list[e] = 1 end
   end
end

undefer_events = function(evt)
   if type(evt) ~= 'table' then evt={evt} end

   for _,e in pairs(evt) do
      if deferred_list[e] and deferred_list[e] > 1 then
         deferred_list[e] = deferred_list[e] - 1
      else
         deferred_list[e] = nil
      end
   end

   -- Process events queued earlier
   local i = 1
   while i <= #deferred_queue do
      local still_deferred = false
      local event = deferred_queue[i][1]
      local source = deferred_queue[i][2]

      for e in pairs(deferred_list) do
         if e == event then
            still_deferred = true
         end
      end

      if not still_deferred then
         if source then print("Undeferring event (" .. source .. "): " .. event) else print("Undeferring event: " .. event) end
         get_message(event, source)
         table.remove(deferred_queue, i)
      else
         i = i + 1
      end
   end
end

-- Parse message into command name and keyword args
parse_command = function(string)
   local splitted = utils.split(string, "%s+")
   local name = splitted[1]
   local kw = {}

   for i = 2, #splitted do
      local s = utils.split(splitted[i], "=")

      if #s == 2 then
         kw[s[1]] = s[2]
      else
         kw[#kw+1] = s[1]
      end
   end

   return name, kw
end

-- Process the event from outside
get_message = function(evt, source)
   local event_name, kwargs = parse_command(evt)

   if deferred_list[evt] then
      table.insert(deferred_queue, {evt, source})
      if source then print("Deferring event (" .. source .. "): " .. evt) else print("Deferring event: " .. evt) end
      return
   end

   if event_name ~= "tick" and event_name ~= "image_acquired" then
      if source then print("Got event (" .. source .. "): " .. evt) else print("Got event: " .. evt) end
   end

   -- Events from named source will be prepended with the source name
   -- make_message_state depends on this behaviour!
   if source then
      event_name = source .. "___" .. event_name
   end

   -- print("Event name: " .. event_name .. ", args: " .. utils.tab2str(kwargs))

   message_kwargs = kwargs

   rfsm.send_events(fsm, event_name)
   if not is_running then
      is_running = true
      rfsm.run(fsm)
      is_running = false
   end
end

-- Get keyword arg (or default value) from latest message
get_message_arg = function(key, default)
   if message_kwargs then
      return message_kwargs[key] or default
   else
      return default
   end
end

-- Auxiliary function - interpolation of globally-defined var as ${var} in string
function interpolate_globals(s)
   -- return (s:gsub('($%b{})', function(w) return _G[w:sub(3, -2)] or w end))

   -- This way we may use in principle any evaluable construct inside the brackets
   return (s:gsub('($%b{})', function(w) return loadstring("return " .. w:sub(3, -2))() or w end))
end

-- Guess command name from a message, separate it from args
function message_name(s)
   return s:gmatch("([^%s]+)")()
end

-- Generic state for sending the message and waiting for reply
make_message_state = function(msg, target, timeout)
   timeout = timeout or 0

   -- Beware: this will never be interpolated!
   local msg_name = message_name(msg)

   if target and target ~= "" then msg_name = target .. "___" .. msg_name end

   local state = rfsm.state
   {
      -- entry = function() print("sending message: " .. msg) end,
      entry = function() message_send_ntries = 0 end,

      rfsm.trans {src="initial", tgt="start"},

      start = rfsm.state{},
      rfsm.trans{src="start", tgt="sending"},

      sending = rfsm.state {entry = function()
                               if target and target ~= "" then
                                  send_message_to(target, interpolate_globals(msg))
                               else
                                  send_message(interpolate_globals(msg))
                               end
                               message_send_ntries = message_send_ntries + 1
                           end},

      rfsm.trans {src="sending", tgt="reply_done", events={msg_name .. "_done"}},
      -- Repeat if necessary on _timeout reply
      rfsm.trans {src="sending", tgt="start", events={msg_name .. "_timeout"}, guard=make_checker("message_send_ntries < message_send_max_ntries")},
      rfsm.trans {src="sending", tgt="reply_timeout", events={msg_name .. "_timeout"}, guard=make_checker("not (message_send_ntries < message_send_max_ntries)")},

      reply_done = rfsm.state {entry = function() dprint("Completed: " .. interpolate_globals(msg)) end},
      rfsm.trans {src="reply_done", tgt="done"},

      reply_timeout = rfsm.state {entry = function()
                                     dprint("Timeout: " .. interpolate_globals(msg) .. " after ".. interpolate_globals("${message_send_ntries}") .." tries")
                                     rfsm.send_events(fsm, "e_message_timeout")
                                 end},
      rfsm.trans {src="reply_timeout", tgt="timeout"},

      -- 'Goog' ending
      done = rfsm.state {},

      -- 'Bad' ending
      timeout = rfsm.state {},
   }

   if timeout > 0 then
      -- Repeat if necessary on no replies
      state[#state+1] =
         -- Let's repeat infinitely
         -- rfsm.trans {src="sending", tgt="start", events={"e_after("..timeout..")"}}
         rfsm.trans {src="sending", tgt="start", events={"e_after("..timeout..")"}, guard=make_checker("message_send_ntries < message_send_max_ntries")}
      state[#state+1] =
         rfsm.trans {src="sending", tgt="reply_timeout", events={"e_after("..timeout..")"}, guard=make_checker("not (message_send_ntries < message_send_max_ntries)")}
   end

   return state
end

-- Get string representation of a current state
get_current_state = function()
   if fsm._act_leaf then
      return fsm._act_leaf._fqn
   else
      return "unknown"
   end
end

-- Make N elements of a given string with number appended
get_string_seq = function(base, N)
   N = N or 1
   local res = {}

   for i=1,N do res[#res+1] = base .. i-1 end

   return res
end

-- Make sequential AND state with N sub-states generated by provided function
-- Function may also be provided (as a string) for run-time control whether to execute each sub-state or not
make_parallel_state = function(length, gen_fn, guard_fn, states)
   local length = length or 0
   states = states or {}

   local entry_fn = function() print "Entering parallel state"; parallel_num_success = 0; parallel_num_failure = 0 end
   local exit_fn = function() print(interpolate_globals("Exiting parallel state, success ${parallel_num_success} failure ${parallel_num_failure}")) end

   if type(states.entry) == 'function' then
      local entry_old = entry_fn
      entry_fn = function() entry_old(); states.entry() end
   end

   if type(states.exit) == 'function' then
      local exit_old = entry_fn
      exit_fn = function() exit_old(); states.exit() end
   end

   local state = {
      run=true,
      entry = entry_fn,
      exit = exit_fn,
   }

   if type(gen_fn) ~= 'function' then
      print "State generator should be given as function!"
      error()
   end

   if guard_fn and type(guard_fn) ~= 'string' then
      print "Guard function should be given as string!"
      error()
   end

   for i=1,length do
      local substate = gen_fn(i) -- ids are starting with 1
      local guardstate = rfsm.state {
         -- dbg=rfsmpp.gen_dbgcolor("fsm"..i,
         --                         { STATE_ENTER=true, STATE_EXIT=true}, false),
         info = false,
         warn = false,

         rfsm.trans{src="initial", tgt="guard", effect = function() parallel_num_success = parallel_num_success + 1 end},
         guard = rfsm.state{},
         main = substate, -- Final state for 'normal' ending
         error = rfsm.state{}, -- Final state for errors
      }

      if guard_fn then
         guardstate[#guardstate+1] = rfsm.trans{src="guard", tgt="main", guard=loadstring("return ("..guard_fn..")(".. i ..")")}
         guardstate[#guardstate+1] = rfsm.trans{src="main", tgt="error", guard=loadstring("return not ("..guard_fn..")(".. i ..")"), effect=function() parallel_num_failure = parallel_num_failure + 1; parallel_num_success = parallel_num_success - 1 end}
      else
         guardstate[#guardstate+1] = rfsm.trans{src="guard", tgt="main"}
      end
      guardstate[#guardstate+1] = rfsm.trans{src="guard", tgt="error", effect=function() parallel_num_failure = parallel_num_failure + 1; parallel_num_success = parallel_num_success - 1 end}

      state[#state+1] = rfsm.init(guardstate)
   end

   -- print("Parallel state: "..length.." substates")

   return rfsm_ext.seqand(state)
end

-- Generate state for sending several consecutive messages to single target
make_multi_message_state = function(msgs, targets, timeouts)
   local count = 0

   if type(msgs) == 'string' then msgs = {msgs} end

   local state = rfsm.state {
      rfsm.trans{src="initial", tgt="s0"},
      done = rfsm.state{},
      timeout = rfsm.state{},
   }

   for i=1, #msgs do
      local msg
      local target
      local timeout

      if type(targets) == 'table' and targets[i] then
         target = targets[i]
      else
         target = targets
      end

      if type(timeouts) == 'table' and timeouts[i] then
         timeout = timeouts[i]
      else
         timeout = timeouts
      end

      if type(msgs[i]) == 'table' then
         msg = msgs[i][1]
         target = msgs[i][2]
         timeout = msgs[i][3]
      else
         msg = msgs[i]
      end

      state["s"..count] = make_message_state(msg, target, timeout)
      state[#state+1] = rfsm.trans{src=".s"..count..".done", tgt="s"..(count+1)}
      state[#state+1] = rfsm.trans{src=".s"..count..".timeout", tgt="timeout"}

      count = count + 1
   end

   state["s"..count] = rfsm.state{}
   state[#state+1] = rfsm.trans{src="s"..count, tgt="done"}

   return state
end

-- Send message to logger
log = function(msg, id)
   if id then
      send_message_to('logger', "log id=" .. id .. " " .. interpolate_globals(msg))
      print("Log (" .. id .. "): "..(os.date("!%Y-%m-%d %H:%M:%S")).." : " .. interpolate_globals(msg))
   else
      send_message_to('logger', interpolate_globals(msg))
      print("Log: "..(os.date("!%Y-%m-%d %H:%M:%S")).." : " .. interpolate_globals(msg))
   end
end

-- Send an email
send_email = function(text, subject, to, from)
   local to = to or "karpov.sv@gmail.com"
   local from = from or "karpov.sv@gmail.com"

   local mail = io.popen("mail -s '" .. subject .. "' " .. to .. " -a 'From: MMT'", "w")

   mail:write(text)
   mail:write("\4")
end
