require "rfsm"

-- Import some common FSM functions from common.lua
require "common"

is_night = true
is_weather_good = true
is_dome_open = true

need_reschedule = false -- will be set by 'reschedule' command, and reset by scheduling

may_followup_alerts = false -- will be set when we are able to make alert (in-FOV) follow-ups

mount_connected = {}
channel_connected = {}

channel_state = {}
mount_state = {}

channel_nreboots = {}

for i=1,num_channels do channel_connected[i] = false end
for i=1,num_mounts do mount_connected[i] = false end

-- Target parameters to be set from C code
target_ra = 0
target_dec = 0
target_name = ""
target_type = "survey"
target_id = 0
target_exposure = 0
target_priority = 1
target_repeat = 1
target_filter = ""
target_uuid = ""

alert_ra = 0
alert_dec = 0
alert_channel_id = 0
alert_id = 0

-- Pre-defined positions for monitoring, 3x3 channels
target_delta_ra = {10, 0, -10, 10, 0, -10, -10, 0, 10}
target_delta_dec = {11, -10, 11, 3, 0, 0, -10, 10, -10}
target_filter_monitoring = 'Clear'
target_filter_33 = {'Clear', 'Clear', 'Clear', 'Clear', 'Clear', 'Clear', 'Clear', 'Clear', 'Clear'}

-- 3x1 channels
target_delta_ra_31 = {10, 0, -10, 10, 0, -10, -10, 0, 10}
target_delta_dec_31 = {0, 0, 0, 0, 0, 0, 0, 0, 0}
target_filter_31 = {'B', 'B', 'B', 'V', 'V', 'V', 'R', 'R', 'R'}

-- 1x3 channels
target_delta_ra_13 = {0, -1, 0, 0, 0, 1, 0, 0, 0}
--target_delta_dec_13 = {10, -10, 10, 0, 0, 0, -10, 10, -10}
target_delta_dec_13 = {7, -9, 10, 2, 0, 0, -9, 7, -10}
target_filter_13 = {'B', 'B', 'V', 'B', 'V', 'R', 'V', 'R', 'R'}

-- 1x1 channels
target_delta_ra_11 = {0, 0, 0, 0, 0, 0, 0, 0, 0}
target_delta_dec_11 = {0, 0, 0, 0, 0, 0, 0, 0, 0}
target_filter_11 = {'B+Pol', 'V+Pol', 'R+Pol', 'B+Pol', 'V+Pol', 'R+Pol', 'B+Pol', 'V+Pol', 'R+Pol'}

-- Pre-defined filters for multi-color imaging
target_filter_bvr = {'B', 'V', 'R', 'B', 'V', 'R', 'B', 'V', 'R'}
target_filter_bvr_pol = {'B+Pol', 'V+Pol', 'R+Pol', 'B+Pol', 'V+Pol', 'R+Pol', 'B+Pol', 'V+Pol', 'R+Pol'}
target_filter_bvr_clear_pol = {'Pol', 'B', 'Clear', 'Pol', 'V', 'Clear', 'Pol', 'R', 'Clear'}

-- Base followup settings. First channel is the one detected the alert, it is not affected
alert_exposures = {0, 1, 10, 10, 10, 10, 5, 5, 5}
alert_filters = {'Clear', 'Clear', 'Clear', 'B', 'V', 'R', 'Pol', 'Pol', 'Pol'}
alert_repeats = {0, 100, 10, 10, 10, 10, 20, 20, 20}

-- Multi-mode narrow-field follow-up for external triggers - 600 s duration
target_filter_multimode = {'Clear', 'Clear', 'Clear', 'B', 'V', 'R', 'Pol', 'Pol', 'Pol'}
target_exposure_multimode = {1, 5, 30, 10, 10, 10, 5, 5, 5}
target_repeat_multimode = {600, 120, 20, 60, 60, 60, 120, 120, 120}

-- INFO: Polaroids are: - 0 60 120 0 60 120 0 60 120

-- Guard conditions for normal channel operations
channel_guard_fn = function(id)
   return channel_connected[id] == true and
      channel_state[id]['state'] ~= '100' and
      channel_connected[math.floor((id+1)/2)*2-1] == true and
      channel_state[math.floor((id+1)/2)*2-1]['state'] ~= '100' and
      channel_state[id]['hw_connected'] == '1' and
      channel_state[math.floor((id+1)/2)*2-1]['hw_connected'] == '1' and
      mount_connected[math.floor((id+1)/2)] == true  and
      mount_state[math.floor((id+1)/2)]['connected'] == '1'
end
-- Guard conditions for mount-related channel operations
channel_mount_guard_fn = function(id)
   return id % 2 == 1 and
      channel_connected[id] == true and
      channel_state[id]['state'] ~= '100' and
      channel_state[id]['hw_connected'] == '1' and
      mount_connected[math.floor((id+1)/2)] == true and
      mount_state[math.floor((id+1)/2)]['connected'] == '1'
end
-- Guard condition for a mount
mount_guard_fn = function(id)
   return mount_connected[id] == true and
      mount_state[id]['connected'] == '1' and
      channel_connected[id*2-1] == true and
      channel_state[id*2-1]['state'] ~= '100'
end
-- Channels have mounts operational
channel_has_mount_guard_fn = function(id)
   return channel_connected[id] == true and
      channel_state[id]['hw_connected'] == '1' and
      mount_connected[math.floor((id+1)/2)] == true and
      mount_state[math.floor((id+1)/2)]['connected'] == '1'
end

-- Wrappers for make_multi_message_state to handle errors and disable channels/mounts
make_channel_message_state = function(id, msg, timeout)
   return rfsm.state{
      rfsm.trans{src="initial", tgt="s"},

      s = make_multi_message_state(msg, "channel"..id, timeout),

      rfsm.trans{src=".s.done", tgt="done"},
      rfsm.trans{src=".s.timeout", tgt="error"},

      -- TODO: inform the parallel state handler that we had an error
      error = rfsm.state{entry=function() end},
      rfsm.trans{src="error", tgt="done"},

      done = rfsm.state{},
   }
end

