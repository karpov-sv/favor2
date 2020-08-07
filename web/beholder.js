Beholder = function(parent_id){
    var tmp;
    var panel = $("<div/>", {class:"panel panel-default"});

    document.title = "MMT Central Control";

    var header = $("<div/>", {class:"panel-heading"}).appendTo(panel);
    var title = $("<h3/>", {class:"panel-title"}).html("MMT Central Control ").appendTo(header);
    this.throbber = $("<span/>", {class:"glyphicon glyphicon-refresh pull-right"}).appendTo(title);
    this.connstatus = $("<span/>", {class:"label label-default"}).appendTo(title);

    this.body = $("<div/>", {class:"panel-body"}).appendTo(panel);

    /* Status */
    var list = $("<div/>", {class:"list-inline"}).appendTo(this.body);

    /* Beholder state */
    tmp = $("<p/>", {class:"list-group-item clearfix"}).appendTo(list);
    this.state = $("<span/>", {class:"list-group-item-text"}).appendTo(tmp);
    var controls = $("<span/>", {class:"btn-group pull-right"}).appendTo(tmp);
    this.makeButton("Automatic mode:").appendTo(controls);
    this.automatic_start = this.makeButton("Start", "start").appendTo(controls);
    this.automatic_stop = this.makeButton("Stop", "stop").appendTo(controls);

    var manual = $("<span/>", {class:"pull-right"}).appendTo(tmp);

    /* Scheduler state */
    tmp = $("<p/>", {class:"list-group-item clearfix"}).appendTo(list);
    this.scheduler = $("<span/>", {class:"list-group-item-text"}).appendTo(tmp);
    this.scheduler_controls = $("<span/>", {class:"btn-group pull-right"}).appendTo(tmp);
    $("<button>", {class:"btn btn-default", title:"Set or move Point of Interest"}).html("Set POI").click($.proxy(this.interestNew, this)).appendTo(this.scheduler_controls);
    $("<button>", {class:"btn btn-default", title:"Clear Point of Interest"}).html("Clear POI").click($.proxy(this.interestDelete, this)).appendTo(this.scheduler_controls);
    this.makeButton("Reschedule", "reschedule").appendTo(this.scheduler_controls);
    this.scheduler_fake_controls = $("<span/>", {class:"btn-group pull-right"}).appendTo(tmp);
    this.makeButton("Set Night", "set_param night=1").appendTo(this.scheduler_fake_controls);
    this.makeButton("Set Day", "set_param night=0").appendTo(this.scheduler_fake_controls);
    this.scheduler_targets_controls = $("<span/>", {class:"btn-group pull-right"}).appendTo(tmp);
    this.makeButton("Targets:").appendTo(this.scheduler_targets_controls);
    this.makeButton("Enable", "command_scheduler add_mode mode=targets").appendTo(this.scheduler_targets_controls);
    this.makeButton("Disable", "command_scheduler remove_mode mode=targets").appendTo(this.scheduler_targets_controls);

    /* Weather state */
    tmp = $("<p/>", {class:"list-group-item clearfix"}).appendTo(list);
    this.weather = $("<span/>", {class:"list-group-item-text"}).appendTo(tmp);
    this.weather_fake_controls = $("<span/>", {class:"btn-group pull-right"}).appendTo(tmp);
    this.makeButton("Set Weather Good", "set_param weather_good=1").appendTo(this.weather_fake_controls);
    this.makeButton("Set Weather Bad", "set_param weather_good=0").appendTo(this.weather_fake_controls);

    /* Dome state */
    tmp = $("<p/>", {class:"list-group-item clearfix"}).appendTo(list);
    this.dome = $("<span/>", {class:"list-group-item-text"}).appendTo(tmp);
    this.dome_fake_controls = $("<span/>", {class:"btn-group pull-right"}).appendTo(tmp);
    this.makeButton("Set Dome Open", "set_param dome_open=1").appendTo(this.dome_fake_controls);
    this.makeButton("Set Dome Closed", "set_param dome_open=0").appendTo(this.dome_fake_controls);

    this.dome_controls = $("<span/>", {class:"btn-group pull-right"}).appendTo(tmp);
    this.makeButton("Dome:").appendTo(this.dome_controls);
    this.dome_controls_inner = $("<span/>", {class:"btn-group"}).appendTo(this.dome_controls);
    this.makeButton("Open", "command_dome open_dome").appendTo(this.dome_controls_inner);
    this.makeButton("Stop", "command_dome stop_dome").appendTo(this.dome_controls_inner);
    this.makeButton("Close", "command_dome close_dome").appendTo(this.dome_controls_inner);
    this.makeButton("Dehum Chan:").appendTo(this.dome_controls_inner);
    this.makeButton("On", "command_dome start_dehum_channels").appendTo(this.dome_controls_inner);
    this.makeButton("Off", "command_dome stop_dehum_channels").appendTo(this.dome_controls_inner);
    this.makeButton("Dehum Dome:").appendTo(this.dome_controls_inner);
    this.makeButton("On", "command_dome start_dehum_dome").appendTo(this.dome_controls_inner);
    this.makeButton("Off", "command_dome stop_dehum_dome").appendTo(this.dome_controls_inner);
    this.makeButton("Vent:").appendTo(this.dome_controls_inner);
    this.makeButton("On", "command_dome start_vent").appendTo(this.dome_controls_inner);
    this.makeButton("Off", "command_dome stop_vent").appendTo(this.dome_controls_inner);
    this.dome_controls_inner_button = this.makeButton(">>", this.toggleDomeControls, "Show/hide control buttons").appendTo(this.dome_controls);
    this.dome_controls_inner.hide();

    /* CAN state */
    tmp = $("<p/>", {class:"list-group-item clearfix"}).appendTo(list);
    this.can_line = tmp;
    this.can = $("<span/>", {class:"list-group-item-text"}).appendTo(tmp);
    this.can_controls = $("<span/>", {class:"btn-group pull-right"}).appendTo(tmp);
    this.makeButton("Chillers:").appendTo(this.can_controls);
    this.can_chillers_on = this.makeButton("On", "command_can set_chillers power=1").appendTo(this.can_controls);
    this.can_chillers_off = this.makeButton("Off", "command_can set_chillers power=0").appendTo(this.can_controls);
    this.can_restart = $("<span/>", {class:"btn-group pull-right"}).appendTo(tmp);
    // this.makeButton("Restart", "command_can exit").appendTo(this.can_restart);

    /* Manual mode controls */
    this.manual_controls = $("<ul/>", {class:"list-group-item clearfix list-inline"}).appendTo(list);

    tmp = $("<li/>", {class:"btn-group"}).appendTo(this.manual_controls);
    this.makeButton("Global:").appendTo(tmp);
    this.makeButton("Reset MOXAs", "reset_moxas").appendTo(tmp);
    this.makeButton("Init Channels", "init_channels").appendTo(tmp);
    this.makeButton("Shutdown", "shutdown").appendTo(tmp);

    tmp = $("<li/>", {class:"btn-group"}).appendTo(this.manual_controls);
    this.makeButton("Regimes:").appendTo(tmp);
    this.makeButton("Monitoring Start", "monitoring_start").appendTo(tmp);
    this.makeButton("Monitoring Stop", "broadcast_channels monitoring_stop").appendTo(tmp);
    this.makeButton("Imaging", this.targetImaging).appendTo(tmp);

    tmp = $("<li/>", {class:"btn-group"}).appendTo(this.manual_controls);
    this.makeButton("Channels:").appendTo(tmp);
    // this.makeButton("Preview", "broadcast_channels preview").appendTo(tmp);
    this.makeButton("Darks", "broadcast_channels write_darks").appendTo(tmp);
    this.makeButton("Flats", "broadcast_channels write_flats").appendTo(tmp);
    this.makeButton("Autofocus", "broadcast_channels autofocus").appendTo(tmp);
    this.makeButton("Locate", "broadcast_odd_channels locate").appendTo(tmp);
    this.makeButton("Reset", "broadcast_channels reset").appendTo(tmp);
    this.makeButton("Stop", "broadcast_channels stop").appendTo(tmp);
    this.makeButton("Restart", "broadcast_channels exit").appendTo(tmp);

    tmp = $("<li/>", {class:"btn-group"}).appendTo(this.manual_controls);
    this.makeButton("FOV:").appendTo(tmp);
    this.makeButton("3x3", "expand").appendTo(tmp);
    this.makeButton("3x1", "expand31").appendTo(tmp);
    this.makeButton("1x3", "expand13").appendTo(tmp);
    this.makeButton("1x1", "collapse").appendTo(tmp);

    tmp = $("<li/>", {class:"btn-group"}).appendTo(this.manual_controls);
    this.makeButton("Processing Start", "broadcast_channels processing_start").appendTo(tmp);
    this.makeButton("Processing Stop", "broadcast_channels processing_stop").appendTo(tmp);
    this.makeButton("Storage Start", "broadcast_channels storage_start").appendTo(tmp);
    this.makeButton("Storage Stop", "broadcast_channels storage_stop").appendTo(tmp);

    tmp = $("<li/>", {class:"btn-group"}).appendTo(this.manual_controls);
    this.makeButton("Low-level:").appendTo(tmp);
    this.makeButton("Open", "broadcast_channels hw set_cover 1").appendTo(tmp);
    this.makeButton("Close", "broadcast_channels hw set_cover 0").appendTo(tmp);
    this.makeButton("Camera On", "broadcast_channels hw set_camera 1").appendTo(tmp);
    this.makeButton("Camera Off", "broadcast_channels hw set_camera 0").appendTo(tmp);
    this.makeButton("Flatfield On", "broadcast_channels hw set_lamp 1").appendTo(tmp);
    this.makeButton("Flatfield Off", "broadcast_channels hw set_lamp 0").appendTo(tmp);
    this.makeButton("Reset Focus", "broadcast_channels hw reset_focus").appendTo(tmp);

    tmp = $("<li/>", {class:"btn-group"}).appendTo(this.manual_controls);
    this.makeButton("Filters:").appendTo(tmp);
    this.makeButton("Clear", "broadcast_channels hw set_filters 0").appendTo(tmp);
    this.makeButton("B", "broadcast_channels hw set_filters name=B").appendTo(tmp);
    this.makeButton("V", "broadcast_channels hw set_filters name=V").appendTo(tmp);
    this.makeButton("R", "broadcast_channels hw set_filters name=R").appendTo(tmp);
    this.makeButton("Pol", "broadcast_channels hw set_filters name=Pol").appendTo(tmp);

    tmp = $("<li/>", {class:"btn-group"}).appendTo(this.manual_controls);
    // this.makeButton("Faint", "filters_faint").appendTo(tmp);
    // this.makeButton("Medium", "filters_medium").appendTo(tmp);
    // this.makeButton("Bright", "filters_bright").appendTo(tmp);
    this.makeButton("BVR 3x1", "filters_31").appendTo(tmp);
    this.makeButton("BVR 1x3", "filters_13").appendTo(tmp);
    this.makeButton("BVR+Pol", "filters_11").appendTo(tmp);

    tmp = $("<li/>", {class:"btn-group"}).appendTo(this.manual_controls);
    this.makeButton("Mounts:").appendTo(tmp);
    this.makeButton("Start Tracking", "broadcast_mounts tracking_start").appendTo(tmp);
    this.makeButton("Stop Tracking", "broadcast_mounts tracking_stop").appendTo(tmp);
    this.makeButton("Repoint", this.targetRepoint).appendTo(tmp);
    this.makeButton("Stop!", "broadcast_mounts stop").appendTo(tmp);
    this.makeButton("Park", "broadcast_mounts park").appendTo(tmp);
    this.makeButton("Restart", "broadcast_mounts exit").appendTo(tmp);

    tmp = $("<li/>", {class:"btn-group"}).appendTo(this.manual_controls);
    this.makeButton("<< RA", "broadcast_mounts set_motors 1 0").appendTo(tmp);
    this.makeButton("<< Dec", "broadcast_mounts set_motors 0 1").appendTo(tmp);
    this.makeButton("Stop", "broadcast_mounts stop").appendTo(tmp);
    this.makeButton("Dec >>", "broadcast_mounts set_motors 0 -1").appendTo(tmp);
    this.makeButton("RA >>", "broadcast_mounts set_motors -1 0").appendTo(tmp);

    this.cmdline = $("<input>", {type:"text", size:"40", class:"form-control"});

    $("<div/>", {class:"input-group"}).append($("<span/>", {class:"input-group-addon"}).html("Command:")).append(this.cmdline).appendTo(this.body);

    this.cmdline.pressEnter($.proxy(function(event){
        this.sendCommand(this.cmdline.val());
        event.preventDefault();
    }, this));

    /* Channels */
    this.nchannels = 0;
    this.nmounts = 0;

    this.channels = [];
    this.mounts = [];

    this.channelsdiv = $("<div/>", {class:""}).appendTo($("<div/>", {class:"well well"}).appendTo(this.body));
    this.mountsdiv = $("<div/>", {class:""}).appendTo($("<div/>", {class:"well well"}).appendTo(this.body));

    this.misc = $("<div/>", {class:""}).appendTo($("<div/>").appendTo(this.body));

    this.sky_image = $("<img/>", {src:"/beholder/sky", class:"image img-responsive center-block"}).appendTo(this.misc);
    new Updater(this.sky_image, 5000);

    var footer = $("<div/>", {class:"panel-footer"});//.appendTo(panel);

    var form = $("<form/>").appendTo(footer);
    // this.throbber = $("<span/>", {class:"glyphicon glyphicon-transfer"}).appendTo(form);
    this.delayValue = 2000;
    this.delay = $("<select/>", {id:getUUID()});
    $("<label>", {for: this.delay.get(0).id}).html("Refresh:").appendTo(form);
    $("<option>", {value: "1000"}).html("1").appendTo(this.delay);
    $("<option>", {value: "2000", selected:1}).html("2").appendTo(this.delay);
    $("<option>", {value: "5000"}).html("5").appendTo(this.delay);
    $("<option>", {value: "10000"}).html("10").appendTo(this.delay);
    this.delay.appendTo(form);
    this.delay.change($.proxy(function(event){
        this.delayValue = this.delay.val();
    }, this));

    panel.appendTo(parent_id);

    this.requestState();
}

