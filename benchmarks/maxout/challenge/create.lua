-- create.lua - POST /posts (authenticated insert, group-commit friendly)
local tokens = dofile("tokens.lua")
local n = 0
request = function()
    n = n + 1
    local tok = tokens[(n % #tokens) + 1]
    local body = '{"body":"maxout benchmark post ' .. n .. ' from holyphp"}'
    return wrk.format("POST", "/posts",
                      { ["Content-Type"] = "application/json",
                        Authorization = "Bearer " .. tok }, body)
end
