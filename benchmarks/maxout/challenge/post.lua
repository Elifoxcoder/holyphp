-- post.lua - GET /posts/:id (the k6 user reads a post from the feed)
local n = 0
request = function()
    n = n + 1
    local id = 499981 + (n % 20)          -- the 20 newest posts, as load.js picks
    return wrk.format("GET", "/posts/" .. id)
end