make_mount_message_state = function(id, msg, timeout)
   return rfsm.state{
      rfsm.trans{src="initial", tgt="s"},

      s = make_multi_message_state(msg, "mount"..id, timeout),

      rfsm.trans{src=".s.done", tgt="done"},
      rfsm.trans{src=".s.timeout", tgt="error"},

      -- TODO: inform the parallel state handler that we had an error
      error = rfsm.state{entry=function() end},
      rfsm.trans{src="error", tgt="done"},

      done = rfsm.state{},
   }
end

-- Channel init state
make_channel_init_state = function(id)
   return rfsm.state{
      rfsm.trans{src="initial", tgt="s_connected"},

      -- Check whether the channel is initially connected and has a mount
      s_connected = rfsm.state{entry=function() channel_nreboots[id] = 0 end},
      rfsm.trans{src="s_connected", tgt="stop", guard=make_checker("channel_connected["..id.."] and channel_state["..id.."]['hw_connected'] == '1' and mount_connected["..(math.floor((id+1)/2)).."] == true and mount_state["..(math.floor((id+1)/2)).."]['connected'] == '1'")},
      rfsm.trans{src="s_connected", tgt="done"},

      stop = make_multi_message_state({"processing_stop", "storage_stop"}, "channel"..id),
      rfsm.trans{src=".stop.done", tgt="s_camera_0"},

      -- Wait for HW to be connected
      s_camera_0 = rfsm.state{},
      rfsm.trans{src="s_camera_0", tgt="s_camera_01", guard=make_checker("channel_state["..id.."]['hw_connected'] == '1'")},
      rfsm.trans{src="s_camera_0", tgt="done"},

      s_camera_01 = rfsm.state{},
      rfsm.trans{src="s_camera_01", tgt="s_focus_0", guard=make_checker("channel_state["..id.."]['grabber'] == '1'")},
      rfsm.trans{src="s_camera_01", tgt="s_camera_1"},

      -- Turn off camera
      s_camera_1 = rfsm.state{entry=function() send_message_to("channel"..id, "hw set_camera 0") end},
      rfsm.trans{src="s_camera_1", tgt="s_camera_2", guard=make_checker("channel_state["..id.."]['hw_camera'] == '0'")},
      rfsm.trans{src="s_camera_1", tgt="s_camera_0", events={"e_after(10)"}},

      -- Turn on camera
      s_camera_2 = rfsm.state{},
      rfsm.trans{src="s_camera_2", tgt="s_camera_3"},

      s_camera_3 = rfsm.state{entry=function() send_message_to("channel"..id, "hw set_camera 1") end},
      rfsm.trans{src="s_camera_3", tgt="s_grabber_00", guard=make_checker("channel_state["..id.."]['hw_camera'] == '1'")},
      --rfsm.trans{src="s_camera_3", tgt="s_camera_2", events={"e_after(20)"}},
      rfsm.trans{src="s_camera_3", tgt="s_camera_final", events={"e_after(20)"}},

      s_grabber_00 = rfsm.state{},
      rfsm.trans{src="s_grabber_00", tgt="s_grabber_0", events={"e_after(10)"}},

      -- Restart the channel to initialize camera
      s_grabber_0 = rfsm.state{entry=function() send_message_to("channel"..id, "exit") end},
      rfsm.trans{src="s_grabber_0", tgt="s_grabber_1", events={"e_after(20)"}},

      -- Check whether the grabber is working
      s_grabber_1 = rfsm.state{entry=function() print("grabber "..id.." = "..channel_state[id]['grabber']) end},
      rfsm.trans{src="s_grabber_1", tgt="s_grabber_2", guard=make_checker("channel_state["..id.."]['grabber'] == '-1'")},
      rfsm.trans{src="s_grabber_1", tgt="s_focus_0", guard=make_checker("channel_state["..id.."]['grabber'] == '1'")},

      -- Reboot the PC
      s_grabber_2 = rfsm.state{entry=function() send_message_to("channel"..id, "reboot") end},
      rfsm.trans{src="s_grabber_2", tgt="s_grabber_3", events={"e_after(10)"}, effect=function() channel_state[id]['grabber'] = '0'; channel_nreboots[id] = channel_nreboots[id] + 1; print("channel "..id..": nreboots="..channel_nreboots[id])  end},

      -- Post-reboot check - if unsuccessful, reset the camera power again
      s_grabber_3 = rfsm.state{entry=function() print("grabber "..id.." = "..channel_state[id]['grabber']) end},
      rfsm.trans{src="s_grabber_3", tgt="s_camera_final", guard=make_checker("channel_state["..id.."]['grabber'] == '-1' and channel_nreboots["..id.."] > 1"), effect=function() log("Channel "..id.." has exceeded reboot limit while initlalizing") end},
      rfsm.trans{src="s_grabber_3", tgt="s_camera_0", guard=make_checker("channel_state["..id.."]['grabber'] == '-1'")},
      rfsm.trans{src="s_grabber_3", tgt="s_focus_0", guard=make_checker("channel_state["..id.."]['grabber'] == '1'")},

      -- Focus etc
      s_focus_0 = rfsm.state{entry=function() send_message_to("channel"..id, "hw reset_focus") end},
      rfsm.trans{src="s_focus_0", tgt="s_focus_1", events={"e_after(10)"}},
      s_focus_1 = rfsm.state{entry=function() send_message_to("channel"..id, "hw move_aperture 130") end},
      rfsm.trans{src="s_focus_1", tgt="s_focus_2", events={"e_after(2)"}},
      s_focus_2 = rfsm.state{entry=function() send_message_to("channel"..id, "hw move_aperture 10") end},
      --s_focus_2 = rfsm.state{entry=function() send_message_to("channel"..id, "hw move_aperture 0") end},
      rfsm.trans{src="s_focus_2", tgt="s_focus_3", events={"e_after(2)"}},
      s_focus_3 = rfsm.state{entry=function() send_message_to("channel"..id, "hw set_filters name=Clear") end},
      rfsm.trans{src="s_focus_3", tgt="done", events={"e_after(2)"}},

      -- Turn off the camera, error case
      s_camera_final = rfsm.state{entry=function() send_message_to("channel"..id, "hw set_camera 0") end},
      rfsm.trans{src="s_camera_final", tgt="done"},

      -- Grabbers should work at this point
      done = rfsm.state{},
   }
end

