Scheduler = function(parent_id){
    var panel = $("<div/>", {class:"scheduler panel panel-default"});

    var header = $("<div/>", {class:"panel-heading"}).appendTo(panel);
    var title = $("<h3/>", {class:"panel-title"}).html("Scheduler ").appendTo(header);
    this.throbber = $("<span/>", {class:"glyphicon glyphicon-import"}).appendTo(title);
    this.connstatus = $("<span/>", {class:"label label-default"}).appendTo(title);

    this.body = $("<div/>", {class:"panel-body"}).appendTo(panel);

    this.status = $("<div/>", {class:"scheduler-status"}).appendTo(this.body);

    this.cmdline = $("<input>", {type:"text", size:"40", class:"form-control"});

    $("<div/>", {class:"input-group"}).append($("<span/>", {class:"input-group-addon"}).html("Command:")).append(this.cmdline).appendTo(this.body);

    this.cmdline.pressEnter($.proxy(function(event){
        this.sendCommand(this.cmdline.val());
        event.preventDefault();
    }, this));

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

Scheduler.prototype.requestState = function(){
    $.ajax({
        url: "/scheduler/status",
        dataType : "json",
        timeout : 1000,
        context: this,

        success: function(json){
            this.throbber.animate({opacity: 1.0}, 200).animate({opacity: 0.1}, 400);

            /* Crude hack to prevent jumping */
            st = document.body.scrollTop;
            sl = document.body.scrollLeft;
            this.updateStatus(json.scheduler_connected, json.scheduler, json.suggestion);
            document.body.scrollTop = st;
            document.body.scrollLeft = sl;
        },

        complete: function( xhr, status ) {
            setTimeout($.proxy(this.requestState, this), this.delayValue);
        }
    });
}

Scheduler.prototype.updateStatus = function(connected, status, suggestion){
    this.status.html("");

    if(connected){
        if(this.body.is(":hidden"))
            this.body.slideDown();

        var list = $("<ul/>", {class:"list-inline"}).appendTo(this.status);
        var now;

        if(status['zsun'] > 90 + 18)
            now = "Night";
        else if(status['zsun'] > 90)
            now = "Twilight";
        else
            now = "Day";

        $("<li/>", {class:"list-group-item"}).html("Now: " + now).appendTo(list);
        $("<li/>", {class:"list-group-item"}).html("Sun Z: " + status['zsun']).appendTo(list);
        $("<li/>", {class:"list-group-item"}).html("Moon Z: " + status['zmoon']).appendTo(list);
        // $("<li/>", {class:"list-group-item"}).html("Lat: " + status['latitude']).appendTo(list);
        // $("<li/>", {class:"list-group-item"}).html("Lon: " + status['longitude']).appendTo(list);

        $("<li/>", {class:"list-group-item"}).html("Suggested: " + suggestion['type']).appendTo(list);
        if(suggestion['type'] != 'survey')
            $("<li/>", {class:"list-group-item"}).html("Name: " + suggestion['name']).appendTo(list);
        $("<li/>", {class:"list-group-item"}).html("RA: " + suggestion['ra']).appendTo(list);
        $("<li/>", {class:"list-group-item"}).html("Dec: " + suggestion['dec']).appendTo(list);
        if(suggestion['type'] != 'survey'){
            $("<li/>", {class:"list-group-item"}).html("Exposure: " + suggestion['exposure']).appendTo(list);
            $("<li/>", {class:"list-group-item"}).html("Filter: " + suggestion['filter']).appendTo(list);
        }

        this.connstatus.html("Connected");
        this.connstatus.removeClass("label-danger").addClass("label-success");
    } else {
        if(this.body.is(":visible"))
            this.body.slideUp();
        this.connstatus.html("Disconnected");
        this.connstatus.removeClass("label-success").addClass("label-danger");
    }
}

Scheduler.prototype.sendCommand = function(command){
    $.ajax({
        url: "/scheduler/command",
        data: {string: command}
    });
}
