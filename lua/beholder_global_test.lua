-- Send the message to outside world
send_message = function(msg)
   print("Sending message: " .. msg)
   -- Fake reply
   get_message(msg .. "_done")
end

send_message_to = function(to, msg)
   print("Sending message: ".. msg.." to: " .. to)
   -- Fake reply
   get_message(message_name(msg) .. "_done")
end

-- Print the debug message
dprint = function(msg)
   print(msg)
end

dofile("beholder_global.lua")

-- get_message("night_on")
-- get_message("weather_good")
-- --get_message("weather_bad")
-- get_message("night_off")
-- get_message("weather_good")
-- get_message("weather_good")
-- get_message("weather_good")

get_message("tick")
get_message("tick")

is_night = true
get_message("tick")

is_weather_good = true

while true do
   get_message("tick")
end