-- Monitoring state constructor with given duration
make_monitoring_state = function(state_name)
   state_name = state_name or "monitoring"

   start_fn = function(id)
      return rfsm.state
      {
         rfsm.trans{src="initial", tgt="setup"},

         setup = make_multi_message_state(
            {
               -- Here we inform the channel on suggested celostate position only, as it has mount position already
               "next_target delta_ra=${delta_ra["..id.."]} delta_dec=${delta_dec["..id.."]} filter=${filter["..id.."]} type=${target_type} exposure=${target_exposure} id=${target_id} uuid=${target_uuid} name=${target_name}",
               -- "write_flats", -- Should we really acquire flats for every field?
               "monitoring_start"
            }, "channel" .. id),

         rfsm.trans{src=".setup.done", tgt="done"},
         rfsm.trans{src=".setup.timeout", tgt="error"},

         error = rfsm.state{entry=function() log("Error starting monitoring on channel "..id) end},
         rfsm.trans{src="error", tgt="done"},

         done = rfsm.state{},
      }
   end

   stop_fn = function(id)
      return rfsm.state
      {
         rfsm.trans{src="initial", tgt="stop"},

         stop = make_multi_message_state({"monitoring_stop", "clear_target"}, "channel" .. id),
         rfsm.trans{src=".stop.done", tgt="done"},
         rfsm.trans{src=".stop.timeout", tgt="error"},

         error = rfsm.state{entry=function() log("Error stopping monitoring on channel "..id) end},
         rfsm.trans{src="error", tgt="done"},

         done = rfsm.state{},
      }
   end

   imaging_fn = function(id)
      return rfsm.state
      {
         rfsm.trans{src="initial", tgt="setup"},

         setup = make_multi_message_state(
            {
               -- Here we inform the channel on suggested celostate position only, as it has mount position already
               "next_target delta_ra=${delta_ra["..id.."]} delta_dec=${delta_dec["..id.."]} filter=${filter["..id.."]} type=survey exposure=20 name=survey-20s repeat=3",
               --"next_target delta_ra=${delta_ra["..id.."]} delta_dec=${delta_dec["..id.."]} filter=V type=survey exposure=60 name=survey-60s",
               "imaging",
               "clear_target",
            }, "channel" .. id),

         rfsm.trans{src=".setup.done", tgt="done"},
         rfsm.trans{src=".setup.timeout", tgt="error"},

         error = rfsm.state{entry=function() log("Error doing survey imaging on channel "..id) end},
         rfsm.trans{src="error", tgt="done"},

         done = rfsm.state{},
      }
   end

   followup_fn = function(id)
      return rfsm.state
      {
         rfsm.trans{src="initial", tgt="done", guard=make_checker(id .. "==alert_channel_id")},
         rfsm.trans{src="initial", tgt="setup1", guard=make_checker("need_reschedule")},
         rfsm.trans{src="initial", tgt="setup"},

         setup = make_multi_message_state(
            {
               "processing_stop",
               -- Here we inform the channel on suggested celostate position only, as it has mount position already
               "next_target ra=${alert_ra} dec=${alert_dec} filter=${alert_filter["..id.."]} exposure=${alert_exposure["..id.."]} repeat=${alert_repeat["..id.."]} type=alert name=alert_${alert_id}",
               --"next_target delta_ra=${delta_ra["..id.."]} delta_dec=${delta_dec["..id.."]} filter=V type=survey exposure=60 name=survey-60s",
               "imaging",
               "clear_target",
               "next_target delta_ra=${delta_ra["..id.."]} delta_dec=${delta_dec["..id.."]} filter=${filter["..id.."]} type=${target_type} exposure=${target_exposure} id=${target_id} uuid=${target_uuid} name=${target_name}",
               -- "write_flats", -- Should we really acquire flats for every field?
               "monitoring_start"
            }, "channel" .. id),

         rfsm.trans{src=".setup.done", tgt="done"},
         rfsm.trans{src=".setup.timeout", tgt="error"},

         -- A bit simpler version for when we need to re-schedule right after follow-up
         setup1 = make_multi_message_state(
            {
               "processing_stop",
               -- Here we inform the channel on suggested celostate position only, as it has mount position already
               "next_target ra=${alert_ra} dec=${alert_dec} filter=${alert_filter["..id.."]} exposure=${alert_exposure["..id.."]} repeat=${alert_repeat["..id.."]} type=alert name=alert_${alert_id}",
               --"next_target delta_ra=${delta_ra["..id.."]} delta_dec=${delta_dec["..id.."]} filter=V type=survey exposure=60 name=survey-60s",
               "imaging",
               "clear_target",
            }, "channel" .. id),

         rfsm.trans{src=".setup1.done", tgt="done"},
         rfsm.trans{src=".setup1.timeout", tgt="error"},

         error = rfsm.state{entry=function() log("Error doing followup on channel "..id) end},
         rfsm.trans{src="error", tgt="done"},

         done = rfsm.state{},
      }
   end

   return rfsm.state{
      entry = function()
         dprint("Monitoring state")
         send_message("set_state " .. state_name)

         -- Here we assume that target_ global variables already contain all the information
         -- on the current pointing
         if target_filter == 'BVR' then
            delta_ra = target_delta_ra_13
            delta_dec = target_delta_dec_13
            filter = target_filter_13
         elseif target_filter == 'BVR+Pol' then
            delta_ra = target_delta_ra_11
            delta_dec = target_delta_dec_11
            filter = target_filter_11
         elseif target_filter == 'BVR+Clear+Pol' then
            delta_ra = target_delta_ra_11
            delta_dec = target_delta_dec_11
            filter = target_filter_bvr_clear_pol
         else
            delta_ra = target_delta_ra
            delta_dec = target_delta_dec
            filter = target_filter_33
         end

      end,

      exit = function()
         dprint("Monitoring state finished")
      end,

      rfsm.trans{src="initial", tgt="imaging1"},

      -- Survey imaging
      imaging1 = make_parallel_state(num_channels, imaging_fn, "channel_guard_fn"),

      rfsm.trans{src="imaging1", tgt="error", events={"e_done"}, guard=make_checker("parallel_num_success == 0")},
      rfsm.trans{src="imaging1", tgt="done", events={"e_done"}, guard=make_checker("need_reschedule")},
      rfsm.trans{src="imaging1", tgt="start", events={"e_done"}},

      -- Parallel state to send monitoring_start
      start = make_parallel_state(num_channels, start_fn, "channel_guard_fn"),

      rfsm.trans{src="start", tgt="error", events={"e_done"}, guard=make_checker("parallel_num_success == 0")},
      rfsm.trans{src="start", tgt="wait", events={"e_done"}},

      wait = rfsm.state{entry = function() log("Monitoring started"); may_followup_alerts=true end,
                        exit = function() log("Monitoring stopped"); may_followup_alerts=false end},
      rfsm.trans{src="wait", tgt="report", events={"e_after(${target_exposure})"}, guard=make_checker("target_id > 0")},
      rfsm.trans{src="wait", tgt="stop", events={"e_after(${target_exposure})"}},
      rfsm.trans{src="wait", tgt="stop", events={"cancel"}}, -- Monitoring may be cancelled
      rfsm.trans{src="wait", tgt="followup_start", events={"alert"}}, -- Internal alert during the monitoring
      rfsm.trans{src="wait", tgt="stop", guard=make_checker("need_reschedule")}, -- Re-scheduling request

      followup_start = rfsm.state{entry = function()
                                     log("Followup started: id=" .. alert_id .. " ra=" .. alert_ra .. " dec=" .. alert_dec .. " channel_id=" .. alert_channel_id)
                                     send_email("MMT alert " .. alert_id .. " at ra=" .. alert_ra .. " dec=" .. alert_dec .. " channel_id=" .. alert_channel_id, "beholder: MMT alert")
                                     alert_exposure = {}
                                     alert_filter = {}
                                     alert_repeat = {}
                                     for i = 1,num_channels do
                                        local i1 = (i - alert_channel_id) % num_channels + 1
                                        alert_exposure[i] = alert_exposures[i1]
                                        alert_filter[i] = alert_filters[i1]
                                        alert_repeat[i] = alert_repeats[i1]
                                     end
                                 end},
      rfsm.trans{src="followup_start", tgt="followup"},

      followup = make_parallel_state(num_channels, followup_fn, "channel_guard_fn"),
      rfsm.trans{src="followup", tgt="error", events={"e_done"}, guard=make_checker("parallel_num_success == 0")},
      rfsm.trans{src="followup", tgt="followup_end", events={"e_done"}},

      followup_end = rfsm.state{entry = function() log("Followup finished") end},
      rfsm.trans{src="followup_end", tgt="stop", guard=make_checker("need_reschedule")}, -- Re-scheduling request
      rfsm.trans{src="followup_end", tgt="wait"},

      -- Report if necessary
      report = make_message_state("target_observed id=${target_id} uuid=${target_uuid}", "scheduler", 5),
      rfsm.trans{src=".report.done", tgt="stop", effect=function() log("Reported completion of target with id=${target_id}") end},

      -- Parallel state to send monitoring_stop
      stop = make_parallel_state(num_channels, stop_fn, "channel_guard_fn"),
      rfsm.trans{src="stop", tgt="error", events={"e_done"}, guard=make_checker("parallel_num_success == 0")},
      rfsm.trans{src="stop", tgt="done", events={"e_done"}, guard=make_checker("need_reschedule")},
      rfsm.trans{src="stop", tgt="imaging2", events={"e_done"}},

      -- Survey imaging - second time
      imaging2 = make_parallel_state(num_channels, imaging_fn, "channel_guard_fn"),

      rfsm.trans{src="imaging2", tgt="error", events={"e_done"}, guard=make_checker("parallel_num_success == 0")},
      rfsm.trans{src="imaging2", tgt="done", events={"e_done"}},

      -- Exit points
      error = rfsm.state{entry = function() log("Monitoring error") end},
      done = rfsm.state{},
   }
