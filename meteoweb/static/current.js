Updater = function(image_id, timeout){
    this.img = $(image_id);
    this.timeout = timeout;
    this.source = $(image_id).attr('src').split('?', 1);

    this.timer = 0;

    this.run();
}

Updater.prototype.update = function(){
    this.img.attr('src', this.source + '?rnd=' + Math.random());

    this.img.one('load', $.proxy(this.run, this));
    this.img.one('error', $.proxy(this.run, this));
}

Updater.prototype.run = function(){
    clearTimeout(this.timer);
    this.timer = setTimeout($.proxy(this.update, this), this.timeout);
}

$(document).ready(function(){
    new Updater('#current_image', 10000);
    new Updater('#current_sqm_brightness', 120000);
    new Updater('#current_meteo_ambient_temp', 120000);
    new Updater('#current_meteo_sky_ambient_temp', 120000);
    new Updater('#current_meteo_wind', 120000);
    new Updater('#current_meteo_humidity', 120000);
});
