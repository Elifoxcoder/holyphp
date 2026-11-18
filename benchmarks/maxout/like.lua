-- like.lua — POST /like?id=<random existing post>
wrk.method = "POST"
request = function()
    return wrk.format("POST", "/like?id=" .. math.random(1, 100000))
end