Beholder.prototype.requestState = function(){
    $.ajax({
        url: "/beholder/status",
        dataType : "json",
        timeout : 1000,
        context: this,

        success: function(json){
            this.throbber.animate({opacity: 1.0}, 200).animate({opacity: 0.1}, 400);

            /* Crude hack to prevent jumping */
            var st = document.body.scrollTop;
            var sl = document.body.scrollLeft;
            this.updateStatus(json.beholder_connected, json.beholder);
            document.body.scrollTop = st;
            document.body.scrollLeft = sl;
        },

        complete: function( xhr, status ) {
            setTimeout($.proxy(this.requestState, this), this.delayValue);
        }
    });
}

Beholder.prototype.sendCommand = function(command){
    $.ajax({
        url: "/beholder/command",
        data: {string: command}
    });
}

Beholder.prototype.updateStatus = function(connected, status){
    if(connected){
        show(this.body);

        if(status['state'].substring(0, 6) == 'manual' && status['state'] != 'manual-init'){
            show(this.manual_controls);
            enable(this.automatic_start);
            disable(this.automatic_stop);
        } else if(status['state'].substring(0, 5) != 'error'){
            hide(this.manual_controls);
            disable(this.automatic_start);
            enable(this.automatic_stop);
        } else {
            /* Error state */
            show(this.manual_controls);
            enable(this.automatic_start);
            enable(this.automatic_stop);
        }

        this.connstatus.html("Connected");
        this.connstatus.removeClass("label-danger").addClass("label-success");

        this.state.html("State: " + label(status['state']) + " : ");

        if(status['is_night'] == 1)
            $("<span/>", {class: "label label-success"}).html("Night").appendTo(this.state);
        else
            $("<span/>", {class: "label label-danger"}).html("Daytime").appendTo(this.state);

        if(status['is_weather_good'] == 1)
            $("<span/>", {class: "label label-success"}).html("Weather good").appendTo(this.state);
        else
            $("<span/>", {class: "label label-danger"}).html("Weather bad").appendTo(this.state);

        if(status['is_dome_open'] == 1)
            $("<span/>", {class: "label label-success"}).html("Dome opened").appendTo(this.state);
        else
            $("<span/>", {class: "label label-danger"}).html("Dome closed").appendTo(this.state);

        if(status['state'].substring(0, 6) == 'manual')
            $("<span/>", {class: "label label-danger"}).html("Manual").appendTo(this.state);
        else
            $("<span/>", {class: "label label-success"}).html("Automatic").appendTo(this.state);

        if(status['state'].substring(0, 5) == 'error')
            $("<span/>", {class: "label label-danger"}).html("Error").appendTo(this.state);

        /* Scheduler */
        if(status['scheduler'] == 0){
            hide(this.scheduler_controls);
            show(this.scheduler_fake_controls);
            this.scheduler.html("Scheduler: " + label("Disconnected", "danger"));
        } else {
            show(this.scheduler_controls);
            hide(this.scheduler_fake_controls);
            var state = "Scheduler: " + label("Connected", "success") + " Zsun: " + label(status['scheduler_zsun']) +
                " Zmoon: " + label(status['scheduler_zmoon']);

            if(status['scheduler_modes'].split(',').indexOf('targets') < 0)
                state += " " + label("No Targets", "danger");

            state += " Suggested: " + label(status['scheduler_suggestion_type']);

            if(status['scheduler_suggestion_type'] != 'survey')
                state += " Name: " + label(status['scheduler_suggestion_name']);
            state += " RA: " + label(parseFloat(status['scheduler_suggestion_ra']).toFixed(2)) + " Dec: " + label(parseFloat(status['scheduler_suggestion_dec']).toFixed(2));
            if(status['scheduler_suggestion_type'] != 'survey'){
                state += " Filter: " + label(status['scheduler_suggestion_filter']);
                state += " Exposure: ";
                if(status['scheduler_suggestion_repeat'] == '1')
                    state += label(status['scheduler_suggestion_exposure']);
                else
                    state += label(status['scheduler_suggestion_exposure'] + ' x ' + status['scheduler_suggestion_repeat']);
            }

            this.scheduler.html(state);
        }

        /* Weather */
        if(status['weather'] == 0){
            show(this.weather_fake_controls);
            this.weather.html("Weather: " + label("Disconnected", "danger"));
        } else {
            var cloud_states = {0:"Clouds Unknown", 1:"Clear", 2:"Cloudy", 3:"Very Cloudy"};
            var wind_states = {0:"Wind Unknown", 1:"Calm", 2:"Windy", 3:"Very Windy"};
            var rain_states = {0:"Rain Unknown", 1:"Dry", 2:"Wet", 3:"Rain"};
            var day_states = {0:"Brightness Unknown", 1:"Dark", 2:"Bright", 3:"Very Bright"};
            var state_colors = {0:"info", 1:"success", 2:"warning", 3:"danger"};

            hide(this.weather_fake_controls);
            this.weather.html("Weather: " + label("Connected", "success") + " " +
                              " " + label(cloud_states[status['weather_cloud_cond']], state_colors[status['weather_cloud_cond']]) +
                              " " + label(wind_states[status['weather_wind_cond']], state_colors[status['weather_wind_cond']]) +
                              " " + label(rain_states[status['weather_rain_cond']], state_colors[status['weather_rain_cond']]) +
                              " Temperature: " + label(status['weather_ambient_temp']) +
                              " Sky-Ambient: " + label(status['weather_sky_ambient_temp']) +
                              " Wind: " + label(status['weather_wind']) +
                              " Humidity: " + label(status['weather_humidity']) +
                              " Brightness: " + label(status['weather_brightness']) +
                              " " + label(day_states[status['weather_day_cond']], state_colors[status['weather_day_cond']]) +
                              "<a href='"+this.meteoLink()+"' target='meteo' class='pull-right' style='opactiy: 50%' title='Open Meteo Archive'><span class='glyphicon glyphicon-fullscreen'></span></a>"
                             );
        }

        /* Dome */
        if(status['dome'] == 0){
            hide(this.dome_controls);
            show(this.dome_fake_controls);
            this.dome.html("Dome: " + label("Disconnected", "danger"));
        } else {
            hide(this.dome_fake_controls);
            show(this.dome_controls);
            var states = {0:"Closed", 1:"Opening", 2:"Closing", 3:"Open"};
            var state_colors = {0:"warning", 1:"warning", 2:"warning", 3:"success"};
            var onoff_colors = {0:"warning", 1:"success"};

            var sidewall_states = {0: "Blocked", 1:"Opened", 2:"Closed", 3:"Moving", 4:"Error"};
            var sidewall_colors = {0: "danger", 1:"success", 2:"warning", 3:"info", 4:"danger"};

            var handrail_states = {0: "Blocked", 1:"Down", 2:"Up", 3:"Moving", 4:"Error"};
            var handrail_colors = {0: "danger", 1:"warning", 2:"success", 3:"info", 4:"danger"};

            var dome_states = {0: "Wait", 1:"Block", 2:"Alarm", 3:"Alarm", 4:"Open Side", 5:"Open Side", 6:"Close Side", 7:"Down Hand", 8:"Up Hand", 9:"Down Hand", 10:"Up Hand", 11:"Opening", 12:"Closing", 13:"Stopping", 14:"Stopping"};
            var dome_results = {0: "Unknown", 1:"Opening", 2:"Closing", 3:"Opened", 4:"Closed", 5:"Block", 6:"HW Err", 7:"485 Err", 8:"Stop", 9:"Side Open Timeout", 10:"Side Close Timeout", 11:"Hand Down Timeout", 12:"Hand Up Timeout", 13:"Roof Open Timeout", 14:"Roof Close Timeout"};

            string = "Dome: " + label("Connected", "success") +
                " " + label(states[status['dome_state']], state_colors[status['dome_state']]);

            if(status['dome_remote'] == '1'){
                string += " " + label(dome_states[status['dome_dome_state']], "primary", "state=" + status['dome_dome_state']) +
                    " " + label(dome_results[status['dome_dome_result']], "primary", "result=" + status['dome_dome_result'])
            } else {
                string += " " + label("Manual", "danger");
            }

            string += " Dehum: " + label("Channels", onoff_colors[status['dome_dehum_channels']]) +
                " " + label("Dome", onoff_colors[status['dome_dehum_dome']]) +
                " " + label("Vent", onoff_colors[status['dome_vent_engineering']]) +
                " Sidewall: " + label(sidewall_states[status['dome_sidewall']], sidewall_colors[status['dome_sidewall']]) +
                " Handrail: " + label(handrail_states[status['dome_handrail']], handrail_colors[status['dome_handrail']]) +
            " ";

            this.dome.html(string);
        }

        /* CAN */
        if(status['can'] == 0){
            hide(this.can_line);
        } else {
            show(this.can_line);

            if(status['can_connected'] == 0) {
                hide(this.can_controls);
                this.can.html("CAN: " + label("Disconnected", "danger"));
            } else {
                var state = "CAN: " + label("Connected", "success");

                show(this.can_controls);

                for(id = 1; id <= status['can_nchillers']; id++){
                    var cstate = status['can_chiller'+id+'_state'];
                    var params = "";

                    params += "Cold: " + status['can_chiller'+id+'_t_cold'];
                    params += " Hot: " + status['can_chiller'+id+'_t_hot'];
                    params += " Chip: " + status['can_chiller'+id+'_t_chip'];

                    if(cstate == 0)
                        state += " " + label('C'+id, 'success', params);
                    else if(cstate == 1)
                        state += " " + label('C'+id, 'warning', params);
                    else
                        state += " " + label('C'+id+':'+cstate, 'danger', params);
                }

                this.can.html(state);

                // if(status['can_chillers_state'] == 0){
                //     enable(this.can_chillers_on);
                //     disable(this.can_chillers_off);
                // } else if(status['can_chillers_state'] == 1){
                //     disable(this.can_chillers_on);
                //     enable(this.can_chillers_off);
                // } else {
                //     enable(this.can_chillers_on);
                //     enable(this.can_chillers_off);
                // }
            }
        }

        /* Channels and mounts */
        this.updateChannels(status);
        this.updateMounts(status);
    } else {
        hide(this.body);

        this.connstatus.html("Disconnected");
        this.connstatus.removeClass("label-success").addClass("label-danger");
    }
}

