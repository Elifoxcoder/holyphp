-- post.lua — unique POST /posts body per request (group-commit friendly)
local n = 0
wrk.method = "POST"
wrk.headers["Content-Type"] = "application/json"
wrk.body = '{"author":"bench","content":"hello world"}'

request = function()
    n = n + 1
    local body = '{"author":"user' .. (n % 1000) .. '","content":"hello world ' .. n .. '"}'
    return wrk.format("POST", "/posts", { ["Content-Type"] = "application/json" }, body)
end
