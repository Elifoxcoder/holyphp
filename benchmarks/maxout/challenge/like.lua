-- like.lua - POST /posts/:id/like with a seed JWT (the spec's authenticated write)
local tokens = dofile("tokens.lua")
local n = 0
request = function()
    n = n + 1
    local id = 499981 + (n % 20)          -- the 20 newest posts, like the k6 user
    local tok = tokens[(n % #tokens) + 1]
    return wrk.format("POST", "/posts/" .. id .. "/like",
                      { Authorization = "Bearer " .. tok }, "")
end