Beholder.prototype.renderChannels = function(){
    this.channelsdiv.html("");
    for(y = 0; y < Math.ceil(this.nchannels/3); y++){
        var row = $("<div/>", {class: "row"}).appendTo(this.channelsdiv);

        for(x = 0; x < 3; x++){
            var id = y*3+x + 1;
            var col = $("<div/>", {class: "col-md-4", style:"padding:1px"}).appendTo(row);

            if(id <= this.nchannels){
                this.channels[id] = {};
                var div = $("<div/>", {class:"panel panel-default", style:"margin-bottom: 5px"}).appendTo(col);
                var header = $("<div/>", {class:"panel-heading", style:"padding-top: 5px; padding-bottom: 5px"}).appendTo(div);
                var title = $("<h3/>", {class:"panel-title"}).html("Channel " + id + " ").appendTo(header);
                this.channels[id].connstatus = $("<span/>", {class:"label label-default"}).appendTo(title);
                $("<a/>", {href:this.channelLink(id), target:"channel"+id, class:"pull-right", title:"Open Channel Interface"}).append(
                    $("<span/>", {class:"glyphicon glyphicon-fullscreen"}).css({opacity:"0.5"})).appendTo(title);

                var body = $("<div/>", {class:"panel-body", style:"padding: 1px; margin: 1px"}).appendTo(div);
                this.channels[id].body = body;

                this.channels[id].progressdiv = $("<div/>", {class:"progress"}).appendTo(body);
                this.channels[id].progress = $("<div/>", {class:"progress-bar", style:"width: 0%"}).appendTo(this.channels[id].progressdiv);

                this.channels[id].image = $("<img/>", {src:"/beholder/small_frame.jpg?id=" + id, class:"image img-responsive center-block"}).appendTo(body);
                new Updater(this.channels[id].image, this.delayValue);

                var list = $("<ul/>", {class:"list-group"}).appendTo(div);
                this.channels[id].list = list;
                this.channels[id].state = $("<li/>", {class: "list-group-item", style:"padding: 5px"}).appendTo(list);
                this.channels[id].coords = $("<li/>", {class: "list-group-item", style:"padding: 5px"}).appendTo(list);
                // this.channels[id].hw = $("<li/>", {class: "list-group-item", style:"padding: 5px"}).appendTo(list);
            }
        }
    }
}

