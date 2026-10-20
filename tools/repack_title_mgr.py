import os
import struct
from PIL import Image, ImageDraw, ImageFont, ImageFilter

def rgb888_to_rgb565(r, g, b):
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)

def generate_logo_images():
    scale = 4
    W6, H6 = 112 * scale, 49 * scale
    
    font_path = 'C:/Windows/Fonts/georgiab.ttf'
    if not os.path.exists(font_path):
        font_path = 'C:/Windows/Fonts/timesbd.ttf'
    if not os.path.exists(font_path):
        font_path = '/usr/share/fonts/truetype/dejavu/DejaVuSerif-Bold.ttf'
        
    f_heroes = ImageFont.truetype(font_path, int(18 * scale))
    f_lore = ImageFont.truetype(font_path, int(22 * scale))

    mask6 = Image.new('L', (W6, H6), 0)
    d6 = ImageDraw.Draw(mask6)

    h_chars = list('HEROES')
    h_widths = [f_heroes.getbbox(c)[2] - f_heroes.getbbox(c)[0] for c in h_chars]
    hx = int(3 * scale)
    hy = int(2 * scale)
    cur_x = hx
    for i, c in enumerate(h_chars):
        d6.text((cur_x, hy), c, font=f_heroes, fill=255)
        cur_x += h_widths[i] + int(2.2 * scale)

    l_chars = list('LORE')
    l_widths = [f_lore.getbbox(c)[2] - f_lore.getbbox(c)[0] for c in l_chars]
    lx = int(5 * scale)
    ly = int(22 * scale)
    cur_x = lx
    for i, c in enumerate(l_chars):
        d6.text((cur_x, ly), c, font=f_lore, fill=255)
        cur_x += l_widths[i] + int(5.0 * scale)

    # Metallic chrome gradient with warm highlights
    grad = Image.new('RGBA', (W6, H6), (0, 0, 0, 0))
    gdraw = ImageDraw.Draw(grad)
    for y in range(H6):
        if y < ly:
            rel_y = (y - hy) / float(max(1, ly - hy))
        else:
            rel_y = (y - ly) / float(max(1, H6 - ly))
        rel_y = max(0.0, min(1.0, rel_y))
        if rel_y < 0.35:
            v = int(255 - (rel_y / 0.35) * 30)
            c = (v, v, int(v * 0.96), 255)
        elif rel_y < 0.50:
            t = (rel_y - 0.35) / 0.15
            v = int(225 - t * 85)
            c = (v, v, int(v * 0.98), 255)
        elif rel_y < 0.55:
            t = (rel_y - 0.50) / 0.05
            v = int(95 + t * 90)
            c = (v, v, v + 12, 255)
        elif rel_y < 0.85:
            t = (rel_y - 0.55) / 0.30
            v = int(185 + t * 65)
            c = (v, v, min(255, v + 6), 255)
        else:
            t = (rel_y - 0.85) / 0.15
            v = int(250 - t * 60)
            c = (v, v, v, 255)
        gdraw.line([(0, y), (W6, y)], fill=c)

    # 1px bevel border on letters
    border_mask = mask6.filter(ImageFilter.MaxFilter(int(1 * scale) * 2 + 1))
    im6_bordered = Image.new('RGBA', (W6, H6), (0, 0, 0, 0))
    edge_layer = Image.new('RGBA', (W6, H6), (28, 35, 28, 255))
    im6_bordered.paste(edge_layer, (0, 0), border_mask)
    im6_bordered.paste(grad, (0, 0), mask6)
    item6 = im6_bordered.resize((112, 49), Image.LANCZOS)

    # Item 5: Drop shadow & outline layer (120x57, offset +4, +4)
    W5, H5 = 120 * scale, 57 * scale
    mask5 = Image.new('L', (W5, H5), 0)
    mask5.paste(border_mask, (4 * scale, 4 * scale))
    shadow_mask = mask5.filter(ImageFilter.MaxFilter(int(2.4 * scale) * 2 + 1))
    shadow_layer = Image.new('RGBA', (W5, H5), (0, 0, 0, 0))
    shadow_layer.paste((0, 0, 0, 255), (0, 0), shadow_mask)
    item5 = shadow_layer.resize((120, 57), Image.LANCZOS)

    # Clean alpha channels
    arr6 = item6.load()
    for y in range(49):
        for x in range(112):
            if arr6[x, y][3] < 80:
                arr6[x, y] = (0, 0, 0, 0)
            else:
                arr6[x, y] = (arr6[x, y][0], arr6[x, y][1], arr6[x, y][2], 255)

    arr5 = item5.load()
    for y in range(57):
        for x in range(120):
            if arr5[x, y][3] < 60:
                arr5[x, y] = (0, 0, 0, 0)
            else:
                arr5[x, y] = (0, 0, 0, 255)

    return item5, item6

