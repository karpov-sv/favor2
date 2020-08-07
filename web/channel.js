Channel = function(parent_id){
    var panel = $("<div/>", {class:"mount panel panel-default"});
    var tmp;

    this.baseurl = location.pathname;

    document.title = "Channel";

    var header = $("<div/>", {class:"panel-heading"}).appendTo(panel);
    var title = $("<h3/>", {class:"panel-title"}).appendTo(header);
    this.title = $("<span/>").html("Channel ").appendTo(title);
    this.throbber = $("<span/>", {class:"glyphicon glyphicon-refresh pull-right"}).appendTo(title);
    this.connstatus = $("<span/>", {class:"label label-default"}).appendTo(title);
    this.hw_connstatus = $("<span/>", {class:"label label-default"}).appendTo(title);

    this.list = $("<ul/>", {class:"list-group"}).appendTo(panel);

    /* Channel status */
    this.status = $("<li/>", {class:"list-group-item"}).appendTo(this.list);
    this.target = $("<li/>", {class:"list-group-item"}).appendTo(this.list);
    this.andor_status = $("<li/>", {class:"list-group-item"}).appendTo(this.list);
    this.processing_status = $("<li/>", {class:"list-group-item"}).appendTo(this.list);

    this.body = $("<div/>", {class:"panel-body"}).appendTo(panel);

    this.div = $("<div/>", {class:"channel"});

    this.progressdiv = $("<div/>", {class:"progress"}).appendTo(this.body);
    this.progress = $("<div/>", {class:"progress-bar", style:"width: 0%"}).appendTo(this.progressdiv);

    /* Channel controls */
    this.channel_controls = $("<ul/>", {class:"list-group-item list-inline"}).appendTo(this.list);

    tmp = $("<li/>", {class:"btn-group"}).appendTo(this.channel_controls);

    this.makeButton("Regimes:").appendTo(tmp);
    this.makeButton("Darks", "write_darks").appendTo(tmp);
    this.makeButton("Flats", "write_flats").appendTo(tmp);
    this.makeButton("Autofocus", "autofocus").appendTo(tmp);
    this.makeButton("Preview", "preview").appendTo(tmp);

    this.makeButton("Monitoring", "monitoring_start").appendTo(tmp);
    this.makeButton("Stop Monitoring", "monitoring_stop").appendTo(tmp);

    this.makeButton("Follow-up", "followup").appendTo(tmp);
    this.makeButton("Imaging", "imaging").appendTo(tmp);

    this.makeButton("Locate", "locate").appendTo(tmp);
    this.makeButton("Stop!", "stop").appendTo(tmp);
    this.makeButton("Reset", "reset").appendTo(tmp);

    tmp = $("<li/>", {class:"btn-group"}).appendTo(this.channel_controls);
    this.makeButton("Processing:").appendTo(tmp);
    this.processing_start_button = this.makeButton("Start", "processing_start").appendTo(tmp);
    this.processing_stop_button = this.makeButton("Stop", "processing_stop").appendTo(tmp);

    tmp = $("<li/>", {class:"btn-group"}).appendTo(this.channel_controls);
    this.makeButton("Storage:").appendTo(tmp);
    this.storage_start_button = this.makeButton("Start", "storage_start").appendTo(tmp);
    this.storage_stop_button = this.makeButton("Stop", "storage_stop").appendTo(tmp);

    tmp = $("<li/>", {class:"btn-group"}).appendTo(this.channel_controls);
    this.makeButton("Grabber:").appendTo(tmp);
    this.grabber_start_button = this.makeButton("Start", "grabber_start").appendTo(tmp);
    this.grabber_stop_button = this.makeButton("Stop", "grabber_stop").appendTo(tmp);

    tmp = $("<li/>", {class:"btn-group"}).appendTo(this.channel_controls);
    this.makeButton("Zoom:").appendTo(tmp);
    this.zoom_on_button = this.makeButton("On", "set_zoom 1").appendTo(tmp);
    this.zoom_off_button = this.makeButton("Off", "set_zoom 0").appendTo(tmp);

    tmp = $("<li/>", {class:"btn-group"}).appendTo(this.channel_controls);
    this.makeButton("Restart!", "exit").appendTo(tmp);

    /* HW status */
    this.hw_status = $("<li/>", {class:"list-group-item"}).appendTo(this.list);
    this.hw_env = $("<li/>", {class:"list-group-item"}).appendTo(this.list);

    /* HW controls */
    this.hw_controls = $("<ul/>", {class:"list-group-item list-inline"}).appendTo(this.list);

    tmp = $("<li/>", {class:"btn-group"}).appendTo(this.hw_controls);
    this.makeButton("Cover:").appendTo(tmp);
    this.cover_open_button = this.makeButton("Open", "set_cover 1", "hw").appendTo(tmp);
    this.cover_close_button = this.makeButton("Close", "set_cover 0", "hw").appendTo(tmp);

    tmp = $("<li/>", {class:"btn-group"}).appendTo(this.hw_controls);
    this.makeButton("II:").appendTo(tmp);
    this.ii_on_button = this.makeButton("On", "set_ii_power 1", "hw").appendTo(tmp);
    this.ii_off_button = this.makeButton("Off", "set_ii_power 0", "hw").appendTo(tmp);
    this.ii_controls = tmp;

    tmp = $("<li/>", {class:"btn-group"}).appendTo(this.hw_controls);
    this.makeButton("Camera:").appendTo(tmp);
    this.camera_on_button = this.makeButton("On", "set_camera 1", "hw").appendTo(tmp);
    this.camera_off_button = this.makeButton("Off", "set_camera 0", "hw").appendTo(tmp);

    tmp = $("<li/>", {class:"btn-group"}).appendTo(this.hw_controls);
    this.makeButton("Flatfield:").appendTo(tmp);
    this.flatfield_on_button = this.makeButton("On", "set_lamp 1", "hw").appendTo(tmp);
    this.flatfield_off_button = this.makeButton("Off", "set_lamp 0", "hw").appendTo(tmp);

    /* Filter controls */
    var filter_controls = $("<li/>", {class:"btn-group"}).appendTo(this.hw_controls);
    this.makeButton("Filters:").appendTo(filter_controls);
    this.makeButton("Clear", "set_filters 0", "hw").appendTo(filter_controls);
    this.makeButton("B", "set_filters name=B", "hw").appendTo(filter_controls);
    this.makeButton("V", "set_filters name=V", "hw").appendTo(filter_controls);
    this.makeButton("R", "set_filters name=R", "hw").appendTo(filter_controls);
    this.makeButton("B+Pol", "set_filters name=B+Pol", "hw").appendTo(filter_controls);
    this.makeButton("V+Pol", "set_filters name=V+Pol", "hw").appendTo(filter_controls);
    this.makeButton("R+Pol", "set_filters name=R+Pol", "hw").appendTo(filter_controls);
    this.makeButton("Pol", "set_filters name=Pol", "hw").appendTo(filter_controls);

    /* Focus control */

    tmp = $("<li/>", {class:"btn-group"}).appendTo(this.hw_controls);
    this.makeButton("<<", "reset_focus id=0", "hw").appendTo(tmp);
    this.makeButton("<100", "move_focus id=0 shift=-100", "hw").appendTo(tmp);
    this.makeButton("<10", "move_focus id=0 shift=-10", "hw").appendTo(tmp);
    this.makeButton("<1", "move_focus id=0 shift=-1", "hw").appendTo(tmp);
    this.makeButton("Focus").appendTo(tmp);
    this.makeButton("1>", "move_focus id=0 shift=1", "hw").appendTo(tmp);
    this.makeButton("10>", "move_focus id=0 shift=10", "hw").appendTo(tmp);
    this.makeButton("100>", "move_focus id=0 shift=100", "hw").appendTo(tmp);
    this.makeButton(">>", "move_focus id=0 shift=4096", "hw").appendTo(tmp);

    tmp = $("<li/>", {class:"btn-group"}).appendTo(this.hw_controls);
    this.makeButton("<<", "reset_focus id=1", "hw").appendTo(tmp);
    this.makeButton("<100", "move_focus id=1 shift=-100", "hw").appendTo(tmp);
    this.makeButton("<10", "move_focus id=1 shift=-10", "hw").appendTo(tmp);
    this.makeButton("Focus2").appendTo(tmp);
    this.makeButton("10>", "move_focus id=1 shift=10", "hw").appendTo(tmp);
    this.makeButton("100>", "move_focus id=1 shift=100", "hw").appendTo(tmp);
    this.makeButton(">>", "move_focus id=1 shift=200", "hw").appendTo(tmp);
    this.focus2_controls = tmp;

    /* Aperture */
    tmp = $("<li/>", {class:"btn-group"}).appendTo(this.hw_controls);
    this.makeButton("<<", "aperture_move -10", "hw").appendTo(tmp);
    this.makeButton("<", "aperture_move -1", "hw").appendTo(tmp);
    this.makeButton("Aperture").appendTo(tmp);
    this.makeButton(">", "aperture_move 1", "hw").appendTo(tmp);
    this.makeButton(">>", "aperture_move 10", "hw").appendTo(tmp);

    /* Celostate controls */

    tmp = $("<li/>", {class:"btn-group"}).appendTo(this.hw_controls);
    this.makeButton("Celostate:").appendTo(tmp);
    this.makeButton("&#8598;", "set_mirror -127 -127", "hw").appendTo(tmp);
    this.makeButton("&#8593;", "set_mirror 0 -127", "hw").appendTo(tmp);
    this.makeButton("&#8599;", "set_mirror 127 -127", "hw").appendTo(tmp);
    this.makeButton("&#8592;", "set_mirror -127 0", "hw").appendTo(tmp);
    this.makeButton("&#183;", "set_mirror 0 0", "hw").appendTo(tmp);
    this.makeButton("&#8594;", "set_mirror 127 ", "hw").appendTo(tmp);
    this.makeButton("&#8601;", "set_mirror -127 127", "hw").appendTo(tmp);
    this.makeButton("&#8595;", "set_mirror 0 127", "hw").appendTo(tmp);
    this.makeButton("&#8600;", "set_mirror 127 127", "hw").appendTo(tmp);

    this.celostate_controls = $("<li/>", {class:"input-group hide"}).appendTo(this.hw_controls);

    this.mirror0 = $("<input/>", {"data-slider-id": "mirror1Slider", type: "text", class: "form-control",
                                  "data-slider-min": "-127", "data-slider-max": "127", "data-slider-value": "0", "data-slider-step": "1"}).appendTo(this.celostate_controls);
    $("<span/>").html(" x ").appendTo(this.celostate_controls);
    this.mirror1 = $("<input/>", {"data-slider-id": "mirror1Slider", type: "text",
                                  "data-slider-min": "-127", "data-slider-max": "127", "data-slider-value": "0", "data-slider-step": "1"}).appendTo(this.celostate_controls);
    this.mirror0.slider();
    this.mirror1.slider();

    this.mirror0.on('slideStop', $.proxy(function(ev) {this.sendCommand("set_mirror "+this.mirror0.slider('getValue')+" "+this.mirror1.slider('getValue'), "hw");}, this));
    this.mirror1.on('slideStop', $.proxy(function(ev) {this.sendCommand("set_mirror "+this.mirror0.slider('getValue')+" "+this.mirror1.slider('getValue'), "hw");}, this));

    $("<button>", {class:"btn btn-default"}).html("Custom").click($.proxy(function(event){
        hideshow(this.celostate_controls);
        event.preventDefault();
    }, this)).appendTo(this.hw_controls);

    /* Command line */
    this.cmdline = $("<input>", {type:"text", size:"40", class:"form-control"});
    this.cmdtarget = $("<select/>", {class:"selectpicker input-group-btn"});
    $("<option>", {value: "channel"}).html("Channel").appendTo(this.cmdtarget);
    $("<option>", {value: "hw"}).html("HW").appendTo(this.cmdtarget);

    $("<div/>", {class:"input-group"}).append(this.cmdtarget).append($("<span/>", {class:"input-group-addon"}).html("Command:")).append(this.cmdline).appendTo(this.body);

    this.cmdline.pressEnter($.proxy(function(event){
        this.sendCommand(this.cmdline.val(), this.cmdtarget.val());
        event.preventDefault();
    }, this));

    this.delayValue = 2000;

    this.image = $("<img/>", {src:this.baseurl + "/image.jpg", class:"image img-thumbnail center-block"}).appendTo(this.body);
    new Updater(this.image, this.delayValue);

    var footer = $("<div/>", {class:"panel-footer"});//.appendTo(panel);

    var form = $("<form/>", {class:"form-inline"}).appendTo(footer);
    //this.throbber = $("<span/>", {class:"glyphicon glyphicon-transfer"}).appendTo(form);
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

Channel.prototype.requestState = function(){
    $.ajax({
        url: this.baseurl + "/status",
        dataType : "json",
        timeout : 1000,
        context: this,

        success: function(json){
            this.throbber.animate({opacity: 1.0, color:"green"}, 200).animate({opacity: 0.1}, 400);

            /* Crude hack to prevent jumping */
            var st = document.body.scrollTop;
            var sl = document.body.scrollLeft;
            this.updateStatus(json.channel_connected, json.channel, json.hw_connected, json.hw);
            document.body.scrollTop = st;
            document.body.scrollLeft = sl;
        },

        complete: function( xhr, status ) {
            setTimeout($.proxy(this.requestState, this), this.delayValue);
        }
    });
}

Channel.prototype.updateStatus = function(connected, status, hw_connected, hw_status){
    var states = {0:"Normal", 1:"Darks", 2:"Autofocus", 3:"Monitoring", 4:"Follow-up", 5:"Flats", 6:"Locate", 7:"Reset", 8:"Calibrate", 9:"Imaging", 100:"Error"};
    var statetypes = {0:"success", 1:"success", 2:"info", 3:"info", 4:"info", 5:"info", 6:"warning", 7:"warning", 8:"warning", 100:"danger"}

    var substates = {"-1":"Error", 0:"Off", 1:"On", 2:"Starting"};
    var coverstates = {"-1":"Unknown", 0:"Closed", 1:"Open"};
    var types = {"-1":"danger", 0:"primary", 1:"warning"};
    var bool = {0:"No", 1:"Yes"};
    var booltypes = {0:"danger", 1:"primary"};

    if(connected || hw_connected){
        show(this.body);
        show(this.list);
    } else {
        hide(this.body);
        hide(this.list);
    }

    if(connected){
        var centered = status['ra'] == status['ra0'] && status['dec'] == status['dec0'];

        show(this.status);
        show(this.channel_controls);
        show(this.image);

        if(status['id']){
            this.title.html("Channel " + (parseInt(status['id'])) + " ");
            document.title = "Channel " + (parseInt(status['id']));
        }

        this.connstatus.html("Connected").removeClass("label-danger").addClass("label-success");

        this.status.html("State: " + label(states[status['state']], statetypes[status['state']]) +
                         " Storage: " + label(substates[status['storage']], types[status['storage']]) +
                         " Processing: " + label(substates[status['processing']], types[status['processing']]) +
                         " Grabber: " + label(substates[status['grabber']], types[status['grabber']]) +
                         // " RA: " + label(status['ra']) + (centered ? "" : " / " + label(status['ra0'])) +
                         // " Dec: " + label(status['dec']) + (centered ? "" : " / " + label(status['dec0'])) +
                         " RA: " + label(status['ra']) +
                         " Dec: " + label(status['dec']) +
                         " Focused: " + label(bool[status['is_focused']], booltypes[status['is_focused']]) +
                         " Darks: " + label(bool[status['has_dark']], booltypes[status['has_dark']]) +
                         " Flats: " + label(bool[status['has_flat']], booltypes[status['has_flat']])
                        );

        this.status.append(" Frame age: " + label(status['last_frame_age'],
                                                 status['last_frame_age'] < 1 ? "primary" : "danger"));

        //if(status['realtime_queue'] > 10)
        this.status.append("<br>Realtime: " + label(status['realtime_queue'], status['realtime_queue'] < 10 ? "success" : "danger"));
        this.status.append(" Secondscale: " + label(status['secondscale_queue'], status['secondscale_queue'] < 10 ? "success" : "danger"));
        this.status.append(" Raw: " + label(status['raw_queue'], status['raw_queue'] < 10 ? "success" : "danger"));
        this.status.append(" Grabber: " + label(status['grabber_queue'], status['grabber_queue'] < 10 ? "success" : "danger"));
        this.status.append(" DB: " + label(status['db_queue'], status['db_queue'] < 10 ? "success" : "danger"));

        this.status.append("<br>Image mean: " + label(status['image_mean']));
        this.status.append(" +/- " + label(status['image_std']));
        this.status.append(" median: " + label(status['image_median']));
        this.status.append(" +/- " + label(status['image_mad']));

        if(status['target_name']){
            show(this.target);
            this.target.html("Next Target: " + label(status['target_name']) +
                             " Type: " + label(status['target_type']) +
                             " Exposure: " + label(status['target_exposure']) +
                             " Filter: " + label(status['target_filter']));
        } else
            hide(this.target);

        if(status['has_ii'] == '1'){
            show(this.ii_controls);
            show(this.focus2_controls);
        } else {
            hide(this.ii_controls);
            hide(this.focus2_controls);
        }

        if(status['is_zoom'] == '1'){
            disable_button(this.zoom_on_button);
            enable_button(this.zoom_off_button);
        } else {
            enable_button(this.zoom_on_button);
            disable_button(this.zoom_off_button);
        }

        if(status['progress'] > 0){
            show(this.progressdiv);
            this.progress.css("width", 100*status['progress']+"%");
        } else {
            hide(this.progressdiv);
        }

        /* Buttons */
        this.buttonStates(status['storage'], this.storage_start_button, this.storage_stop_button);
        this.buttonStates(status['processing'], this.processing_start_button, this.processing_stop_button);
        this.buttonStates(status['grabber'], this.grabber_start_button, this.grabber_stop_button);
    } else {
        this.connstatus.html("Disconnected").removeClass("label-success").addClass("label-danger");
        this.status.html("");
        hide(this.status);
        hide(this.channel_controls);
        hide(this.image);
        hide(this.progressdiv);
    }

    if(hw_connected){
        show(this.hw_status);
        show(this.hw_env);
        show(this.hw_controls);

        if(hw_status['connected'] == 1){
            this.hw_status.html("Cover: " + label(coverstates[hw_status['cover']], types[hw_status['cover']]) +
                                (status['has_ii'] == "1" ? " Image Intensifier: " + label(substates[hw_status['ii_power']], types[hw_status['ii_power']]) : "") +
                                " Flatfield Lamp: " + label(substates[hw_status['lamp']], types[hw_status['lamp']]) +
                                " Camera: " + label(substates[hw_status['camera']], types[hw_status['camera']]) +
                                " Focus: " + label(hw_status['focus']) +
                                (status['has_ii'] == "1" ? " / " + label(hw_status['focus2']) : "") +
                                " Celostate: " + label(hw_status['celostate_pos0']) + " / " + label(hw_status['celostate_pos1']) +
                                " Filters: " + label(hw_status['filter0'] + " " + hw_status['filter1'] + " " + hw_status['filter2'] + " " + hw_status['filter3']) + " = " + label(hw_status['filter_name']) +
                                "");

            this.hw_env.html("Camera Temp: " + label(hw_status['camera_temperature']) +
                             " Hum: " + label(hw_status['camera_humidity']) +
                             " Dewpoint: " + label(hw_status['camera_dewpoint']) +
                             " Celostate Temp: " + label(hw_status['celostate_temperature']) +
                             " Hum: " + label(hw_status['celostate_humidity']) +
                             " Dewpoint: " + label(hw_status['celostate_dewpoint']));
            } else {
                this.hw_status.html(label("HW Controller Disconnected", "danger"));
                this.hw_env.html("");
            }

        // FIXME: prevent updating when slider is active
        // this.mirror0.slider('setValue', parseInt(hw_status['celostate_pos0']));
        // this.mirror1.slider('setValue', parseInt(hw_status['celostate_pos1']));

        this.hw_connstatus.html("HW Connected");
        this.hw_connstatus.removeClass("label-danger").addClass("label-success");

        /* Buttons */

        this.buttonStates(hw_status['cover'], this.cover_open_button, this.cover_close_button);
        this.buttonStates(hw_status['ii_power'], this.ii_on_button, this.ii_off_button);
        this.buttonStates(hw_status['camera'], this.camera_on_button, this.camera_off_button);
        this.buttonStates(hw_status['lamp'], this.flatfield_on_button, this.flatfield_off_button);
    } else {
        this.hw_connstatus.html("HW Disconnected");
        this.hw_connstatus.removeClass("label-success").addClass("label-danger");
        this.hw_status.html("");
        this.hw_env.html("");
        hide(this.hw_status);
        hide(this.hw_env);
        hide(this.hw_controls);
    }

    if(connected && status['andor'] == '1' && status['grabber'] != -1){
        var binning = {0:"1x1", 1:"2x2", 2:"3x3", 3:"4x4", 4:"8x8"};
        var cooling = {0:"Off", 1:"Stabilized", 2:"Cooling", 3:"Drift", 4:"Not stabilized", 5:"Fault"};
        var coolingtype = {0:"danger", 1:"success", 2:"warning", 3:"danger", 4:"warning", 5:"danger"};
        var preamp = {0:"High Range", 1:"Low Noise", 2:"Low Noise + High Range"};
        var shutter = {0:"Rolling", 1:"Global"};

        show(this.andor_status);

        this.andor_status.html("");
        this.andor_status.append(" Andor Temp: " + label(status['andor_temperature']));
        this.andor_status.append(" " + label(cooling[status['andor_temperaturestatus']], coolingtype[status['andor_temperaturestatus']]));

        this.andor_status.append(" Exp: " + label(status['andor_exposure']) + " / " + label(status['andor_framerate'] + " fps"));
        this.andor_status.append(" Mode: " + label(preamp[status['andor_preamp']]));
        this.andor_status.append(" Shutter: " + label(shutter[status['andor_shutter']]));
        this.andor_status.append(" Binning: " + label(binning[status['andor_binning']]));
        if(status['andor_filter'] == 1)
            this.andor_status.append(" " + label("Noise Filter", "success"));
    } else
        hide(this.andor_status);

    if(connected && status['processing'] == 1){
        show(this.processing_status);

        this.processing_status.html("");

        this.processing_status.append("Records: " + label(status['realtime_nrecords']) + " ");
        this.processing_status.append("S: " + label(status['realtime_nsingle']) + " ");
        this.processing_status.append("D: " + label(status['realtime_ndouble']) + " ");
        this.processing_status.append("m: " + label(status['realtime_nmoving']) + " ");
        this.processing_status.append("M: " + label(status['realtime_nmeteor']) + " ");
        this.processing_status.append("F: " + label(status['realtime_nflash']) + " ");

        this.processing_status.append("Secondscale Nobj: " + label(status['secondscale_nobjects']) + " ");
        this.processing_status.append("Mag Lim: " + label(Number(status['secondscale_mag_limit']).toFixed(1)) + " ");
        this.processing_status.append("(" + label(Number(status['secondscale_mag_min']).toFixed(1)) + "/" + label(Number(status['secondscale_mag_max']).toFixed(1)) + ") ");
    } else {
        hide(this.processing_status);
    }
}

Channel.prototype.sendCommand = function(command, target){
    var target = target || "channel";

    if(target == "channel")
        path = this.baseurl + "/command";
    else if(target == "hw")
        path = this.baseurl + "/hw_command";

    $.ajax({
        url: path,
        data: {string: command}
    });
}

Channel.prototype.makeButton = function(text, command, target)
{
    target = target || "channel";

    if(command)
        return $("<button>", {class:"btn btn-default"}).html(text).click($.proxy(function(event){
            this.sendCommand(command, target);
            event.preventDefault();
        }, this));
    else
        return $("<button>", {class:"btn btn-default disabled"}).html(text);
}

enable_button = function(obj){
    obj.prop('disabled', false);
    //obj.addClass("btn-primary").removeClass("btn-default");
    obj.addClass("btn-default").removeClass("btn-success");
    //obj.addClass("btn-primary");
}

disable_button = function(obj){
    obj.prop('disabled', true);
    //obj.removeClass("btn-primary").addClass("btn-default");
    obj.removeClass("btn-default").addClass("btn-success");
    //obj.addClass("btn-primary");
}

Channel.prototype.buttonStates = function(state, button1, button2)
{
    if(state == 0){
        disable_button(button2);
        enable_button(button1);
    } else if(state == 1) {
        enable_button(button2);
        disable_button(button1);
    } else {
        enable_button(button1);
        enable_button(button2);
    }
}
