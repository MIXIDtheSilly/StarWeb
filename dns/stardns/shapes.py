"""Decorative SVG shapes, flattened into numbers the page script can use.

The browser has no SVG support and no way to sample an image from script, so
the artwork in assets/ is reduced here to plain geometry and emitted as Lua
literals. The page then rasterises it per frame as a dithered halftone, which
is what makes the shapes spin and shimmer.

Two forms come out of this:

  * the cog becomes a radius-per-angle table. It is star-convex about its
    centre, so "is this point inside" is one table lookup, and spinning it is
    just an offset into that table.
  * each line becomes a sampled polyline. It is a stroked path, so a point is
    inside when it lies within half the stroke width of the curve.

Figma writes degenerate `V` commands between segments (a vertical lineto to
the y the pen is already on); they are parsed and discarded.
"""
import math
import re
from functools import lru_cache
from pathlib import Path

ASSETS = Path(__file__).resolve().parent.parent / "assets"

# Angular resolution of the cog table. 720 bins is a quarter degree apiece,
# finer than the dither grid can resolve at any size we draw it.
COG_BINS = 720

_NUM = re.compile(r"-?\d*\.?\d+(?:e-?\d+)?")


def _numbers(text: str) -> list[float]:
    return [float(n) for n in _NUM.findall(text)]


def _path_d(svg: str) -> str:
    return re.search(r'\sd="([^"]*)"', svg).group(1)


def _attr(svg: str, name: str, default: float = 0.0) -> float:
    m = re.search(rf'\s{name}="([^"]*)"', svg)
    return float(m.group(1)) if m else default


def _cubic(p0, p1, p2, p3, steps):
    """Sample one cubic Bezier, skipping t=0 so segments do not double up."""
    out = []
    for i in range(1, steps + 1):
        t = i / steps
        u = 1.0 - t
        a, b, c, d = u * u * u, 3 * u * u * t, 3 * u * t * t, t * t * t
        out.append((a * p0[0] + b * p1[0] + c * p2[0] + d * p3[0],
                    a * p0[1] + b * p1[1] + c * p2[1] + d * p3[1]))
    return out


def flatten_path(d: str, steps: int = 24) -> list[tuple[float, float]]:
    """Walk an M/C/V/Z path into a polyline."""
    tokens = re.findall(r"[MCVZmcvz]|-?\d*\.?\d+(?:e-?\d+)?", d)
    pts: list[tuple[float, float]] = []
    cur = (0.0, 0.0)
    i = 0
    while i < len(tokens):
        cmd = tokens[i]
        i += 1
        if cmd in "Mm":
            cur = (float(tokens[i]), float(tokens[i + 1]))
            i += 2
            pts.append(cur)
        elif cmd in "Cc":
            p1 = (float(tokens[i]), float(tokens[i + 1]))
            p2 = (float(tokens[i + 2]), float(tokens[i + 3]))
            p3 = (float(tokens[i + 4]), float(tokens[i + 5]))
            i += 6
            pts.extend(_cubic(cur, p1, p2, p3, steps))
            cur = p3
        elif cmd in "Vv":
            # Figma's no-op separator; consume its argument and move on.
            i += 1
        elif cmd in "Zz":
            pass
    return pts


def cog_table(path: Path | None = None) -> dict:
    """Radius per angle bin for the cog, normalised to a unit outer radius."""
    svg = (path or ASSETS / "cog.svg").read_text()
    pts = flatten_path(_path_d(svg), steps=28)

    cx = sum(p[0] for p in pts) / len(pts)
    cy = sum(p[1] for p in pts) / len(pts)

    bins: list[float | None] = [None] * COG_BINS
    for x, y in pts:
        ang = math.atan2(y - cy, x - cx) % (2 * math.pi)
        r = math.hypot(x - cx, y - cy)
        b = int(ang / (2 * math.pi) * COG_BINS) % COG_BINS
        if bins[b] is None or r > bins[b]:
            bins[b] = r

    # Sampling leaves gaps between bins; bridge them from the nearest filled
    # neighbour on each side so the outline stays continuous.
    filled = [i for i, v in enumerate(bins) if v is not None]
    if not filled:
        raise ValueError("cog path produced no points")
    for i, v in enumerate(bins):
        if v is not None:
            continue
        lo = max((f for f in filled if f <= i), default=filled[-1] - COG_BINS)
        hi = min((f for f in filled if f >= i), default=filled[0] + COG_BINS)
        a, b = bins[lo % COG_BINS], bins[hi % COG_BINS]
        span = (hi - lo) or 1
        bins[i] = a + (b - a) * ((i - lo) / span)

    peak = max(bins)
    return {"r": [v / peak for v in bins], "peak": peak}


def line_polyline(name: str) -> dict:
    """Sampled centre line plus half its stroke width, in unit coordinates."""
    svg = (ASSETS / name).read_text()
    pts = flatten_path(_path_d(svg), steps=26)
    width = _attr(svg, "stroke-width", 93.0)
    w = _attr(svg, "width", 1.0)
    h = _attr(svg, "height", 1.0)

    scale = max(w, h)
    return {
        "pts": [(x / scale, y / scale) for x, y in pts],
        "half": (width / 2.0) / scale,
        "w": w / scale,
        "h": h / scale,
    }


def _fmt(v: float, places: int = 4) -> str:
    return f"{round(v, places):g}"


# Emitted as globals: the panel inlines this beside the drawing code, but the
# scratch page loads them as two separate <script>s, which do not share locals.
def lua_cog(var: str = "COG") -> str:
    t = cog_table()
    body = ",".join(_fmt(v) for v in t["r"])
    return f"{var} = {{{body}}}\n{var}_N = {len(t['r'])}\n"


def lua_line(var: str, name: str) -> str:
    d = line_polyline(name)
    xs = ",".join(_fmt(p[0]) for p in d["pts"])
    ys = ",".join(_fmt(p[1]) for p in d["pts"])
    return (f"{var} = {{ x = {{{xs}}}, y = {{{ys}}}, "
            f"half = {_fmt(d['half'])}, w = {_fmt(d['w'])}, h = {_fmt(d['h'])} }}\n")


@lru_cache(maxsize=1)
def lua_shapes() -> str:
    return (lua_cog()
            + lua_line("LINE_L", "line_left_top.svg")
            + lua_line("LINE_R", "line_right_bottom.svg"))


if __name__ == "__main__":
    print(lua_shapes())