Beholder.prototype.updateChannels = function(status){
    var states = {0:"Normal", 1:"Darks", 2:"Autofocus", 3:"Monitoring", 4:"Follow-up", 5:"Flats", 6:"Locate", 7:"Reset", 8:"Calibrate", 9:"Imaging", 100:"Error"};
    var types = {0:"success", 1:"success", 2:"info", 3:"info", 4:"info", 5:"info", 6:"warning", 7:"warning", 8:"warning", 100:"danger"}

    if(this.nchannels != status['nchannels']){
        this.nchannels = status['nchannels'];
        this.renderChannels();
    }

    var d;

    for(d = 1; d <= this.nchannels; d++){
        var connected = status['channel'+d] == 1;

        if(connected){
            show(this.channels[d].body);
            show(this.channels[d].list);

            this.channels[d].connstatus.html("Connected");
            this.channels[d].connstatus.removeClass("label-danger").addClass("label-success");

            this.channels[d].state.html("State: " + label(states[status['channel'+d+'_state']], types[status['channel'+d+'_state']]) + " ");

            var coords = "RA: " + label(status['channel'+d+'_ra']);

            coords += " Dec: " + label(status['channel'+d+'_dec']);
            coords += " Filter: " + label(status['channel'+d+'_hw_filter']);
            coords += " Mirror: " + label(status['channel'+d+'_hw_mirror0'] + " " + status['channel'+d+'_hw_mirror1']);

            var params = "";

            if(status['channel'+d+'_andor'] == '1' && status['channel'+d+'_grabber'] != "-1"){
                var cooling = {0:"Off", 1:"Stabilized", 2:"Cooling", 3:"Drift", 4:"Not stabilized", 5:"Fault"};

                params += " Andor Temp: " + status['channel'+d+'_andor_temperature'] + ", " + cooling[status['channel'+d+'_andor_temperaturestatus']] + "\n";
            }

            params += " Camera Temp: " + status['channel'+d+'_hw_camera_temperature'] + " Hum:" + status['channel'+d+'_hw_camera_humidity'] + "\n";
            params += " Celostate Temp: " + status['channel'+d+'_hw_celostate_temperature'] + " Hum:" + status['channel'+d+'_hw_celostate_humidity'] + "\n";
            params += "\n";
            params += " Exposure: " + status['channel'+d+'_andor_exposure'] + "\n";
            params += " Image Mean: " + status['channel'+d+'_image_mean'] + " Std:" + status['channel'+d+'_image_std'] + "\n";
            params += " Image Median: " + status['channel'+d+'_image_median'] + " MAD:" + status['channel'+d+'_image_mad'];

            if(params != "")
                coords += "<span class='glyphicon glyphicon-info-sign pull-right' title='" + params + "'></span>"

            this.channels[d].coords.html(coords);

            if(status['channel'+d+'_hw_cover']==1)
                $("<span/>", {class: "label label-success"}).html("Open").appendTo(this.channels[d].state);
            if(status['channel'+d+'_hw_cover']==-1)
                $("<span/>", {class: "label label-danger"}).html("Cover").appendTo(this.channels[d].state);
            if(status['channel'+d+'_has_ii']==1 && status['channel'+d+'_hw_ii_power']==1)
                $("<span/>", {class: "label label-success"}).html("II").appendTo(this.channels[d].state);
            if(status['channel'+d+'_hw_lamp']==1)
                $("<span/>", {class: "label label-danger"}).html("FF").appendTo(this.channels[d].state);
            if(status['channel'+d+'_grabber']==-1)
                $("<span/>", {class: "label label-danger"}).html("Grabber").appendTo(this.channels[d].state);
            if(status['channel'+d+'_grabber']==0 || status['channel'+d+'_grabber']==2)
                $("<span/>", {class: "label label-warning"}).html("Grabber: " + status['channel'+d+'_last_frame_age']).appendTo(this.channels[d].state);

            if(status['channel'+d+'_processing']==1)
                $("<span/>", {class: "label label-primary"}).html("Processing").appendTo(this.channels[d].state);
            if(status['channel'+d+'_storage']==1)
                $("<span/>", {class: "label label-info"}).html("Storage").appendTo(this.channels[d].state);
            if(status['channel'+d+'_is_focused']==0)
                $("<span/>", {class: "label label-danger"}).html("Unfocused").appendTo(this.channels[d].state);
            if(status['channel'+d+'_hw']==0 || status['channel'+d+'_hw_connected']==0)
                $("<span/>", {class: "label label-danger"}).html("HW").appendTo(this.channels[d].state);
            if(status['channel'+d+'_hw_camera']==0)
                $("<span/>", {class: "label label-danger"}).html("Camera").appendTo(this.channels[d].state);
            if(status['channel'+d+'_realtime_queue'] > 50)
                $("<span/>", {class: "label label-danger", title:status['channel'+d+'_realtime_queue']}).html("Realtime").appendTo(this.channels[d].state);
            if(status['channel'+d+'_secondscale_queue'] > 10)
                $("<span/>", {class: "label label-danger", title:status['channel'+d+'_secondscale_queue']}).html("Secondscale").appendTo(this.channels[d].state);
            if(status['channel'+d+'_raw_queue'] > 50)
                $("<span/>", {class: "label label-danger", title:status['channel'+d+'_raw_queue']}).html("RAW").appendTo(this.channels[d].state);

            if(status['channel'+d+'_hw_camera_temperature'] > 35)
                $("<span/>", {class: "label label-danger"}).html("Overheating").appendTo(this.channels[d].state);

            if(status['channel'+d+'_hw_camera_humidity'] > 80)
                $("<span/>", {class: "label label-danger"}).html("Humidity").appendTo(this.channels[d].state);
            else if(status['channel'+d+'_hw_camera_humidity'] > 75)
                $("<span/>", {class: "label label-warning"}).html("Humidity").appendTo(this.channels[d].state);

            if(status['channel'+d+'_progress'] > 0){
                if(this.channels[d].progressdiv.is(":hidden"))
                    this.channels[d].progressdiv.slideDown();

                this.channels[d].progress.css("width", 100*status['channel'+d+'_progress']+"%");
            } else {
                if(this.channels[d].progressdiv.is(":visible"))
                    this.channels[d].progressdiv.slideUp();
            }

        } else {
            hide(this.channels[d].body);
            hide(this.channels[d].list);

            this.channels[d].connstatus.html("Disconnected");
            this.channels[d].connstatus.removeClass("label-success").addClass("label-danger");
        }
    }
}

