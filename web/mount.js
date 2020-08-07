Mount = function(parent_id){
    var panel = $("<div/>", {class:"mount panel panel-default"});

    // Hack to get right mount url even for channel page
    this.baseurl = location.pathname;
    this.baseurl = this.baseurl.replace('channels/1', 'mounts/1');
    this.baseurl = this.baseurl.replace('channels/3', 'mounts/2');
    this.baseurl = this.baseurl.replace('channels/5', 'mounts/3');
    this.baseurl = this.baseurl.replace('channels/7', 'mounts/4');
    this.baseurl = this.baseurl.replace('channels/9', 'mounts/5');
    this.baseurl = this.baseurl.replace('channel', 'mount');

    this.parent_id = parent_id;

    if(this.parent_id != "#contents2")
        document.title = "Mount";

    var header = $("<div/>", {class:"panel-heading"}).appendTo(panel);
    var title = $("<h3/>", {class:"panel-title"}).appendTo(header);
    this.title = $("<span/>").html("Mount ").appendTo(title);
    this.throbber = $("<span/>", {class:"glyphicon glyphicon-refresh pull-right"}).appendTo(title);
    this.connstatus = $("<span/>", {class:"label label-default"}).appendTo(title);

    this.list = $("<ul/>", {class:"list-group"}).appendTo(panel);

    this.status = $("<li/>", {class:"list-group-item"}).appendTo(this.list);
    this.coords = $("<li/>", {class:"list-group-item"}).appendTo(this.list);
    this.encoders = $("<li/>", {class:"list-group-item"}).appendTo(this.list);

    this.body = $("<ul/>", {class:"panel-body list-inline"}).appendTo(panel);

    /* High-level controls */
    var controls = $("<li/>", {class:"btn-group"}).appendTo(this.body);

    this.makeButton("Start Tracking", "tracking_start").appendTo(controls);
    this.makeButton("Stop Tracking", "tracking_stop").appendTo(controls);
    this.makeButton("Stop!", "stop").appendTo(controls);
    this.makeButton("Park", "park").appendTo(controls);

    /* Low-level */
    var motors = $("<li/>", {class:"btn-group"}).appendTo(this.body);

    this.makeButton("< RA", "set_motors 1 0").appendTo(motors);
    this.makeButton("< Dec", "set_motors 0 1").appendTo(motors);
    this.makeButton("Stop!", "stop").appendTo(motors);
    this.makeButton("Dec >", "set_motors 0 -1").appendTo(motors);
    this.makeButton("RA >", "set_motors -1 0").appendTo(motors);

    var restart = $("<li/>", {class:"btn-group"}).appendTo(this.body);
    this.makeButton("Restart", "exit").appendTo(restart);

    this.cmdline = $("<input>", {type:"text", size:"40", class:"form-control"});

    $("<div/>", {class:"input-group"}).append($("<span/>", {class:"input-group-addon"}).html("Command:")).append(this.cmdline).appendTo(this.body);

    this.cmdline.pressEnter($.proxy(function(event){
        this.sendCommand(this.cmdline.val());
        event.preventDefault();
    }, this));

    var footer = $("<div/>", {class:"panel-footer"});//.appendTo(panel);

    var form = $("<form/>").appendTo(footer);
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

Mount.prototype.requestState = function(){
    $.ajax({
        url: this.baseurl + "/status",
        dataType : "json",
        timeout : 1000,
        context: this,

        success: function(json){
            this.throbber.animate({opacity: 1.0}, 200).animate({opacity: 0.1}, 400);

            /* Crude hack to prevent jumping */
            st = document.body.scrollTop;
            sl = document.body.scrollLeft;
            this.updateStatus(json.mount_connected, json.mount);
            document.body.scrollTop = st;
            document.body.scrollLeft = sl;
        },

        complete: function( xhr, status ) {
            setTimeout($.proxy(this.requestState, this), this.delayValue);
        }
    });
}

Mount.prototype.updateStatus = function(connected, status){
    var states = {"-1":"Error", 0:"Idle", 1:"Slewing", 2:"Tracking", 3:"Tracking", 4:"Manual"};
    var types = {"-1":"danger", 0:"success", 1:"info", 2:"warning", 3:"warning", 4:"danger"}

    if(connected){
        show(this.body);
        show(this.list);

        if(status['id']){
            this.title.html("Mount " + (parseInt(status['id'])) + " ");
            if(this.parent_id != "#contents2")
                document.title = "Mount " + (parseInt(status['id']));
        }

        if(status['connected'] == 1){
            this.status.html("State: " + label(states[status['state']], types[status['state']]) + " ");
            if(status['calibrated'] == 0)
                $("<span/>", {class: "label label-danger"}).html("Uncalibrated").appendTo(this.status);

            this.coords.html("RA: " + label(status['ra']) + " Dec: " + label(status['dec']) +
                             " Alt: " + label(status['alt']) + " Az: " + label(status['az']) +
                             " Target RA: " + label(status['next_ra']) + " Dec: " + label(status['next_dec']));

            this.encoders.html("Encoder RA: " + label(status['enc_ra']) + " Dec: " + label(status['enc_dec']));
            this.encoders.append(" Next Enc RA: " + label(status['next_enc_ra']) + " Dec: " + label(status['next_enc_dec']));
        } else {
            this.status.html(label("Mount Controller Disconnected", "danger"));
        }

        this.connstatus.html("Connected");
        this.connstatus.removeClass("label-danger").addClass("label-success");
    } else {
        hide(this.body);
        hide(this.list);

        this.connstatus.html("Disconnected");
        this.connstatus.removeClass("label-success").addClass("label-danger");
    }
}

Mount.prototype.sendCommand = function(command){
    $.ajax({
        url: this.baseurl + "/command",
        data: {string: command}
    });
}

Mount.prototype.makeButton = function(text, command)
{
    return $("<button>", {class:"btn btn-default"}).html(text).click($.proxy(function(event){
        this.sendCommand(command);
        event.preventDefault();
    }, this));
}
