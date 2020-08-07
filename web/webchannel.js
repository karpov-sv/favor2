$(document).ready(function(){
    object = null;

    if(location.pathname == '/channel.html')
        object = new Channel("#contents");
    else if(location.pathname == '/mount.html')
        object = new Mount("#contents");
    else if(location.pathname == '/scheduler.html')
        object = new Scheduler("#contents");
    else
        object = new Beholder("#contents");

    $('.selectpicker').selectpicker();
});