end

-- Imaging state constructor. Params are in target_* global variables
make_imaging_state = function(state_name)
   state_name = state_name or "imaging"

   imaging_fn = function(id)
      return rfsm.state
      {
         rfsm.trans{src="initial", tgt="setup"},

         setup = make_multi_message_state(
            {
               -- Here we inform the channel on suggested celostate position only, as it has mount position already
               "next_target delta_ra=${delta_ra["..id.."]} delta_dec=${delta_dec["..id.."]} filter=${filter["..id.."]} name=${target_name} type=${target_type} exposure=${exposure["..id.."]} repeat=${repeats["..id.."]} id=${target_id} uuid=${target_uuid}",
               "imaging",
               "clear_target",
            }, "channel" .. id),

         rfsm.trans{src=".setup.done", tgt="done"},
         rfsm.trans{src=".setup.timeout", tgt="error"},

         error = rfsm.state{entry=function() log("Error while imaging in channel "..id) end},
         rfsm.trans{src="error", tgt="done"},

         done = rfsm.state{},
      }
   end

   return rfsm.state{
      entry = function()
         dprint("Imaging state")
         send_message("set_state " .. state_name)
         log("Imaging started")

         -- Here we assume that target_ global variables already contain all the information
         -- on the current pointing

         -- By default 'Imaging' means all channels imaging the same region in same filter
         delta_ra = target_delta_ra_11
         delta_dec = target_delta_dec_11
         filter = {}; for i=1,num_channels do filter[i] = target_filter end
         exposure = {}; for i=1,num_channels do exposure[i] = target_exposure end
         repeats = {}; for i=1,num_channels do repeats[i] = target_repeat end

         if target_filter == 'BVR' then
            -- Three filters
            filter = target_filter_13
         elseif target_filter == 'BVR+Pol' then
            -- Three filters + three polarizations
            filter = target_filter_11
         elseif target_filter == 'Multimode' then
            filter = target_filter_multimode
            exposure = target_exposure_multimode
            repeats = target_repeat_multimode
         else
            -- Overrides for single-filter mode
            if target_type == 'LVC' or (target_type == 'Fermi' and target_filter == 'Clear') or target_type == 'widefield' then
               -- LVC need a really wide field
               delta_ra = target_delta_ra
               delta_dec = target_delta_dec
            end
         end
      end,

      exit = function()
         dprint("Imaging state finished")
         log("Imaging stopped")
      end,

      rfsm.trans{src="initial", tgt="imaging"},

      -- Imaging
      imaging = make_parallel_state(num_channels, imaging_fn, "channel_guard_fn"),
      rfsm.trans{src="imaging", tgt="error", events={"e_done"}, guard=make_checker("parallel_num_success == 0")},
      rfsm.trans{src="imaging", tgt="report", events={"e_done"}, guard=make_checker("target_id > 0")},
      rfsm.trans{src="imaging", tgt="done", events={"e_done"}},
      -- rfsm.trans{src="imaging", tgt="done", guard=make_checker("need_reschedule")}, -- Re-scheduling request

      -- Report if necessary
      report = make_message_state("target_observed id=${target_id} uuid=${target_uuid}", "scheduler", 5),
      rfsm.trans{src=".report.done", tgt="done", effect=function() log("Reported completion of target with id=${target_id}") end},

      -- Exit points
      error = rfsm.state{entry = function() log("Imaging error") end},
      done = rfsm.state{},
   }