Beholder.prototype.renderMounts = function(){
    this.mountsdiv.html("");
    for(y = 0; y < Math.ceil(this.nmounts/3); y++){
        var row = $("<div/>", {class: "row"}).appendTo(this.mountsdiv);

        for(x = 0; x < 3; x++){
            var id = y*3+x + 1;
            var col = $("<div/>", {class: "col-md-4", style:"padding:1px"}).appendTo(row);

            if(id <= this.nmounts){
                this.mounts[id] = {};
                var div = $("<div/>", {class:"panel panel-default", style:"margin-bottom: 5px"}).appendTo(col);
                var header = $("<div/>", {class:"panel-heading", style:"padding-top: 5px; padding-bottom: 5px"}).appendTo(div);
                var title = $("<h3/>", {class:"panel-title"}).html("Mount " + id + " ").appendTo(header);
                this.mounts[id].connstatus = $("<span/>", {class:"label label-default"}).appendTo(title);
                $("<a/>", {href:this.mountLink(id), target:"mount"+id, class:"pull-right", title:"Open Mount Interface"}).append(
                    $("<span/>", {class:"glyphicon glyphicon-fullscreen"}).css({opacity:"0.5"})).appendTo(title);

                // var body = $("<div/>", {class:"panel-body"}).appendTo(div);
                // this.mounts[id].body = body;
                var list = $("<ul/>", {class:"list-group"}).appendTo(div);

                this.mounts[id].list = list;
                this.mounts[id].state = $("<li/>", {class: "list-group-item", style:"padding: 5px"}).appendTo(list);
                this.mounts[id].coords = $("<li/>", {class: "list-group-item", style:"padding: 5px"}).appendTo(list);
            }
        }
    }
}

