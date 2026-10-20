import os
import struct
from PIL import Image, ImageDraw, ImageFont, ImageFilter

def rgb888_to_rgb565(r, g, b):
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)

def encode_item(img, bpp, b0, max_colors, has_transparency=True, additive=False):
    w, h = img.size
    pixels = img.load()
    palette = []
    color_map = {}
    
    if has_transparency and not additive:
        palette.append(0xF81F) # Magenta transparency key in HLE/midas
        color_map['TRANS'] = 0
        
    for y in range(h):
        for x in range(w):
            p = pixels[x, y]
            if has_transparency and p[3] < 128:
                continue
            c16 = rgb888_to_rgb565(p[0], p[1], p[2])
            if has_transparency and not additive and c16 == 0xF81F:
                c16 = 0xF81E # Prevent accidental key collision
            if c16 not in color_map:
                if len(palette) < max_colors:
                    color_map[c16] = len(palette)
                    palette.append(c16)
                    
    while len(palette) < 2:
        palette.append(0)
        
    pal_count = len(palette)
    header = struct.pack('<BBHH', b0, pal_count, w, h)
    
    pal_bytes = bytearray()
    for c in palette:
        pal_bytes += struct.pack('<H', c)
        
    def find_best_idx(c16):
        if c16 in color_map:
            return color_map[c16]
        r1 = (c16 >> 11) & 0x1F
        g1 = (c16 >> 5) & 0x3F
        b1 = c16 & 0x1F
        best_d = 999999
        best_i = 0
        start_i = 1 if (has_transparency and not additive) else 0
        for i in range(start_i, len(palette)):
            pal_c = palette[i]
            r2 = (pal_c >> 11) & 0x1F
            g2 = (pal_c >> 5) & 0x3F
            b2 = pal_c & 0x1F
            d = (r1 - r2)**2 + (g1 - g2)**2 + (b1 - b2)**2
            if d < best_d:
                best_d = d
                best_i = i
        return best_i

    pixel_bytes = bytearray()
    if bpp == 8:
        for y in range(h):
            for x in range(w):
                p = pixels[x, y]
                if has_transparency and p[3] < 128:
                    pixel_bytes.append(0)
                else:
                    c16 = rgb888_to_rgb565(p[0], p[1], p[2])
                    if has_transparency and not additive and c16 == 0xF81F: c16 = 0xF81E
                    pixel_bytes.append(find_best_idx(c16))
    elif bpp == 4:
        row_stride = (w + 1) // 2
        for y in range(h):
            row = bytearray(row_stride)
            for x in range(w):
                p = pixels[x, y]
                if has_transparency and p[3] < 128:
                    idx = 0
                else:
                    c16 = rgb888_to_rgb565(p[0], p[1], p[2])
                    idx = find_best_idx(c16) & 0x0F
                if x % 2 == 0:
                    row[x // 2] |= (idx & 0x0F)
                else:
                    row[x // 2] |= ((idx & 0x0F) << 4)
            pixel_bytes += row
            
    return header + pal_bytes + pixel_bytes

def generate_heroes_lore_logo():
    W, H = 120, 57
    scale = 4
    SW, SH = W * scale, H * scale
    
    font_path = 'C:/Windows/Fonts/georgiab.ttf'
    f_h = ImageFont.truetype(font_path, int(23 * scale))
    f_l = ImageFont.truetype(font_path, int(23 * scale))
    
    t1 = 'HEROES'
    t2 = 'LORE'
    
    b1 = f_h.getbbox(t1)
    b2 = f_l.getbbox(t2)
    w1, h1 = b1[2] - b1[0], b1[3] - b1[1]
    w2, h2 = b2[2] - b2[0], b2[3] - b2[1]
    
    x1 = (SW - w1) // 2
    y1 = 0
    x2 = (SW - w2) // 2
    y2 = int(26.5 * scale)
    
    mask = Image.new('L', (SW, SH), 0)
    mdraw = ImageDraw.Draw(mask)
    mdraw.text((x1, y1), t1, font=f_h, fill=255)
    mdraw.text((x2, y2), t2, font=f_l, fill=255)
    
    outline_mask = mask.filter(ImageFilter.MaxFilter(7))
    
    canvas = Image.new('RGBA', (SW, SH), (0, 0, 0, 0))
    canvas.paste((0, 0, 0, 255), (0, 0), outline_mask)
    
    grad = Image.new('RGBA', (SW, SH), (0, 0, 0, 0))
    gdraw = ImageDraw.Draw(grad)
    for y in range(SH):
        line_y = y if y < y2 else (y - y2)
        lh = float(y2 if y < y2 else (SH - y2))
        pos = max(0.0, min(1.0, line_y / lh))
        
        if pos < 0.4:
            val = int(255 - pos / 0.4 * 40)
            col = (val, val, min(255, val + 15), 255)
        elif pos < 0.52:
            col = (100, 110, 130, 255)
        else:
            val = int(185 + (pos - 0.52) / 0.48 * 60)
            col = (val, val, min(255, val + 10), 255)
        gdraw.line([(0, y), (SW, y)], fill=col)
        
    canvas.paste(grad, (0, 0), mask)
    final = canvas.resize((W, H), Image.LANCZOS)
    
    f_arr = final.load()
    for y in range(H):
        for x in range(W):
            p = f_arr[x, y]
            if p[3] < 100:
                f_arr[x, y] = (0, 0, 0, 0)
            elif p[3] < 200 and sum(p[:3]) < 100:
                f_arr[x, y] = (0, 0, 0, 255)
    return final

def generate_pixel_subtitle():
    W, H = 56, 14
    im = Image.new('RGB', (W, H), (0, 0, 0))
    draw = ImageDraw.Draw(im)
    
    FONT = {
        'A': [' # ', '# #', '###', '# #', '# #'],
        'B': ['## ', '# #', '## ', '# #', '## '],
        'C': [' ##', '#  ', '#  ', '#  ', ' ##'],
        'D': ['## ', '# #', '# #', '# #', '## '],
        'E': ['###', '#  ', '## ', '#  ', '###'],
        'F': ['###', '#  ', '## ', '#  ', '#  '],
        'G': [' ##', '#  ', '# #', '# #', ' ##'],
        'H': ['# #', '# #', '###', '# #', '# #'],
        'I': ['###', ' # ', ' # ', ' # ', '###'],
        'K': ['# #', '## ', '#  ', '## ', '# #'],
        'L': ['#  ', '#  ', '#  ', '#  ', '###'],
        'M': ['# #', '###', '# #', '# #', '# #'],
        'N': ['## ', '# #', '# #', '# #', '# #'],
        'O': [' # ', '# #', '# #', '# #', ' # '],
        'P': ['## ', '# #', '## ', '#  ', '#  '],
        'R': ['## ', '# #', '## ', '# #', '# #'],
        'S': [' ##', '#  ', ' # ', '  #', '## '],
        'T': ['###', ' # ', ' # ', ' # ', ' # '],
        'U': ['# #', '# #', '# #', '# #', '###'],
        'V': ['# #', '# #', '# #', '# #', ' # '],
        ' ': ['   ', '   ', '   ', '   ', '   '],
    }
    
    def draw_text(txt, start_x, start_y):
        cur_x = start_x
        for ch in txt:
            glyph = FONT.get(ch, FONT[' '])
            for r, row in enumerate(glyph):
                for c, val in enumerate(row):
                    if val == '#':
                        draw.point((cur_x + c, start_y + r), fill=(255, 255, 255))
            cur_x += len(glyph[0]) + 1
            
    x1 = (W - 43) // 2
    draw_text('COVENANT OF', x1, 1)
    
    x2 = (W - 31) // 2
    draw_text('DARKNESS', x2, 8)
    return im

def generate_button(w, h, text):
    new_btn = Image.new('RGBA', (w, h), (0, 0, 0, 0))
    draw = ImageDraw.Draw(new_btn)
    draw.rounded_rectangle([2, 1, w-3, h-2], radius=8, fill=(16, 20, 30, 255), outline=(90, 100, 120, 255), width=1)
    font = ImageFont.truetype('C:/Windows/Fonts/arialbd.ttf', 11)
    bbox = font.getbbox(text)
    tw, th = bbox[2] - bbox[0], bbox[3] - bbox[1]
    tx = (w - tw) // 2
    ty = (h - th) // 2 - 1
    draw.text((tx+1, ty+1), text, font=font, fill=(0, 0, 0, 255))
    draw.text((tx, ty), text, font=font, fill=(240, 245, 255, 255))
    return new_btn

def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    ref_mgr = os.path.join(root, 'reference', 'extracted_vfs', 'c', 'sp', 'imgcom', 'title.mgr')
    with open(ref_mgr, 'rb') as f:
        orig = f.read()

    count = struct.unpack('<I', orig[:4])[0]
    offset = 4
    items = []
    for i in range(count):
        sz = struct.unpack('<I', orig[offset:offset+4])[0]
        offset += 4
        items.append(orig[offset:offset+sz])
        offset += sz

    im5 = generate_heroes_lore_logo()
    im6 = Image.new('RGBA', (112, 49), (0, 0, 0, 0))
    im8 = generate_button(94, 25, 'TOUCH SCREEN')
    im9 = generate_button(86, 24, 'PRESS START')
    im19 = generate_pixel_subtitle()

    items[5] = encode_item(im5, bpp=8, b0=0x18, max_colors=122, has_transparency=True)
    items[6] = encode_item(im6, bpp=8, b0=0x18, max_colors=10, has_transparency=True)
    items[8] = encode_item(im8, bpp=4, b0=0x14, max_colors=16, has_transparency=True)
    items[9] = encode_item(im9, bpp=4, b0=0x14, max_colors=16, has_transparency=True)
    items[19] = encode_item(im19, bpp=4, b0=0x04, max_colors=16, has_transparency=False, additive=True)

    out = bytearray()
    out += struct.pack('<I', len(items))
    for item in items:
        out += struct.pack('<I', len(item))
        out += item

    targets = [
        os.path.join(root, 'assets_override', 'c', 'sp', 'imgcom', 'title.mgr'),
        os.path.join(root, 'dist', 'assets_override', 'c', 'sp', 'imgcom', 'title.mgr')
    ]
    for t in targets:
        os.makedirs(os.path.dirname(t), exist_ok=True)
        with open(t, 'wb') as f:
            f.write(out)
        print(f'Wrote {len(out)} bytes to {t}')

if __name__ == '__main__':
    main()
