require "rfsm"

-- Import some common FSM functions from common.lua
require "common"
require "focus"

-- accum_length = 100
-- accom_type = "dark"

accum_use_progress = true

is_focused = false
is_hw_connected = false

-- C code state variables
realtime_state = 0
raw_state = 0
grabber_state = 0

-- These will be already set by C code
-- has_ii = false
-- has_focus2 = false

channel_error_guard = make_checker("grabber_state == -1 or realtime_state == -1 or raw_state == -1")

-- Collecting dark frames
make_accum_image_state = function()
   return rfsm.state
   {
      entry = function()
         accumulated = 0
         dprint("accumulating " .. accum_type .. ": " .. accum_length .. " frames")
         send_message("accum_image_prepare")
      end,

      exit = function()
         if accum_use_progress then send_message("set_progress 0") end
      end,

      rfsm.trans{src="initial", tgt="waiting"},

      waiting = rfsm.state{},
      rfsm.trans{src="waiting", tgt="got_image", events={"image_acquired"}},

      got_image = rfsm.state{entry =
                             function()
                                send_message("accum_image_append")
                                accumulated = accumulated + 1
                                if accum_use_progress then send_message("set_progress " .. accumulated/accum_length) end
                                dprint("accumulating: " .. accumulated .. " / " .. accum_length)
                             end},
      rfsm.trans{src="got_image", tgt="store", guard = make_checker("accumulated >= accum_length")},
      rfsm.trans{src="got_image", tgt="waiting"},

      -- FIXME: we are stopping the acquisition so BitFlow grabber will not crash our kernel
      store0 = make_message_state("grabber_stop", nil, 5),
      rfsm.trans{src=".store0.done", tgt="store"},

      store = rfsm.state{entry =
                         function()
                            -- These calls are synchronous
                            if accum_type == "file" then
                               send_message("accum_image_to_file filename=" .. accum_filename)
                            else
                               send_message("accum_image_to_" .. accum_type)
                            end
                            dprint("accumulating " .. accum_type .." done")
                         end},

      -- FIXME: here we are starting the acquisition again
      rfsm.trans{src="store", tgt="done"},
      store1 = make_message_state("grabber_start", nil, 5),

      rfsm.trans{src=".store1.done", tgt="done"},

      -- 'Goog' ending
      done = rfsm.conn {},
   }
end

