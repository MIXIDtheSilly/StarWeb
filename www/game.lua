local cv = document.getElementById("view")
        local ctx = cv:getContext("2d")

        -- 1 = wall, 0 = empty. The border is solid so you can't walk out.
        local MAP = {
            "1111111111111111",
            "1000000000000001",
            "1011110001111001",
            "1010000000001001",
            "1010111110101001",
            "1000100000101001",
            "1110100111101001",
            "1000100100000001",
            "1011100101111101",
            "1000000100000001",
            "1011111111110111",
            "1000000000010001",
            "1101111100010101",
            "1000000000000101",
            "1000000000000001",
            "1111111111111111",
        }
        local MAP_H = #MAP
        local MAP_W = #MAP[1]

        local function is_wall(x, y)
            local col = math.floor(x) + 1
            local row = math.floor(y) + 1
            if col < 1 or col > MAP_W or row < 1 or row > MAP_H then return true end
            return string.sub(MAP[row], col, col) == "1"
        end

        local px, py = 1.5, 1.5   -- player position, in map cells
        local angle = 0           -- facing, radians
        local FOV = math.pi / 3
        local MOVE = 2.5          -- cells per second
        local TURN = 2.4          -- radians per second

        local keys = {}
        document.addEventListener("keydown", function(e) keys[e.key] = true end)
        document.addEventListener("keyup", function(e) keys[e.key] = false end)

        -- Distance to the first wall along `ra`, plus which side we hit.
        -- Steps cell edge to cell edge (DDA) so it never steps over a corner.
        local function cast(ra)
            local sin, cos = math.sin(ra), math.cos(ra)
            local mx, my = math.floor(px), math.floor(py)
            local dx = (cos == 0) and 1e30 or math.abs(1 / cos)
            local dy = (sin == 0) and 1e30 or math.abs(1 / sin)
            local sx, sdx, sy, sdy

            if cos < 0 then sx, sdx = -1, (px - mx) * dx
            else            sx, sdx =  1, (mx + 1 - px) * dx end
            if sin < 0 then sy, sdy = -1, (py - my) * dy
            else            sy, sdy =  1, (my + 1 - py) * dy end

            local vertical = false
            for _ = 1, 64 do
                if sdx < sdy then
                    sdx = sdx + dx; mx = mx + sx; vertical = true
                else
                    sdy = sdy + dy; my = my + sy; vertical = false
                end
                if is_wall(mx + 0.5, my + 0.5) then break end
            end

            local dist
            if vertical then dist = (mx - px + (1 - sx) / 2) / cos
            else             dist = (my - py + (1 - sy) / 2) / sin end
            return math.max(dist, 0.0001), vertical
        end

        local function move(nx, ny)
            -- Axes resolve separately, so brushing a wall slides along it.
            if not is_wall(nx, py) then px = nx end
            if not is_wall(px, ny) then py = ny end
        end

        local last = nil
        local function frame(t)
            local W, H = cv.width, cv.height
            if W < 1 or H < 1 then requestAnimationFrame(frame); return end

            local dt = last and math.min((t - last) / 1000, 0.1) or 0
            last = t

            if keys["a"] then angle = angle - TURN * dt end
            if keys["d"] then angle = angle + TURN * dt end
            local step = 0
            if keys["w"] then step = MOVE * dt end
            if keys["s"] then step = -MOVE * dt end
            if step ~= 0 then
                move(px + math.cos(angle) * step, py + math.sin(angle) * step)
            end

            ctx:clearRect(0, 0, W, H)
            ctx.fillStyle = "#000000"
            ctx:fillRect(0, 0, W, H)

            -- One vertical strip per screen column.
            local cols = math.min(math.floor(W), 480)
            local cw = W / cols
            for i = 0, cols - 1 do
                local ra = angle - FOV / 2 + (i + 0.5) / cols * FOV
                local dist, vertical = cast(ra)
                -- Rays fan out at even angles, so the raw distance bulges at the
                -- edges of the view. Projecting onto the view direction flattens it.
                dist = dist * math.cos(ra - angle)
                local h = math.min(H / dist, H * 4)

                -- Shade by distance, and keep the two wall faces distinct so
                -- corners read at all against a white-on-black scene.
                local shade = 1 / (1 + dist * dist * 0.06)
                if vertical then shade = shade * 0.65 end
                local v = math.floor(255 * math.max(shade, 0.06))
                ctx.fillStyle = string.format("#%02x%02x%02x", v, v, v)
                ctx:fillRect(i * cw, (H - h) / 2, cw + 1, h)
            end

            requestAnimationFrame(frame)
        end

        requestAnimationFrame(frame)