Beholder.prototype.updateMounts = function(status){
    var states = {"-1":"Error", 0:"Idle", 1:"Slewing", 2:"Tracking", 3:"Tracking", 4:"Manual"};
    var types = {"-1":"danger", 0:"success", 1:"info", 2:"warning", 3:"warning", 4:"danger"}

    if(this.nmounts != status['nmounts']){
        this.nmounts = status['nmounts'];
        this.renderMounts();
    }

    var d;

    for(d = 1; d <= this.nmounts; d++){
        var connected = status['mount'+d] == 1;

        if(connected){
            // show(this.mounts[d].body);
            show(this.mounts[d].list);

            this.mounts[d].connstatus.html("Connected");
            this.mounts[d].connstatus.removeClass("label-danger").addClass("label-success");

            if(status['mount'+d+'_connected'] == 1){
                this.mounts[d].state.html("State: " + label(states[status['mount'+d+'_state']], types[status['mount'+d+'_state']]) + " ");
                if(status['mount'+d+'_calibrated'] == 0)
                    $("<span/>", {class: "label label-danger"}).html("Uncalibrated").appendTo(this.mounts[d].state);
                this.mounts[d].coords.html("RA: " + label(status['mount'+d+'_ra']) + " Dec: " + label(status['mount'+d+'_dec']));
            } else {
                this.mounts[d].state.html("State: " + label("HW Disconnected", "danger") + " ");
                this.mounts[d].coords.html("");
            }
        } else {
            // hide(this.mounts[d].body);
            hide(this.mounts[d].list);

            this.mounts[d].connstatus.html("Disconnected");
            this.mounts[d].connstatus.removeClass("label-success").addClass("label-danger");
        }
    }
}