-- Events to be deferred while re-configuring the system
events_to_defer_on_configure = {}
make_configure_state = function(args)
   local is_ii = args.ii or false
   local is_processing = args.processing or false
   local is_storage = args.storage or false
   local is_lamp = args.lamp or false

   -- filter and mirror should be string, and may contain values to be interpolated
   local filter = args.filter or "Clear"
   local mirror = args.mirror or "delta_ra=0 delta_dec=0"

   local is_grabber
   -- Values that should be TRUE by default (as nil and false are both false, you can't use 'or' here)
   if(args.grabber ~= nil) then
      is_grabber = args.grabber
   else
      is_grabber = true
   end

   local is_cover
   if(args.cover ~= nil) then
      is_cover = args.cover
   else
      is_cover = true
   end

   local messages = {}

   -- TODO: perform these steps in parallel
   messages[#messages+1] = {is_cover and "set_cover 1" or "set_cover 0", "hw", 10}
   messages[#messages+1] = {"set_filters name=" .. filter, "hw", 5}
   messages[#messages+1] = {is_lamp and "set_lamp 1" or "set_lamp 0", "hw", 5}
   messages[#messages+1] = {"set_mirror " .. mirror , "hw", 5}

   -- Storage and processing are being turned on after the hardware changes, and off - before
   if is_storage then table.insert(messages, "storage_start") else table.insert(messages, 1, "storage_stop") end
   if is_processing then table.insert(messages, "processing_start") else table.insert(messages, 1, "processing_stop") end
   -- II and grabber are being turned on before the storage/processing, and off - after
   if has_ii then
      if is_ii then table.insert(messages, 1, {"set_ii_power 1", "hw", 10}) else table.insert(messages, {"set_ii_power 0", "hw", 10}) end
   end
   if is_grabber then table.insert(messages, 1, "grabber_start") else table.insert(messages, "grabber_stop") end

   state = make_multi_message_state(messages)

   state.entry = function() defer_events(events_to_defer_on_configure) end
   state.exit = function() undefer_events(events_to_defer_on_configure) end

   return state
end

system_dark = rfsm.state
{
   entry = function()
      accum_type = "dark"
      accum_length = 1000
      accum_use_progress = true
      send_message("set_state write_darks")
   end,
   exit = function() send_message("state_done write_darks") end,

   rfsm.trans{src="initial", tgt="init"},

   init = make_configure_state{ii=false, cover=false},
   rfsm.trans{src=".init.done", tgt="accum"},

   accum = make_accum_image_state(),
   rfsm.trans{src=".accum.done", tgt="done"},

   done = rfsm.state {},
}

system_flat = rfsm.state
{
   entry = function()
      accum_type = "flat"
      accum_length = 100
      accum_use_progress = true
      send_message("set_state write_flats")
   end,
   exit = function() send_message("state_done write_flats") end,

   rfsm.trans{src="initial", tgt="init"},

   init = make_configure_state{ii=true, lamp=false, cover=true},
   rfsm.trans{src=".init.done", tgt="accum"},

   accum = make_accum_image_state(),
   rfsm.trans{src=".accum.done", tgt="final"},

   final = make_configure_state{ii=false, lamp=false, cover=true},
   rfsm.trans{src=".final.done", tgt="done"},

   done = rfsm.state {},
}

system_skyflat = rfsm.state
{
   entry = function()
      accum_type = "skyflat"
      accum_length = 100
      accum_use_progress = true
      send_message("set_state write_flats")
   end,
   exit = function() send_message("state_done write_flats") end,

   rfsm.trans{src="initial", tgt="accum"},

   init = make_configure_state{ii=true, lamp=false, cover=true},
   rfsm.trans{src=".init.done", tgt="accum"},

   accum = make_accum_image_state(),
   rfsm.trans{src=".accum.done", tgt="done"},

   final = make_configure_state{ii=false, lamp=false, cover=true},
   rfsm.trans{src=".final.done", tgt="done"},

   done = rfsm.state {},
}

make_autofocus_seq_state = function(id)
   id = id or 0

   return rfsm.state
   {
      entry = function()
         -- These are global variables!
         focus_min = focus_min or 0
         focus_max = focus_max or 4096
         focus_step = focus_step or 100
         focus_x = {}
         focus_y = {}
         focus = focus_min;

         accum_type = "focus"
         accum_length = 10
         accum_use_progress = false

         dprint("Guessing rough focus position: "..focus_min.." "..focus_max.." "..focus_step.." "..focus) end,
      exit = function() send_message("set_progress 0"); end,

      rfsm.trans{src="initial", tgt="init"},

      init = make_message_state("set_focus pos=${focus_min} id="..id, "hw", 10),
      rfsm.trans{src=".init.done", tgt="step1"},

      step1 = make_accum_image_state(), -- We already set arguments in entry()
      rfsm.trans{src=".step1.done", tgt="step2"},

      step2 = rfsm.state{entry =
                         function()
                            dprint("focus="..focus.." quality="..focus_quality);
                            focus_x[#focus_x+1] = focus;
                            focus_y[#focus_y+1] = focus_quality;
                            send_message("set_progress " .. (focus - focus_min)/(focus_max - focus_min));
                         end},
      rfsm.trans{src="step2", tgt="step3", guard=make_checker("focus + focus_step <= focus_max")},
      rfsm.trans{src="step2", tgt="done"},

      step3 = rfsm.state{entry=function() focus = focus + focus_step end},
      rfsm.trans{src="step3", tgt="step4"},
      step4 = make_message_state("move_focus id="..id.." shift=${focus_step}", "hw", 5),
      rfsm.trans{src=".step4.done", tgt="step1"},

      done = rfsm.state {},
   }
end

make_autofocus_motor_state = function(id)
   id = id or 0

   return rfsm.state
   {
      rfsm.trans{src="initial", tgt="init"},
      -- rfsm.trans{src="initial", tgt="filters_B", effect=function() focus_pos = 3620; focus_pos_Clear = focus_pos end},

      -- Reset focus
      init = make_message_state("reset_focus id="..id, "hw", 10),
      rfsm.trans{src=".init.done", tgt="guess"},

      -- First, rough step
      guess = rfsm.state{entry=function()
                            print("focus " .. id)
                            if id == 0 then
                               focus_min = 0;
                               focus_max = 4096;
                               focus_step = 100;
                            else
                               focus_min = 0;
                               focus_max = 200;
                               focus_step = 20;
                            end
                        end},
      rfsm.trans{src="guess", tgt="guess1"},

      guess1 = make_autofocus_seq_state(id),
      rfsm.trans{src=".guess1.done", tgt="guess2"},

      guess2 = rfsm.state{entry=function()
                             save(focus_x, focus_y, "focus-1.txt");
                             focus_pos = find_maxpos(focus_x, focus_y);
                             dprint("Max pos="..focus_pos);
                             if id == 0 then
                                focus_min = math.max(0, focus_pos - 200);
                                focus_max = math.min(4096, focus_pos + 200);
                                focus_step = 10;
                             else
                                focus_min = math.max(0, focus_pos - 60);
                                focus_max = math.min(200, focus_pos + 60);
                                focus_step = 5;
                             end
                                end},
      rfsm.trans{src="guess2", tgt="measure1"},

      -- Second, finer step
      measure1 = make_autofocus_seq_state(id),
      rfsm.trans{src=".measure1.done", tgt="measure2"},

      measure2 = rfsm.state{entry=function()
                               save(focus_x, focus_y, "focus-2.txt");
                               focus_pos = find_maxpos(focus_x, focus_y);
                               dprint("Max pos="..focus_pos);
                               if id == 0 then
                                  focus_min = math.max(0, focus_pos - 30);
                                  focus_max = math.min(4096, focus_pos + 30);
                                  focus_step = 1;
                               else
                                  focus_min = math.max(0, focus_pos - 10);
                                  focus_max = math.min(200, focus_pos + 10);
                                  focus_step = 1;
                               end
                                  end},
      --rfsm.trans{src="measure2", tgt="set1"},
      rfsm.trans{src="measure2", tgt="refine1"},

      -- Third, finest step
      refine1 = make_autofocus_seq_state(id),
      rfsm.trans{src=".refine1.done", tgt="refine2"},

      refine2 = rfsm.state{entry=function()
                               save(focus_x, focus_y, "focus-3.txt");
                               focus_pos = round(find_maxpos_precise(focus_x, focus_y));
                               dprint("Max pos="..focus_pos);
                                  end},
      rfsm.trans{src="refine2", tgt="set", guard=function()
                    if id == 0 then return false; else return true; end end},
      rfsm.trans{src="refine2", tgt="filters_B", effect=function() focus_pos_Clear = focus_pos end},

      filters_B = make_autofocus_filter_state("B"),
      rfsm.trans{src=".filters_B.done", tgt="filters_V", effect=function() focus_pos_B = focus_pos end},

      filters_V = make_autofocus_filter_state("V"),
      rfsm.trans{src=".filters_V.done", tgt="filters_R", effect=function() focus_pos_V = focus_pos end},

      filters_R = make_autofocus_filter_state("R"),
      rfsm.trans{src=".filters_R.done", tgt="filters_remember", effect=function() focus_pos_R = focus_pos; focus_pos = focus_pos_Clear end},

      filters_remember = make_multi_message_state({"remember_focus clear=${focus_pos_Clear} b=${focus_pos_B} v=${focus_pos_V} r=${focus_pos_R}"}, "hw"),
      rfsm.trans{src=".filters_remember.done", tgt="set"},

      set = make_multi_message_state({"set_focus pos=${focus_pos} id=" .. id}, "hw"),
      rfsm.trans{src=".set.done", tgt="done"},

      done = rfsm.state {},
   }
end

make_autofocus_filter_state = function(filter)
   return rfsm.state {
      rfsm.trans{src="initial", tgt="prepare"},

      prepare = make_configure_state{ii=true, filter=filter},
      rfsm.trans{src=".prepare.done", tgt="measure1", effect=function()
                    focus_min = math.max(0, focus_pos - 200);
                    focus_max = math.min(4096, focus_pos + 200);
                    focus_step = 10;
      end},

      measure1 = make_autofocus_seq_state(0),
      rfsm.trans{src=".measure1.done", tgt="measure2", effect=function()
                    save(focus_x, focus_y, "focus-"..filter.."-1.txt");
                    focus_pos = find_maxpos(focus_x, focus_y);
                    dprint("Max pos="..focus_pos);
                    focus_min = math.max(0, focus_pos - 30);
                    focus_max = math.min(4096, focus_pos + 30);
                    focus_step = 1;
      end},

      measure2 = make_autofocus_seq_state(0),
      rfsm.trans{src=".measure2.done", tgt="finish", effect=function()
                    save(focus_x, focus_y, "focus-"..filter.."-2.txt");
                    focus_pos = round(find_maxpos_precise(focus_x, focus_y));
                    dprint("Max pos="..focus_pos);
      end},

      finish = make_configure_state{ii=true, filter="Clear"},
      rfsm.trans{src=".finish.done", tgt="done"},

      done = rfsm.state{},
   }
end

-- Autofocus entry state
system_autofocus = rfsm.state
{
   entry = function() send_message("set_state autofocus"); pos = 0; end,
   exit = function() send_message("state_done autofocus") end,

   -- rfsm.trans{src="initial", tgt="done", guard=make_checker("is_focused")},
   rfsm.trans{src="initial", tgt="init"},

   init = make_configure_state{ii=true, filter="Clear"},
   rfsm.trans{src=".init.done", tgt="focus1"},

   focus1 = make_autofocus_motor_state(0),
   rfsm.trans{src=".focus1.done", tgt="done", guard=make_checker("not has_focus2")},
   rfsm.trans{src=".focus1.done", tgt="focus2"},

   focus2 = make_autofocus_motor_state(1),
   rfsm.trans{src=".focus2.done", tgt="done"},

   done = rfsm.state {},
}

system_autofocus0 = rfsm.state
{
   entry = function() send_message("set_state autofocus"); pos = 0; end,
   exit = function() send_message("state_done autofocus") end,

   rfsm.trans{src="initial", tgt="init"},

   init = make_configure_state{ii=true},
   rfsm.trans{src=".init.done", tgt="focus1"},

   focus1 = make_autofocus_motor_state(0),
   rfsm.trans{src=".focus1.done", tgt="done"},

   done = rfsm.state {},
}

system_monitoring = rfsm.state
{
   entry = function()
      -- dprint(interpolate_globals("Monitoring: ra=${target_ra} dec=${target_dec} filter=${target_filter}"));
      send_message("set_state monitoring");
   end,
   exit = function()
      send_message("state_done monitoring")
      send_message("command_done monitoring_stop")
   end,

   rfsm.trans{src="initial", tgt="init"},

   init = make_configure_state{ii=true, storage=true, processing=true, filter="${target_filter}", mirror="${target_mirror0} ${target_mirror1}"},
   rfsm.trans{src=".init.done", tgt="wait"},

   wait = rfsm.state{entry=function() send_message("command_done monitoring_start") end},
   rfsm.trans{src="wait", tgt="finalize", events={"monitoring_stop"}},

   finalize = make_configure_state{ii=false, storage=false, processing=false},
   rfsm.trans{src=".finalize.done", tgt="done"},

   done = rfsm.state {},
}

system_followup = rfsm.state
{
   entry = function()
      dprint(interpolate_globals("Observing: name=${target_name} type=${target_type} id=${target_id} filter=${target_filter} exposure=${target_exposure} repeat=${target_repeat} mirror=${target_mirror0} ${target_mirror1}"));
      accum_type = "db type=target"
      accum_length = target_repeat
      accum_use_progress = true

      send_message("set_state followup")
   end,
   exit = function() send_message("state_done followup") end,

   rfsm.trans{src="initial", tgt="init"},

   init = make_configure_state{ii=true, storage=true, filter="${target_filter}", mirror="${target_mirror0} ${target_mirror1}"},
   rfsm.trans{src=".init.done", tgt="accum"},

   accum = make_accum_image_state(),
   rfsm.trans{src=".accum.done", tgt="done"},

   done = rfsm.state {},
}

system_imaging = rfsm.state
{
   entry = function()
      log("Imaging: name=${target_name} type=${target_type} id=${target_id} filter=${target_filter} exposure=${target_exposure} repeat=${target_repeat} mirror=${target_mirror0} ${target_mirror1}")
      send_message("set_state imaging")
      send_message("set_progress 0")

      if target_filter:find("Pol") == 3 then
         target_filter_pre = target_filter:sub(1,1)
      elseif target_filter:find("Pol") == 1 then
         target_filter_pre = "Clear"
      else
         target_filter_pre = nil
      end
   end,
   exit = function()
      --- We will reset the grabber here, hoping that it will process
      --- these (and following) commands sequentially
      send_message("grabber_stop")
      send_message("set_grabber exposure=0.1")
      send_message("grabber_start")
      send_message("set_progress 0")
      send_message("state_done imaging")
   end,

   rfsm.trans{src="initial", tgt="init0"},

   init0 = rfsm.state{},

   rfsm.trans{src="init0", tgt="init_pre", guard=make_checker("target_filter_pre ~= nil")},
   rfsm.trans{src="init0", tgt="init"},

   --- Pre imaging
   init_pre = make_configure_state{ii=true, filter="${target_filter_pre}", mirror="${target_mirror0} ${target_mirror1}"},
   rfsm.trans{src=".init_pre.done", tgt="init_grabber_pre"},

   init_grabber_pre = make_multi_message_state({"grabber_stop",
                                                "set_grabber exposure=${target_exposure} count=1",
                                                "accum_image_prepare",
                                                "grabber_start"}),
   rfsm.trans{src=".init_grabber_pre.done", tgt="acquire_pre"},

   acquire_pre = rfsm.state{},
   rfsm.trans{src="acquire_pre", tgt="store_pre", events={"image_acquired"}},

   store_pre = make_multi_message_state({"accum_image_append",
                                         "accum_image_to_db type=${target_type} count=0"}),
   rfsm.trans{src=".store_pre.done", tgt="init", effect=function() send_message("set_progress 0"); end},

   --- Main imaging
   init = make_configure_state{ii=true, filter="${target_filter}", mirror="${target_mirror0} ${target_mirror1}"},
   rfsm.trans{src=".init.done", tgt="init_grabber"},

   init_grabber = make_multi_message_state({"grabber_stop",
                                            "set_grabber exposure=${target_exposure} count=${target_repeat}",
                                            "accum_image_prepare"}),
   rfsm.trans{src=".init_grabber.done", tgt="accum"},

   accum = rfsm.state{entry=function() send_message("grabber_start"); count=0 end},
   rfsm.trans{src="accum", tgt="acquire"},

   acquire = rfsm.state{},
   rfsm.trans{src="acquire", tgt="store", events={"image_acquired"}},

   store = make_multi_message_state({"accum_image_append",
                                     "accum_image_to_db type=${target_type} count=${count}"}),
   rfsm.trans{src=".store.done", tgt="acquire", guard=make_checker("count + 1 < target_repeat"), effect=function() count=count+1; send_message("accum_image_prepare"); send_message("set_progress " .. count/target_repeat) end},
   rfsm.trans{src=".store.done", tgt="done", effect=function() send_message("set_progress 0"); end},

   done = rfsm.state {},
}

system_locate = rfsm.state
{
   entry = function()
      send_message("set_state locate")
      accum_type = "locate"
      accum_length = 10
      accum_use_progress = false
   end,
   exit = function()
      send_message("state_done locate")
   end,

   rfsm.trans{src="initial", tgt="init"},

   -- We should do it in zero celostate position
   init = make_configure_state{ii=true, storage=false, processing=false, mirror="0 0"},
   rfsm.trans{src=".init.done", tgt="accum"},

   accum = make_accum_image_state(),
   rfsm.trans{src=".accum.done", tgt="done"},

   done = rfsm.state {},
}

--- Celostate calibration
system_calibrate_celostate = rfsm.state
{
   entry = function()
      send_message("set_state calibrate")
      accum_length = 10
      accum_use_progress = false

      mirror_pos = -120
      mirror_step = 10
      mirror_progress = 0
   end,

   rfsm.trans{src="initial", tgt="init"},

   -- Get central pos
   init = make_configure_state{ii=true, storage=false, processing=false, mirror="0 0"},
   rfsm.trans{src=".init.done", tgt="measure0", effect=function()
                 accum_type = "getpos"
                 mirror_poses={}
                 mirror_dists={}
                 mirror_delta_ra = {}
                 mirror_delta_dec = {}
   end},

   measure0 = make_accum_image_state(),
   rfsm.trans{src=".measure0.done", tgt="set1", effect=function() accum_type = "getdist" end},

   -- First celostate
   set1 = make_configure_state{ii=true, storage=false, processing=false, mirror="${mirror_pos} 0"},
   rfsm.trans{src=".set1.done", tgt="measure1", effect=function() send_message("set_progress "..mirror_progress) end},

   measure1 = make_accum_image_state(),
   rfsm.trans{src=".measure1.done", tgt="process1"},

   process1 = rfsm.state{entry=function()
                            mirror_poses[#mirror_poses+1] = mirror_pos
                            mirror_dists[#mirror_dists+1] = sky_distance
                            mirror_delta_ra[#mirror_delta_ra+1] = sky_delta_ra
                            mirror_delta_dec[#mirror_delta_dec+1] = sky_delta_dec
                            mirror_pos = mirror_pos + mirror_step
                            mirror_progress = mirror_progress + mirror_step/480
                        end},
   rfsm.trans{src="process1", tgt="set1", guard=make_checker("mirror_pos <= 120")},
   rfsm.trans{src="process1", tgt="set2", effect=function()
                 save4(mirror_poses, mirror_dists, mirror_delta_ra, mirror_delta_dec, "celostate1.txt");
                 mirror_poses = {}
                 mirror_dists = {}
                 mirror_delta_ra = {}
                 mirror_delta_dec = {}
                 mirror_pos = -120
   end},

   -- Second celostate
   set2 = make_configure_state{ii=true, storage=false, processing=false, mirror="0 ${mirror_pos}"},
   rfsm.trans{src=".set2.done", tgt="measure2", effect=function() send_message("set_progress "..mirror_progress) end},

   measure2 = make_accum_image_state(),
   rfsm.trans{src=".measure2.done", tgt="process2"},

   process2 = rfsm.state{entry=function()
                            mirror_poses[#mirror_poses+1] = mirror_pos
                            mirror_dists[#mirror_dists+1] = sky_distance
                            mirror_delta_ra[#mirror_delta_ra+1] = sky_delta_ra
                            mirror_delta_dec[#mirror_delta_dec+1] = sky_delta_dec
                            mirror_pos = mirror_pos + mirror_step
                            mirror_progress = mirror_progress + mirror_step/480
                        end},
   rfsm.trans{src="process2", tgt="set2", guard=make_checker("mirror_pos <= 120")},
   rfsm.trans{src="process2", tgt="reset", effect=function()
                 save4(mirror_poses, mirror_dists, mirror_delta_ra, mirror_delta_dec, "celostate2.txt")
   end},

   -- Reset celostate pos
   reset = make_configure_state{ii=true, storage=false, processing=false, mirror="0 0"},
   rfsm.trans{src=".reset.done", tgt="done", effect=function() send_message("set_progress 0") end},

   done = rfsm.state{},
}

-- Normal state
system_normal = rfsm.state
{
   entry = function()
      dprint("normal state")
      send_message("set_state normal")
   end,

   -- rfsm.trans{src="initial", tgt="init"},

   -- init = make_configure_state{ii=false, storage=false, processing=false},
}

-- List of events to be deferred during the reset state
events_to_defer_on_reset = {"autofocus", "autofocus0", "write_darks", "write_flats",
                            "followup", "locate", "preview"}
system_reset = rfsm.state
{
   entry = function() send_message("set_state reset"); defer_events(events_to_defer_on_reset) end,
   exit = function() send_message("command_done reset") undefer_events(events_to_defer_on_reset) end,

   rfsm.trans{src="initial", tgt="init"},

   init = rfsm.state{},

   rfsm.trans{src="init", tgt="config", guard=make_checker("is_hw_connected")},

   config = make_configure_state{ii=false, processing=false, storage=false, cover=false},

   rfsm.trans{src=".config.done", tgt="done"},

   done = rfsm.state{},
}

-- Global state
global = rfsm.state
{
   -- dbg = false,
   dbg = rfsmpp.gen_dbgcolor("channel", { STATE_ENTER=true, STATE_EXIT=true}, false),
   info = false,
   warn = false,

   rfsm.trans{src="initial", tgt="main"},

   main = rfsm.state{
      rfsm.trans{src="initial", tgt="normal"},

      -- Main state
      normal = utils.deepcopy(system_normal),

      -- Dark
      rfsm.trans{src="normal", tgt="dark", events={"write_darks"}},
      dark = utils.deepcopy(system_dark),
      rfsm.trans{src=".dark.done", tgt="normal"},

      -- Flat
      rfsm.trans{src="normal", tgt="flat", events={"write_flats"}},
      flat = utils.deepcopy(system_skyflat),
      rfsm.trans{src=".flat.done", tgt="normal"},

      -- Autofocus
      rfsm.trans{src="normal", tgt="autofocus", events={"autofocus"}},
      autofocus = utils.deepcopy(system_autofocus),
      rfsm.trans{src=".autofocus.done", tgt="normal"},

      rfsm.trans{src="normal", tgt="autofocus0", events={"autofocus0"}},
      autofocus0 = utils.deepcopy(system_autofocus0),
      rfsm.trans{src=".autofocus0.done", tgt="normal"},

      -- Calibrate celostate
      rfsm.trans{src="normal", tgt="calibrate_celostate", events={"calibrate_celostate"}},
      calibrate_celostate = utils.deepcopy(system_calibrate_celostate),
      rfsm.trans{src=".calibrate_celostate.done", tgt="normal"},

      -- Monitoring
      -- rfsm.trans{src="normal", tgt="monitoring", events={"monitoring_start"}},
      -- monitoring = utils.deepcopy(system_monitoring),
      -- rfsm.trans{src="monitoring", tgt="followup", events={"followup"}},
      -- rfsm.trans{src=".monitoring.done", tgt="normal"},

      -- Monitoring start
      rfsm.trans{src="normal", tgt="monitoring_start", events={"monitoring_start"}},
      monitoring_start = rfsm.state{
         entry = function()
            log("Monitoring started: ra=${target_ra} dec=${target_dec} filter=${target_filter}");
         end,
         exit = function()
            send_message("command_done monitoring_start")
         end,

         rfsm.trans{src="initial", tgt="init"},

         init = make_configure_state{ii=true, storage=true, processing=true, filter="${target_filter}", mirror="${target_mirror0} ${target_mirror1}"},
         rfsm.trans{src=".init.done", tgt="done"},

         done = rfsm.state{},
      },
      rfsm.trans{src=".monitoring_start.done", tgt="normal"},

      -- Monitoring stop
      rfsm.trans{src="normal", tgt="monitoring_stop", events={"monitoring_stop"}},
      rfsm.trans{src="monitoring_start", tgt="monitoring_stop", events={"monitoring_stop"}},
      monitoring_stop = rfsm.state{
         entry = function()
         end,
         exit = function()
            log("Monitoring stopped");
            send_message("command_done monitoring_stop")
         end,

         rfsm.trans{src="initial", tgt="init"},

         init = make_configure_state{ii=false, storage=false, processing=false},

         rfsm.trans{src=".init.done", tgt="done"},

         done = rfsm.state{},
      },
      rfsm.trans{src=".monitoring_stop.done", tgt="normal"},

      -- Followup
      rfsm.trans{src="normal", tgt="followup", events={"followup"}},
      followup = utils.deepcopy(system_followup),
      rfsm.trans{src=".followup.done", tgt="normal"},

      -- Imaging
      rfsm.trans{src="normal", tgt="imaging", events={"imaging"}},
      imaging = utils.deepcopy(system_imaging),
      rfsm.trans{src=".imaging.done", tgt="normal"},

      -- Locate the current pointing
      rfsm.trans{src="normal", tgt="locate", events={"locate"}},
      locate = utils.deepcopy(system_locate),
      rfsm.trans{src=".locate.done", tgt="normal"},

      -- Preview
      rfsm.trans{src="normal", tgt="preview", events={"preview"}},
      preview = make_configure_state{ii=true, processing=false, storage=false},
      rfsm.trans{src=".preview.done", tgt="normal"},
   },

   rfsm.trans{src="main", tgt="main", events={"stop"}},

   -- Init
   rfsm.trans{src="main", tgt="init", events={"reset"}},
   init = utils.deepcopy(system_reset),
   rfsm.trans{src="init", tgt="error", events={"e_message_timeout"}},
   rfsm.trans{src=".init.done", tgt="main"},

   -- Error
   rfsm.trans{src="main", tgt="error", events={"e_message_timeout", "error"}, effect=function() print("Transition due to message") end},
   rfsm.trans{src="main", tgt="error", guard=channel_error_guard, effect=function() print("Transition due to guard") end},
   error = rfsm.state{entry=function() send_message("set_state error"); send_message("processing_stop"); send_message("storage_stop") end},
   rfsm.trans{src="error", tgt="init", events={"reset"}},
   rfsm.trans{src="error", tgt="main", events={"stop"}},
   rfsm.trans{src="error", tgt="reboot", events={"reboot"}},
   rfsm.trans{src="error", tgt="shutdown", events={"shutdown"}},

   -- Reboot
   rfsm.trans{src="main", tgt="reboot", events={"reboot"}},
   reboot = rfsm.state{entry=function() os.execute("sudo reboot") end},
   rfsm.trans{src="reboot", tgt="error", events={"e_message_timeout"}},
   rfsm.trans{src="reboot", tgt="main"},

   -- Shutdown
   rfsm.trans{src="main", tgt="shutdown", events={"shutdown"}},
   shutdown = rfsm.state{entry=function() os.execute("sudo shutdown -h now") end},
   rfsm.trans{src="shutdown", tgt="error", events={"e_message_timeout"}},
   rfsm.trans{src="shutdown", tgt="main"},
}

-- return global

fsm = rfsm.init(global)

is_running = true
rfsm.run(fsm)
is_running = false
