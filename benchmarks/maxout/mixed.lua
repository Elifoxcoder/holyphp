-- mixed.lua — 60% feed, 20% health, 10% create, 10% like
local n = 0
request = function()
    n = n + 1
    local r = n % 10
    if r < 6 then
        return wrk.format("GET", "/feed")
    elseif r < 8 then
        return wrk.format("GET", "/health")
    elseif r == 8 then
        local body = '{"author":"user' .. (n % 1000) .. '","content":"mixed post ' .. n .. '"}'
        return wrk.format("POST", "/posts", { ["Content-Type"] = "application/json" }, body)
    else
        return wrk.format("POST", "/like?id=" .. math.random(1, 100000))
    end
end