Beholder.prototype.makeButton = function(text, command, title)
{
    if(typeof(command) == "string")
        return $("<button>", {class:"btn btn-default", title:title}).html(text).click($.proxy(function(event){
            this.sendCommand(command);
            event.preventDefault();
        }, this));
    else if(typeof(command) == "function")
        return $("<button>", {class:"btn btn-default", title:title}).html(text).click($.proxy(command, this));
    else
        return $("<button>", {class:"btn btn-default disabled", title:title}).html(text);
}

Beholder.prototype.channelLink = function(id)
{
    return "/channels/" + id;

    // if(window.location.hostname.indexOf('rokos') == 0)
    //     return "http://"+window.location.hostname+":600"+id+"/channel";
    // else
    //     return "http://192.168.16.14"+id+"/channel";
}

Beholder.prototype.mountLink = function(id)
{
    return "/mounts/" + id;

    // if(window.location.hostname.indexOf('rokos') == 0)
    //     return "http://"+window.location.hostname+":600"+(id*2-1)+"/mount";
    // else
    //     return "http://192.168.16.14"+(id*2-1)+"/mount";
}

Beholder.prototype.meteoLink = function()
{
    if(window.location.hostname.indexOf('rokos') == 0)
        return "http://allsky.sonarh.ru:8000/";
    else
        return "http://192.168.16.132/";
}

Beholder.prototype.interestNew = function()
{
    bootbox.prompt("Point of Interest coordinates", $.proxy(function(result){
        if(result){
            this.sendCommand("add_interest name=POI weight=1000 radius=5 coords=" + escape(result));
        }
    }, this));
}

Beholder.prototype.interestDelete = function()
{
    this.sendCommand("delete_interest name=POI");
}

