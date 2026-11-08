#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Render original vector-inspired PS5Term branding; requires Pillow."""
from pathlib import Path
from PIL import Image, ImageDraw
root=Path(__file__).resolve().parents[1]
(root/'assets/branding').mkdir(parents=True,exist_ok=True)
svg='''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 512 512"><rect width="512" height="512" fill="#0a0f17"/><rect x="64" y="96" width="384" height="320" rx="48" fill="#111823" stroke="#222f3e" stroke-width="3"/><path d="M136 198 L206 256 L136 314" fill="none" stroke="#59e5ce" stroke-width="24" stroke-linecap="square" stroke-linejoin="miter"/><path d="M246 314 H360" stroke="#d4e0ed" stroke-width="24"/><path d="M96 96 H416" stroke="#59e5ce" stroke-width="6"/></svg>'''
(root/'assets/branding/logo.svg').write_text(svg+'\n')
scale=4
im=Image.new('RGB',(512*scale,512*scale),(10,15,23));d=ImageDraw.Draw(im)
def box(coords,fill,radius=0,outline=None,width=1):
 coords=tuple(x*scale for x in coords)
 if radius:d.rounded_rectangle(coords,radius*scale,fill=fill,outline=outline,width=width*scale)
 else:d.rectangle(coords,fill=fill)
box((64,96,448,416),(17,24,35),48,(34,47,62),3)
d.line([(136*scale,198*scale),(206*scale,256*scale),(136*scale,314*scale)],fill=(89,229,206),width=24*scale)
d.line([(246*scale,314*scale),(360*scale,314*scale)],fill=(212,224,237),width=24*scale)
d.line([(96*scale,96*scale),(416*scale,96*scale)],fill=(89,229,206),width=6*scale)
im=im.resize((512,512),Image.Resampling.LANCZOS)
im.save(root/'sce_sys/icon0.png')
im.save(root/'assets/branding/icon.png')
print('Wrote original SVG mark and 512px launcher icon')
