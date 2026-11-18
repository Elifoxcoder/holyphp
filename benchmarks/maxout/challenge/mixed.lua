-- mixed.lua - the k6 user loop, minus the think time: 60% feed, 35% single
-- post, 4% like, 1% create. Same proportions for both servers, driven by one
-- counter so the request stream is identical.
local tokens = dofile("tokens.lua")
local n = 0
request = function()
    n = n + 1
    local k = n % 100
    if k < 60 then
        return wrk.format("GET", "/feed")
    elseif k < 95 then
        return wrk.format("GET", "/posts/" .. (499981 + (n % 20)))
    elseif k < 99 then
        local tok = tokens[(n % #tokens) + 1]
        return wrk.format("POST", "/posts/" .. (499981 + (n % 20)) .. "/like",
                          { Authorization = "Bearer " .. tok }, "")
    else
        local tok = tokens[(n % #tokens) + 1]
        local body = '{"body":"mixed benchmark post ' .. n .. '"}'
        return wrk.format("POST", "/posts",
                          { ["Content-Type"] = "application/json",
                            Authorization = "Bearer " .. tok }, body)
    end
end