def encode_item(img, trans_idx=115, total_pal=122):
    w, h = img.size
    pixels = img.load()
    color_map = {}
    palette = []
    
    for y in range(h):
        for x in range(w):
            p = pixels[x, y]
            if p[3] < 128:
                continue
            c16 = rgb888_to_rgb565(p[0], p[1], p[2])
            if c16 == 0xF81F:
                c16 = 0xF81E
            if c16 not in color_map:
                if len(palette) < trans_idx:
                    color_map[c16] = len(palette)
                    palette.append(c16)
                    
    while len(palette) < trans_idx:
        palette.append(0x0000)
        
    palette.append(0xF81F) # Trans key at trans_idx
    
    while len(palette) < total_pal:
        palette.append(0xFFFF)
        
    def find_best_idx(c16):
        if c16 in color_map:
            return color_map[c16]
        r1, g1, b1 = (c16 >> 11) & 0x1F, (c16 >> 5) & 0x3F, c16 & 0x1F
        best_d, best_i = 999999, 0
        for i in range(trans_idx):
            pal_c = palette[i]
            r2, g2, b2 = (pal_c >> 11) & 0x1F, (pal_c >> 5) & 0x3F, pal_c & 0x1F
            d = (r1 - r2)**2 + (g1 - g2)**2 + (b1 - b2)**2
            if d < best_d:
                best_d = d
                best_i = i
        return best_i

    pal_bytes = bytearray()
    for c in palette:
        pal_bytes += struct.pack('<H', c)
        
    pixel_bytes = bytearray()
    for y in range(h):
        for x in range(w):
            p = pixels[x, y]
            if p[3] < 128:
                pixel_bytes.append(trans_idx)
            else:
                c16 = rgb888_to_rgb565(p[0], p[1], p[2])
                if c16 == 0xF81F: c16 = 0xF81E
                pixel_bytes.append(find_best_idx(c16))
                
    header = struct.pack('<BBHH', 0x18, len(palette), w, h)
    return header + pal_bytes + pixel_bytes

def clear_additive_item(orig_item):
    b0, pal_count, w, h = struct.unpack('<BBHH', orig_item[:6])
    pal_end = 6 + pal_count * 2
    header_and_pal = orig_item[:pal_end]
    bpp = 8 if (b0 & 0x08) else 4
    if bpp == 8:
        cleared_pixels = bytearray(w * h)
    else:
        row_stride = (w + 1) // 2
        cleared_pixels = bytearray(row_stride * h)
    return header_and_pal + cleared_pixels

def clear_item_19(orig_item):
    b0, pal_count, w, h = struct.unpack('<BBHH', orig_item[:6])
    pal_end = 6 + pal_count * 2
    header_and_pal = orig_item[:pal_end]
    row_stride = (w + 1) // 2
    # Item 19 transparent index is 12 (0xCC in 4bpp pairs)
    cleared_pixels = bytearray([0xCC] * (row_stride * h))
    return header_and_pal + cleared_pixels

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

    # Generate custom western HEROES LORE logo
    im5, im6 = generate_logo_images()
    items[5] = encode_item(im5, trans_idx=117)
    items[6] = encode_item(im6, trans_idx=115)
    
    # Items 14-18: Korean subtitle characters ("칠흑의 계약") -> clear in additive mode
    for i in range(14, 19):
        items[i] = clear_additive_item(items[i])
        
    # Item 19: Korean subtitle banner mask -> set to transparent index 12
    items[19] = clear_item_19(items[19])
    
    # Items 8 and 9: keep authentic original "Tap to Continue" from reference title.mgr

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
