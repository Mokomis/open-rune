"""Create a labeled comparison at equal display size, using bilinear originals."""
import argparse
import json
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

ap = argparse.ArgumentParser()
ap.add_argument('--root', type=Path, required=True)
ap.add_argument('--pack', type=Path, required=True)
ap.add_argument('--output', type=Path, required=True)
ap.add_argument('--ids', nargs='+', required=True)
ap.add_argument('--masked', action='store_true', help='Show masked variants on a neutral background')
args = ap.parse_args()
inventory = {r['id']:r for r in json.loads((args.root/'inventory.json').read_text())['textures']}
fontpath = Path('/System/Library/Fonts/Supplemental/Arial.ttf')
font = ImageFont.truetype(str(fontpath),18) if fontpath.exists() else ImageFont.load_default(size=18)
rows = []
for identity in args.ids:
    row = inventory[identity]
    source = Image.open(args.root/row['directory']/row['file']).convert('RGB')
    upscaled = Image.open(args.pack/'4x'/identity.split('.')[0]/row['file']).convert('RGB')
    if args.masked:
        source = source.convert('RGBA')
        source.putalpha(Image.open(args.root/row['directory']/row['mask_file']).convert('L'))
        upscaled = Image.open(args.pack/'4x'/identity.split('.')[0]/Path(row['file']).with_suffix('.masked.png'))
    scale = min(512/source.width,512/source.height)
    size = (round(source.width*scale),round(source.height*scale))
    panel = Image.new('RGB',(1056,size[1]+70),(22,22,25))
    draw = ImageDraw.Draw(panel)
    draw.text((16,6),identity,font=font,fill=(230,211,170))
    draw.text((16,30),'Original (bilinear filtering)',font=font,fill='white')
    draw.text((536,30),'AI 4× master',font=font,fill='white')
    for x, im, filter in [(16,source,Image.Resampling.BILINEAR),(536,upscaled,Image.Resampling.LANCZOS)]:
        tile = im.resize(size,filter)
        if args.masked:
            background = Image.new('RGBA',size,(90,100,110,255))
            tile = Image.alpha_composite(background,tile.convert('RGBA')).convert('RGB')
        panel.paste(tile,(x,58))
    rows.append(panel)
canvas = Image.new('RGB',(1056,sum(r.height for r in rows)),(22,22,25))
y=0
for row in rows:
    canvas.paste(row,(0,y));y+=row.height
args.output.parent.mkdir(parents=True,exist_ok=True)
canvas.save(args.output)
