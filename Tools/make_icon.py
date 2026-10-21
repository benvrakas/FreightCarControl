"""Draws the Freight Car Control icon: a train of freight cars where one car is being loaded and the others are skipped.

  python Tools/make_icon.py

Writes Resources/Icon128.png (shown in the game's Mods menu) and Docs/Images/icon-512.png (mod page).
"""
import os
from PIL import Image, ImageDraw

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SIZE = 512
SCALE = 4  # Drawn larger and scaled down for smooth edges.

BACKGROUND_TOP = (38, 44, 54)
BACKGROUND_BOTTOM = (22, 26, 32)
RAIL = (120, 126, 136)
SLEEPER = (74, 60, 48)
ORANGE = (250, 149, 73)
ORANGE_DARK = (196, 104, 40)
GREY = (92, 98, 108)
GREY_DARK = (64, 69, 77)
WHEEL = (30, 32, 36)
WHITE = (238, 240, 242)


def s(value):
    return int(value * SCALE)


def box(x0, y0, x1, y1):
    return [s(x0), s(y0), s(x1), s(y1)]


def draw_car(draw, x, y, width, height, body, trim):
    draw.rounded_rectangle(box(x, y, x + width, y + height), radius=s(8), fill=body)
    for rib in range(1, 4):
        rx = x + width * rib / 4
        draw.line([s(rx), s(y + 8), s(rx), s(y + height - 8)], fill=trim, width=s(4))
    draw.rectangle(box(x - 4, y + height - 4, x + width + 4, y + height + 6), fill=trim)
    for wx in (x + width * 0.22, x + width * 0.78):
        draw.ellipse(box(wx - 13, y + height + 2, wx + 13, y + height + 28), fill=WHEEL)
        draw.ellipse(box(wx - 5, y + height + 10, wx + 5, y + height + 20), fill=trim)


def draw_arrow(draw, cx, top, bottom, color):
    draw.rectangle(box(cx - 14, top, cx + 14, bottom - 30), fill=color)
    draw.polygon([(s(cx - 40), s(bottom - 34)), (s(cx + 40), s(bottom - 34)), (s(cx), s(bottom))], fill=color)


def main():
    big = SIZE * SCALE
    image = Image.new("RGB", (big, big))
    draw = ImageDraw.Draw(image)
    for row in range(big):
        t = row / big
        color = tuple(int(BACKGROUND_TOP[i] + (BACKGROUND_BOTTOM[i] - BACKGROUND_TOP[i]) * t) for i in range(3))
        draw.line([(0, row), (big, row)], fill=color)

    rail_y = 372
    for sx in range(10, SIZE, 34):
        draw.rectangle(box(sx, rail_y + 4, sx + 18, rail_y + 22), fill=SLEEPER)
    draw.rectangle(box(0, rail_y, SIZE, rail_y + 8), fill=RAIL)

    car_width, car_height, gap = 104, 96, 18
    left = (SIZE - (4 * car_width + 3 * gap)) / 2
    car_y = rail_y - car_height - 30
    loaded = 1
    for index in range(4):
        x = left + index * (car_width + gap)
        if index > 0:
            draw.rectangle(box(x - gap, car_y + car_height - 4, x, car_y + car_height + 2), fill=GREY_DARK)
        if index == loaded:
            draw_car(draw, x, car_y, car_width, car_height, ORANGE, ORANGE_DARK)
        else:
            draw_car(draw, x, car_y, car_width, car_height, GREY, GREY_DARK)

    arrow_x = left + loaded * (car_width + gap) + car_width / 2
    draw_arrow(draw, arrow_x, 70, car_y - 14, ORANGE)
    for index in (0, 2, 3):
        cx = left + index * (car_width + gap) + car_width / 2
        draw.line([s(cx - 20), s(car_y - 52), s(cx + 20), s(car_y - 12)], fill=WHITE, width=s(7))
        draw.line([s(cx + 20), s(car_y - 52), s(cx - 20), s(car_y - 12)], fill=WHITE, width=s(7))

    final = image.resize((SIZE, SIZE), Image.LANCZOS)
    docs = os.path.join(ROOT, "Docs", "Images")
    resources = os.path.join(ROOT, "Resources")
    os.makedirs(docs, exist_ok=True)
    os.makedirs(resources, exist_ok=True)
    final.save(os.path.join(docs, "icon-512.png"))
    final.resize((128, 128), Image.LANCZOS).save(os.path.join(resources, "Icon128.png"))
    print("Wrote Docs/Images/icon-512.png and Resources/Icon128.png")


if __name__ == "__main__":
    main()
