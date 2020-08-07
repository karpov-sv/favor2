-- Aux functions used for focusing

fit = require("fit")

-- Save table to text file
function save(x, y, filename)
   local file = io.open(filename, "w")

   for i = 1, #x do
      file:write(x[i].." "..y[i].."\n")
   end

   file:close()
end

function save3(x, y, z, filename)
   local file = io.open(filename, "w")

   for i = 1, #x do
      file:write(x[i].." "..y[i].." "..z[i].."\n")
   end

   file:close()
end

function save4(x, y, z, zz, filename)
   local file = io.open(filename, "w")

   for i = 1, #x do
      file:write(x[i].." "..y[i].." "..z[i].." "..zz[i].."\n")
   end

   file:close()
end

-- Find maximum of an array
function find_maxpos(x, y)
   local max_x = -1
   local max_y = -1

   for i = 1, #x do
      if y[i] > max_y then
         max_x = x[i]
         max_y = y[i]
      end
   end

   return max_x, max_y
end

function find_maxpos_precise(x, y)
   local a, b, c = fit.parabola(x, y)

   local x0 = -b/2/c

   if x0 > math.min(unpack(x)) and x0 < math.max(unpack(x)) then
      return x0, a
   else
      return find_maxpos(x, y)
   end
end

function round(x)
   return math.ceil(x - 0.5)
end