end

-- System is active - let's observe!
system_active = rfsm.state
{
   entry = function()
      print("System is activated");
      send_message("set_state active")
      log("System is observing")
      log("Observations in automatic mode started", "news")
   end,

   exit = function()
      log("Observations in automatic mode stopped", "news")
   end,

   rfsm.trans{src="initial", tgt="init"},

   -- Zero-point
   zero = rfsm.state {entry = function() print("Zero-point state, waiting"); send_message("set_state zeropoint") end},
   rfsm.trans{src="zero", tgt="init", events={"e_after(10)"}},

   -- Init
   init = rfsm.state {
      entry = function() print("System is activated"); send_message("set_state prepare"); end,
   },
   rfsm.trans{src="init", tgt="init_mounts"},

   init_mounts = make_parallel_state(num_mounts,
                                     function(id) return make_mount_message_state(id, "tracking_start") end,
                                     "mount_guard_fn"),
   rfsm.trans{src="init_mounts", tgt="zero", events={"e_done"}, guard=make_checker("parallel_num_success == 0")},
   rfsm.trans{src="init_mounts", tgt="init_channels", events={"e_done"}},

   init_channels = make_parallel_state(num_channels, function(id)
                                          return make_channel_message_state(id, {
                                                                               "processing_stop",
                                                                               "storage_stop",
                                                                               -- "reset",
                                                                               -- "autofocus",
                                                                               -- "write_darks",
                                                                               -- "write_flats"
                                                     }) end,
                                       "channel_guard_fn"),

   rfsm.trans{src="init_channels", tgt="zero", events={"e_done"}, guard=make_checker("parallel_num_success == 0")},
   rfsm.trans{src="init_channels", tgt="locate_channels", events={"e_done"}},

   -- Locate
   locate_channels = make_parallel_state(num_channels,
                                         function(id) return make_channel_message_state(id, "locate") end,
                                         "channel_mount_guard_fn"),
   rfsm.trans{src="locate_channels", tgt="zero", events={"e_done"}, guard=make_checker("parallel_num_success == 0")},
   rfsm.trans{src="locate_channels", tgt="scheduling0", events={"e_done"}},

   -- Schedule
   scheduling0 = rfsm.state{entry=function() send_message("set_state schedule") end},
   rfsm.trans{src="scheduling0", tgt="scheduling"},

   scheduling = make_message_state("schedule", nil, 5),
   -- We may disable actions if its type is not interesting temporarily
   --rfsm.trans{src=".scheduling.done", tgt="waiting", guard=make_checker("target_type == 'survey'")},
   rfsm.trans{src=".scheduling.done", tgt="repointing", effect=function() send_message("set_state repointing"); need_reschedule=false end},
   rfsm.trans{src=".scheduling.timeout", tgt="waiting", effect=function() log("Scheduling failed") end},

   -- Repoint
   repointing = make_parallel_state(num_mounts,
                                    function(id) return make_mount_message_state(id, "repoint ra=${target_ra} dec=${target_dec}") end,
                                    "mount_guard_fn",
                                    {entry = function() log("Repointing to ra=${target_ra} dec=${target_dec} for target type '${target_type}'") end}),
   rfsm.trans{src="repointing", tgt="zero", events={"e_done"}, guard=make_checker("parallel_num_success == 0")},
   -- Rapid repointing with no refining for high priority targets
   rfsm.trans{src="repointing", tgt="observing", events={"e_done"}, guard=make_checker("target_type == 'Fermi'")},
   rfsm.trans{src="repointing", tgt="observing", events={"e_done"}, guard=make_checker("target_type == 'Swift'")},
   rfsm.trans{src="repointing", tgt="observing", events={"e_done"}, guard=make_checker("target_priority > 1")},
   -- Refine the pointing if not too close to the Pole (as the mounts are not aligned properly)
   rfsm.trans{src="repointing", tgt="repointing_locate", events={"e_done"}, guard=make_checker("target_dec < 80")},
   rfsm.trans{src="repointing", tgt="observing", events={"e_done"}}, -- Disable refining for now

   -- Post-repoint locate
   repointing_locate = make_parallel_state(num_channels,
                                           function(id) return make_channel_message_state(id, "locate") end,
                                          "channel_mount_guard_fn"),
   rfsm.trans{src="repointing_locate", tgt="zero", events={"e_done"}, guard=make_checker("parallel_num_success == 0")},
   rfsm.trans{src="repointing_locate", tgt="repointing_wait", events={"e_done"}},

   -- Wait so mounts have some time to be properly fixed
   repointing_wait = rfsm.state{},
   rfsm.trans{src="repointing_wait", tgt="repointing_refine", events={"e_after(5)"}},

   -- Post-repoint refine
   repointing_refine = make_parallel_state(num_mounts,
                                           function(id) return make_mount_message_state(id, "repoint ra=${target_ra} dec=${target_dec}") end,
                                           "mount_guard_fn"),
   rfsm.trans{src="repointing_refine", tgt="zero", events={"e_done"}, guard=make_checker("parallel_num_success == 0")},
   rfsm.trans{src="repointing_refine", tgt="observing", events={"e_done"}},

   -- Generic observation entry point
   observing = rfsm.state{},

   rfsm.trans{src="observing", tgt="scheduling0", guard=make_checker("need_reschedule")},
   -- 'survey' and 'monitoring' target types lead to monitoring state
   rfsm.trans{src="observing", tgt="monitoring", guard=make_checker("target_type == 'survey'")},
   rfsm.trans{src="observing", tgt="monitoring", guard=make_checker("target_type == 'monitoring'")},
   -- all other types lead to 'imaging' state`
   rfsm.trans{src="observing", tgt="imaging"},

   -- Monitoring
   monitoring = make_monitoring_state("monitoring"),
   rfsm.trans{src=".monitoring.error", tgt="zero"},
   rfsm.trans{src=".monitoring.done", tgt="scheduling0"},

   -- Imaging
   imaging = make_imaging_state("imaging"),
   rfsm.trans{src=".imaging.error", tgt="zero"},
   rfsm.trans{src=".imaging.done", tgt="scheduling0"},

   -- Idle
   waiting = rfsm.state{entry=function() dprint("Waiting"); send_message("set_state waiting") end},
   rfsm.trans{src="waiting", tgt="scheduling0", events={"e_after(10)"}},
}

system_activate = rfsm.state
{
   entry = function() print("System is being activated"); send_message("set_state activating");  log("Activating system") end,

   rfsm.trans{src="initial", tgt="can"},

   -- CAN
   can = rfsm.state{entry=function() send_message_to("can", "set_chillers 1") end},
   rfsm.trans{src="can", tgt="init", events={"e_after(2)"}},

   init = make_parallel_state(num_channels,
                              function(id) return make_channel_init_state(id) end,
                              nil),

   rfsm.trans{src="init", tgt="done", events={"e_done"}},

   done = rfsm.conn{},
}

system_deactivate = rfsm.state
{
   entry = function() print("System is being deactivated"); send_message("set_state deactivating");  log("Deactivating system") end,

   rfsm.trans{src="initial", tgt="reset"},

   -- Reset the channels
   reset = make_parallel_state(num_channels,
                               function(id) return make_channel_message_state(id, {"reset"},  10) end,
                               "channel_guard_fn"),
   rfsm.trans{src="reset", tgt="park", events={"e_done"}},

   -- Park
   park = make_parallel_state(num_mounts,
                              function(id) return make_mount_message_state(id, "park", 60) end,
                              "mount_guard_fn"),
   rfsm.trans{src="park", tgt="done", events={"e_done"}},

   done = rfsm.conn{},
}

system_shutdown = rfsm.state
{
   entry = function() print("System is being deactivated"); send_message("set_state deactivating"); log("Shutting down system") end,

   rfsm.trans{src="initial", tgt="dome"},

   -- Dome
   dome = rfsm.state{entry=function() send_message_to("dome", "close_dome") end},
   rfsm.trans{src="dome", tgt="reset", events={"e_after(1)"}},

   -- Reset the channel
   reset = make_parallel_state(num_channels,
                               function(id) return make_channel_message_state(id, {"reset", "hw set_camera 0"},  10) end,
                               --channel_guard,
                               nil),
   rfsm.trans{src="reset", tgt="park", events={"e_done"}},

   -- Park
   park = make_parallel_state(num_mounts,
                              function(id) return make_mount_message_state(id, "park", 60) end,
                              --mount_guard,
                              nil),
   rfsm.trans{src="park", tgt="restart", events={"e_done"}},

   -- Restart channels
   restart = make_parallel_state(num_channels,
                                 function(id) return rfsm.state
                                    {
                                       rfsm.trans{src="initial", tgt="send_camera"},

                                       send_camera = rfsm.state{entry=function() send_message_to("channel"..id, "hw set_camera 0") end},
                                       rfsm.trans{src="send_camera", tgt="send_reset", events={"e_after(2)"}},

                                       send_reset = rfsm.state{entry=function() send_message_to("channel"..id, "exit") end},
                                       rfsm.trans{src="send_reset", tgt="done"},

                                       done = rfsm.state{},
                                    }
                                 end
   ),
   rfsm.trans{src="restart", tgt="can0", events={"e_done"}},

   -- CAN
   can0 = rfsm.state{},
   rfsm.trans{src="can0", tgt="can", events={"e_after(5)"}},

   can = rfsm.state{entry=function() send_message_to("can", "set_chillers 0") end},
   rfsm.trans{src="can", tgt="done", events={"e_after(2)"}},

   done = rfsm.conn{},
}

system_standby = rfsm.state
{
   entry = function() print("System is on standby") end,

   rfsm.trans{src="initial", tgt="inactive"},

   inactive = rfsm.state {entry=function() print("System is waiting for activity conditions"); send_message("set_state standby"); log("System is on standby") end},
   rfsm.trans{src="inactive", tgt="activate", guard=make_checker("is_night and is_weather_good and is_dome_open")},

   activate = utils.deepcopy(system_activate),
   rfsm.trans{src=".activate.done", tgt="active"},

   active = utils.deepcopy(system_active),
   rfsm.trans{src="active", tgt="shutdown", guard=make_checker("not is_night"),
              effect=function() log("Shutting down due to daytime") end},
   rfsm.trans{src="active", tgt="shutdown", guard=make_checker("not is_weather_good"),
              effect=function() log("Shutting down due to bad weather") end},
   rfsm.trans{src="active", tgt="shutdown", guard=make_checker("not (is_night and is_weather_good and is_dome_open)"),
              effect=function() log("Shutting down due to daytime or bad weather") end},

   deactivate = utils.deepcopy(system_deactivate),
   rfsm.trans{src=".deactivate.done", tgt="inactive"},

   shutdown = utils.deepcopy(system_shutdown),
   -- rfsm.trans{src=".shutdown.done", tgt="inactive"},
   rfsm.trans{src=".shutdown.done", tgt="final"},

   --final = rfsm.state{entry=function() os.execute("echo MMT shutdown|mail -s MMT_state karpov.sv@gmail.com") end},
   final = rfsm.state{entry=function() send_email("MMT shutdown", "beholder: MMT shutdown") end},
}

system_test = rfsm.state
{
   entry = function() print "entry" end,
   exit = function() print "exit" end,

   dbg=rfsmpp.gen_dbgcolor("fsm",
                        { STATE_ENTER=true, STATE_EXIT=true}, false),

   rfsm.trans{src="initial", tgt="init"},

   init = rfsm.state{entry=function() exp=0.03; send_message("set_state test_flats"); end},
   rfsm.trans{src="init", tgt="pre1"},

   pre1 = rfsm.state{entry=function()
                        waittime=exp*100;
                        if(exp < 1) then waittime=250*exp end
                        if(exp < 0.5) then waittime=300*exp end
                        if(exp < 0.2) then waittime=600*exp end
                        if(exp > 2) then waittime=40*exp end
                        if(exp > 3) then waittime=30*exp end
                        if(exp > 5) then waittime=20*exp end
                        if(exp > 10) then waittime=10*exp end
                        --if(exp > 4) then waittime=40*exp end
                        --if(exp > 5) then waittime=30*exp end
                        --if(exp > 6) then waittime=20*exp end
                        --if(exp > 7) then waittime=15*exp end
                        --if(exp > 8) then waittime=10*exp end
                        print(interpolate_globals("exp=${exp} waittime=${waittime}")); end},
   rfsm.trans{src="pre1", tgt="final", guard=make_checker("exp>=100")},
   --rfsm.trans{src="pre1", tgt="pre2"},
   rfsm.trans{src="pre1", tgt="inner1"},

   -- Normalization Exposure
   inner1 = make_message_state("broadcast_channels set_grabber exposure=1"),
   rfsm.trans{src="inner1", tgt="inner2", events={"e_after(10)"}},

   inner2 = make_message_state("broadcast_channels storage_start"),
   rfsm.trans{src="inner2", tgt="inner3", events={"e_after(30)"}},

   inner3 = make_message_state("broadcast_channels storage_stop"),
   rfsm.trans{src="inner3", tgt="pre2", events={"e_after(1)"}},

   -- Work Exposure
   pre2 = make_message_state("broadcast_channels set_grabber exposure=${exp}"),
   rfsm.trans{src="pre2", tgt="wait0", events={"e_after(10)"}},

   wait0 = make_message_state("broadcast_channels storage_start"),
   rfsm.trans{src="wait0", tgt="post1", events={"e_after(${waittime})"}},

   post1 = make_message_state("broadcast_channels storage_stop"),
   rfsm.trans{src="post1", tgt="post2", events={"e_after(1)"}},

   post2 = rfsm.state{entry=function()
                         exp = exp*1.04
                         -- exp = math.pow(10,math.random(math.floor(100*math.log10(0.03)),math.floor(100*math.log10(20)))/100)
                         -- if exp<0.1 then
                         --    exp=exp+0.01
                         -- elseif exp<0.1 then
                         --    exp=exp+0.02
                         -- elseif exp<3 then
                         --    exp=exp+0.1
                         -- elseif exp<6  then
                         --    exp=exp+0.2
                         -- elseif exp<10 then
                         --    exp=exp+0.4
                         -- else
                         --    exp=exp+0.5
                         -- end
                     end},
   rfsm.trans{src="post2", tgt="pre1"},

   final = rfsm.state{entry=function() print "final" end},
}

system_manual = rfsm.state
{
   entry=function() print("manual state") end,

   rfsm.trans{src="initial", tgt="main"},

   -- Main state
   main=rfsm.state{entry=function() send_message("set_state manual") end},

   -- Init
   rfsm.trans{src="main", tgt="init", events={"init", "init_channels"}},
   init = make_parallel_state(num_channels,
                                function(id) return make_channel_init_state(id) end,
                                nil,
                                {entry=function() dprint("Manual init state"); send_message("set_state manual-init"); end}),
   rfsm.trans{src="init", tgt="main", events={"e_done"}},
   rfsm.trans{src="init", tgt="main", events={"stop"}},

   -- Shutdown
   rfsm.trans{src="main", tgt="shutdown", events={"shutdown", "shutdown_channels"}},
   shutdown = utils.deepcopy(system_shutdown),
   rfsm.trans{src=".shutdown.done", tgt="main"},
   rfsm.trans{src="shutdown", tgt="main", events={"stop"}},

   -- Locate
   rfsm.trans{src="main", tgt="locate", events={"locate"}},
   locate = make_parallel_state(num_channels,
                                function(id) return make_channel_message_state(id, "locate") end,
                                "channel_mount_guard_fn",
                                {entry=function() dprint("Manual locate state"); send_message("set_state manual-locate"); end}),
   rfsm.trans{src="locate", tgt="main", events={"e_done"}},
   rfsm.trans{src="locate", tgt="main", events={"stop"}},

   -- Monitoring
   rfsm.trans{src="main", tgt="monitoring_start", events={"monitoring_start"}},
   monitoring_start = make_parallel_state(num_channels,
                                          function(id) return make_channel_message_state(id, {
                                                                                            --"next_target delta_ra=0 delta_dec=0 filter=${target_filter_monitoring} type=survey name=field",
                                                                                            "next_target delta_ra=${target_delta_ra["..id.."]} delta_dec=${target_delta_dec["..id.."]} filter=${target_filter_monitoring} type=survey name=field",
                                                                                            "monitoring_start"
                                          }) end,
                                          "channel_guard_fn"),
   rfsm.trans{src="monitoring_start", tgt="main", events={"e_done"}},

   rfsm.trans{src="main", tgt="monitoring_stop", events={"monitoring_stop"}},
   monitoring_stop = make_parallel_state(num_channels,
                                          function(id) return make_channel_message_state(id, "monitoring_stop") end,
                                          "channel_guard_fn"),
   rfsm.trans{src="monitoring_stop", tgt="main", events={"e_done"}},

   -- Imaging
   rfsm.trans{src="main", tgt="imaging", events={"imaging"}},
   imaging = make_imaging_state("manual-imaging"),
   rfsm.trans{src="imaging", tgt="main", events={"e_done"}},
   rfsm.trans{src="imaging", tgt="main", events={"stop"}},

   -- 3x3 channels
   rfsm.trans{src="main", tgt="expand", events={"expand", "expand33"}},
   expand = make_parallel_state(num_channels,
                                function(id) return make_channel_message_state(id, {
                                                                                  "set_mirror delta_ra=${target_delta_ra["..id.."]} delta_dec=${target_delta_dec["..id.."]}"
                                }) end,
                                nil);
   rfsm.trans{src="expand", tgt="main", events={"e_done"}},
   rfsm.trans{src="expand", tgt="main", events={"stop"}},

   -- 3x1 channels
   rfsm.trans{src="main", tgt="expand31", events={"expand31"}},
   expand31 = make_parallel_state(num_channels,
                                function(id) return make_channel_message_state(id, {
                                                                                  "set_mirror delta_ra=${target_delta_ra_31["..id.."]} delta_dec=${target_delta_dec_31["..id.."]}"
                                }) end,
                                nil);
   rfsm.trans{src="expand31", tgt="main", events={"e_done"}},
   rfsm.trans{src="expand31", tgt="main", events={"stop"}},

   -- 1x3 channels
   rfsm.trans{src="main", tgt="expand13", events={"expand13"}},
   expand13 = make_parallel_state(num_channels,
                                function(id) return make_channel_message_state(id, {
                                                                                  "set_mirror delta_ra=${target_delta_ra_13["..id.."]} delta_dec=${target_delta_dec_13["..id.."]}"
                                }) end,
                                nil);
   rfsm.trans{src="expand13", tgt="main", events={"e_done"}},
   rfsm.trans{src="expand13", tgt="main", events={"stop"}},

   -- Single field
   rfsm.trans{src="main", tgt="collapse", events={"collapse"}},
   collapse = make_parallel_state(num_channels,
                                function(id) return make_channel_message_state(id, {
                                                                                  "set_mirror delta_ra=0 delta_dec=0"
                                }) end,
                                nil);
   rfsm.trans{src="collapse", tgt="main", events={"e_done"}},
   rfsm.trans{src="collapse", tgt="main", events={"stop"}},

   -- Combinations of filters, 3x1
   rfsm.trans{src="main", tgt="filters_31", events={"filters_31"}},
   filters_31 = make_parallel_state(num_channels,
                                    function(id) return make_channel_message_state(id, "hw set_filters name=${target_filter_31["..id.."]}") end,
                                    nil);
   rfsm.trans{src="filters_31", tgt="main", events={"e_done"}},
   rfsm.trans{src="filters_31", tgt="main", events={"stop"}},

   -- Combinations of filters, 1x3
   rfsm.trans{src="main", tgt="filters_13", events={"filters_13"}},
   filters_13 = make_parallel_state(num_channels,
                                    function(id) return make_channel_message_state(id, "hw set_filters name=${target_filter_13["..id.."]}") end,
                                    nil);
   rfsm.trans{src="filters_13", tgt="main", events={"e_done"}},
   rfsm.trans{src="filters_13", tgt="main", events={"stop"}},

   -- Combinations of filters, 1x1
   rfsm.trans{src="main", tgt="filters_11", events={"filters_11"}},
   filters_11 = make_parallel_state(num_channels,
                                    function(id) return make_channel_message_state(id, "hw set_filters name=${target_filter_11["..id.."]}") end,
                                    nil);
   rfsm.trans{src="filters_11", tgt="main", events={"e_done"}},
   rfsm.trans{src="filters_11", tgt="main", events={"stop"}},


   -- test
   make_test = utils.deepcopy(system_test),
   --rfsm.state {entry=function() print("manual state"); send_message("set_state manual"); end},
   rfsm.trans{src="main", tgt="make_test", events={"make_test"}},
   rfsm.trans{src="make_test", tgt="main", events={"stop"}},
   rfsm.trans{src=".make_test.final", tgt="main"},
}

-- Global state
global = rfsm.state
{
   dbg = rfsmpp.gen_dbgcolor("beholder", { STATE_ENTER=true, STATE_EXIT=true}, false),

   --dbg = false,
   info = false,
   warn = false,

   rfsm.trans{src="initial", tgt="worker"},

   worker = rfsm.state
   {
      rfsm.trans{src="initial", tgt="manual"},

      -- Manual state, no activities, safe to control the channels manually
      manual = utils.deepcopy(system_manual),
      --rfsm.state {entry=function() print("manual state"); send_message("set_state manual"); end},
      rfsm.trans{src="manual", tgt="standby", events={"start"}},

      -- Standby state, waiting for activity conditions
      -- standby = utils.deepcopy(system_active),
      standby = utils.deepcopy(system_standby),
      --rfsm.trans{src="standby", tgt="deactivate", events={"stop"}},
      rfsm.trans{src="standby", tgt="manual", events={"stop"}},
      rfsm.trans{src=".standby.final", tgt="manual"},

      deactivate = utils.deepcopy(system_deactivate),
      rfsm.trans{src=".deactivate.done", tgt="manual"},

      deactivate_for_error = utils.deepcopy(system_deactivate),
      rfsm.trans{src=".deactivate_for_error.done", tgt="error"},

      -- Post-error idle state
      error = rfsm.state {entry=function() print("post-error idle state"); send_message("set_state error"); end},
      rfsm.trans{src="error", tgt="manual", events={"start", "stop"}},
   },

   -- If something goes wrong with messages - lets restart everything
   -- rfsm.trans{src="worker", tgt=".worker.deactivate_for_error", events={"e_message_timeout"}},

   -- Reboot
   rfsm.trans{src="worker", tgt="reboot", events={"reboot"}},
   reboot = rfsm.state{entry=function() os.execute("sudo reboot") end},
   rfsm.trans{src="reboot", tgt="worker"},

   -- Shutdown
   rfsm.trans{src="worker", tgt="shutdown", events={"shutdown_central"}},
   shutdown = rfsm.state{entry=function() os.execute("sudo shutdown -h now") end},
   rfsm.trans{src="shutdown", tgt="worker"},

   -- Rescheduling handling - internal transition
   rfsm.trans{src="worker", tgt="internal", events={"reschedule", "scheduler___reschedule"}, effect=function()
                 if get_message_arg('priority', 1000)*1 > target_priority then
                    need_reschedule = True
                    log("Need rescheduling due to external request with priority " .. get_message_arg('priority', 1000))
                 else
                    log("Skipping rescheduling due to external request with priority " .. get_message_arg('priority', 1000))
                 end
   end},
}

fsm = rfsm.init(global)

print("\n")
print("Beholder script loaded")
print("num_channels = "..num_channels)
print("num_mounts = "..num_mounts)
print("\n")

is_running = true
rfsm.run(fsm)
is_running = false