Beholder.prototype.targetNew = function()
{
    var body = $("<div/>", {class: ""});
    var tmp;

    tmp = $("<div/>", {class:"input-group"}).appendTo(body);
    $("<span/>", {class:"input-group-addon"}).html("Coords:").appendTo(tmp);
    this.target_coords = $("<input>", {class:"form-control", type:"text", size:"10", placeholder:"SIMBAD object name or coordinates"}).appendTo(tmp);

    tmp = $("<div/>", {class:"input-group"}).appendTo(body);
    $("<span/>", {class:"input-group-addon"}).html("Name:").appendTo(tmp);
    this.target_name = $("<input>", {class:"form-control", type:"text", size:"10", value:"object"}).appendTo(tmp);

    tmp = $("<div/>", {class:"input-group"}).appendTo(body);
    $("<span/>", {class:"input-group-addon"}).html("Type:").appendTo(tmp);
    this.target_type = $("<input>", {class:"form-control", type:"text", size:"10", value:"imaging"}).appendTo(tmp);

    tmp = $("<div/>", {class:"input-group"}).appendTo(body);
    $("<span/>", {class:"input-group-addon"}).html("Exposure:").appendTo(tmp);
    this.target_exposure = $("<input>", {class:"form-control", type:"text", size:"10", value:"10"}).appendTo(tmp);

    tmp = $("<div/>", {class:"input-group"}).appendTo(body);
    $("<span/>", {class:"input-group-addon"}).html("Filter:").appendTo(tmp);
    this.target_filter = $("<input>", {class:"form-control", type:"text", size:"10", value:"B"}).appendTo(tmp);

    bootbox.dialog({
        title: "New scheduler target",
        message: body,
        buttons: {
            create: {
                label: "Create",
                callback: $.proxy(function(){
                    this.sendCommand("target_create coords=" + escape(this.target_coords.val()) +
                                     " exposure=" + escape(this.target_exposure.val()) + " filter=" + escape(this.target_filter.val()) +
                                     " name=" + escape(this.target_name.val()) + " type=" + escape(this.target_type.val()));
                    //bootbox.alert("Target added");
                }, this)}
        }
    });
}

Beholder.prototype.targetRepoint = function()
{
    bootbox.prompt("Object name or coordinates to repoint", $.proxy(function(result){
        if(result){
            this.sendCommand("repoint_mounts " + result);
        }
    }, this));
}

Beholder.prototype.targetImaging = function()
{
    var body = $("<div/>", {class: ""});
    var tmp;

    tmp = $("<div/>", {class:"input-group"}).appendTo(body);
    $("<span/>", {class:"input-group-addon"}).html("Name:").appendTo(tmp);
    this.target_name = $("<input>", {class:"form-control", type:"text", size:"10", value:"object"}).appendTo(tmp);

    tmp = $("<div/>", {class:"input-group"}).appendTo(body);
    $("<span/>", {class:"input-group-addon"}).html("Type:").appendTo(tmp);
    this.target_type = $("<input>", {class:"form-control", type:"text", size:"10", value:"imaging"}).appendTo(tmp);

    tmp = $("<div/>", {class:"input-group"}).appendTo(body);
    $("<span/>", {class:"input-group-addon"}).html("Exposure:").appendTo(tmp);
    this.target_exposure = $("<input>", {class:"form-control", type:"text", size:"10", value:"10"}).appendTo(tmp);

    tmp = $("<div/>", {class:"input-group"}).appendTo(body);
    $("<span/>", {class:"input-group-addon"}).html("Filter:").appendTo(tmp);
    this.target_filter = $("<input>", {class:"form-control", type:"text", size:"10", value:"B"}).appendTo(tmp);

    bootbox.dialog({
        title: "Acquire image for current target",
        message: body,
        buttons: {
            create: {
                label: "Image!",
                callback: $.proxy(function(){
                    this.sendCommand("next_target " +
                                     " exposure=" + escape(this.target_exposure.val()) + " filter=" + escape(this.target_filter.val()) +
                                     " name=" + escape(this.target_name.val()) + " type=" + escape(this.target_type.val()));
                    this.sendCommand("imaging");
                }, this)}
        }
    });
}

Beholder.prototype.targetFollowup = function()
{
    var body = $("<div/>", {class: ""});
    var tmp;

    tmp = $("<div/>", {class:"input-group"}).appendTo(body);
    $("<span/>", {class:"input-group-addon"}).html("RA:").appendTo(tmp);
    this.target_ra = $("<input>", {class:"form-control", type:"text", size:"10", placeholder:"degrees"}).appendTo(tmp);

    tmp = $("<div/>", {class:"input-group"}).appendTo(body);
    $("<span/>", {class:"input-group-addon"}).html("Dec:").appendTo(tmp);
    this.target_dec = $("<input>", {class:"form-control", type:"text", size:"10", placeholder:"degrees"}).appendTo(tmp);

    tmp = $("<div/>", {class:"input-group"}).appendTo(body);
    $("<span/>", {class:"input-group-addon"}).html("Name:").appendTo(tmp);
    this.target_name = $("<input>", {class:"form-control", type:"text", size:"10", value:"transient"}).appendTo(tmp);

    tmp = $("<div/>", {class:"input-group"}).appendTo(body);
    $("<span/>", {class:"input-group-addon"}).html("Exposure:").appendTo(tmp);
    this.target_exposure = $("<input>", {class:"form-control", type:"text", size:"10", value:"10"}).appendTo(tmp);

    bootbox.dialog({
        title: "Initiate follow-up for a given coordinates",
        message: body,
        buttons: {
            create: {
                label: "Follow-up!",
                callback: $.proxy(function(){
                    this.sendCommand("next_target ra=" + escape(this.target_ra.val()) + " dec=" + escape(this.target_dec.val()) +
                                     " exposure=" + escape(this.target_exposure.val()) +
                                     " name=" + escape(this.target_name.val()) + " type=followup");
                    this.sendCommand("followup");
                }, this)}
        }
    });
}

Beholder.prototype.toggleDomeControls = function()
{
    if(this.dome_controls_inner.is(":visible")){
        this.dome_controls_inner_button.text(">>");
    } else {
        this.dome_controls_inner_button.text("<<");
    }

    hideshow(this.dome_controls_inner);
}
