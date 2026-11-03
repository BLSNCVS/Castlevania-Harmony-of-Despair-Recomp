# Usage: python3 make_icon.py <Cinzel.ttf> <out.ico> <out.png>
# Draws the hodrecomp app icon: a gold Cinzel "HD" monogram on a dark
# rounded tile with a crimson frame. Writes a multi-size .ico and a 256px PNG.
import sys
from PIL import Image, ImageDraw, ImageFont, ImageFilter

font_path, out_ico, out_png = sys.argv[1:4]
S = 1024  # draw large, downsample for clean edges
img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
d = ImageDraw.Draw(img)
pad, radius = 40, 190
# Outer crimson frame and dark inner tile.
d.rounded_rectangle([pad, pad, S - pad, S - pad], radius, fill=(140, 26, 36, 255))
inner = pad + 34
d.rounded_rectangle([inner, inner, S - inner, S - inner], radius - 30, fill=(22, 11, 14, 255))
# Thin gold rule inside the frame.
rule = inner + 26
d.rounded_rectangle([rule, rule, S - rule, S - rule], radius - 50, outline=(214, 170, 92, 255), width=10)

# Monogram with a soft crimson glow behind it.
font = ImageFont.truetype(font_path, 470)
text = "HD"
bbox = d.textbbox((0, 0), text, font=font)
w, h = bbox[2] - bbox[0], bbox[3] - bbox[1]
pos = ((S - w) / 2 - bbox[0], (S - h) / 2 - bbox[1] + 6)
glow = Image.new("RGBA", (S, S), (0, 0, 0, 0))
ImageDraw.Draw(glow).text(pos, text, font=font, fill=(200, 40, 50, 210))
glow = glow.filter(ImageFilter.GaussianBlur(28))
img.alpha_composite(glow)
d = ImageDraw.Draw(img)
d.text(pos, text, font=font, fill=(237, 199, 115, 255))

big = img.resize((256, 256), Image.LANCZOS)
big.save(out_png)
big.save(out_ico, sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
