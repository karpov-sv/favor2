uid = 0;

getUUID = function(){
    return "_my_uuid_" + (uid++)
}

$.fn.pressEnter = function(fn) {

    return this.each(function() {
        $(this).bind('enterPress', fn);
        $(this).keyup(function(e){
            if(e.keyCode == 13)
            {
              $(this).trigger("enterPress");
            }
        })
    });
};

hideshow = function(obj){
    if(obj.is(":visible"))
        obj.slideUp();
    else if(obj.is(":hidden")){
        obj.slideDown();
        obj.removeClass('hide');
    }
}

hideshow_horiz = function(obj){
    if(obj.is(":visible"))
        obj.slideLeft();
    else if(obj.is(":hidden")){
        obj.slideRight();
        obj.removeClass('hide');
    }
}

hide = function(obj){
    if(obj.is(":visible"))
        obj.slideUp();
}

show = function(obj){
    obj.removeClass('hide');
    if(obj.is(":hidden"))
        obj.slideDown();
}

enable = function(obj){
    obj.prop('disabled', false);
}

disable = function(obj){
    obj.prop('disabled', true);
}

label = function(html, type, tooltip)
{
    var type = type || "primary";

    if(tooltip)
        return "<span class='label label-" + type + "' title='" + tooltip + "'>" + html + "</span>";
    else
        return "<span class='label label-" + type + "'>" + html + "</span>";
}

color = function(html, type)
{
    var type = type || "primary";

    return "<span class='text-" + type + "'>" + html + "</span>";
}

Updater = function(image_id, timeout){
    this.img = $(image_id);
    this.timeout = timeout;
    this.source = $(image_id).attr('src');

    this.timer = 0;

    this.run();
}

Updater.prototype.update = function(){
    if(this.source.indexOf("?") > 0)
        this.img.attr('src', this.source + '&rnd=' + Math.random());
    else
        this.img.attr('src', this.source + '?rnd=' + Math.random());

    this.img.one('load', $.proxy(this.run, this));
    this.img.one('error', $.proxy(this.run, this));
}

Updater.prototype.run = function(){
    clearTimeout(this.timer);
    this.timer = setTimeout($.proxy(this.update, this), this.timeout);
}
